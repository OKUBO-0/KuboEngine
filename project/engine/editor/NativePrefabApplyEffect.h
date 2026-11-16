#pragma once
#include "PrefabApplyAdmission.h"
#include "PrefabPendingTransactions.h"
#include "PrefabSourceHistoryEffect.h"

namespace Engine::Editor {
// Initial native source publication + subsequent archived-pair Undo/Redo.
// Used only by the serialized authoring owner. End is called AFTER the admission
// publishes World/selection/history; archive failure retains pending and disables
// replay. Source-only values never substitute for native identity/byte checks.
class NativePrefabApplyEffect final:public PrefabApplyEffect {
    using NativeFile=HeldProjectPrefabRevision::NativeFile;
    using Directory=PrefabSourceAcknowledgement::Directory;
public:
    enum class Completion {NotStarted,Pending,Archived};
    struct Hooks {
        // Test-only boundary probes; production leaves all hooks empty.
        std::function<void()> afterPrepare,afterPark,afterPublish,beforeCompletion,afterArchive;
        std::function<void(const std::filesystem::path&)> beforeArchive;
    };
private:
    std::filesystem::path manifest_,projectRoot_,root_,pending_,archive_;
    ProjectPrefabRevision expected_;
    PrefabValue next_;
    std::string nextBytes_,journalBytes_;
    PrefabFileIdentity nextIdentity_,journalIdentity_;
    std::unique_ptr<PrefabSourcePublication> initial_;
    std::unique_ptr<PrefabSourceHistoryEffect> replay_;
    Hooks hooks_;
    Completion completion_=Completion::NotStarted;
    bool used_=false,initialSucceeded_=false;
    static size_t Cost(const Base::ProjectContext& project,const ProjectPrefabRevision& revision,const PrefabValue& next) {
        // Fixed serialized snapshot/path accounting, including initial state and
        // the later replay receipt. This is not an exact process heap measurement.
        return sizeof(NativePrefabApplyEffect)+sizeof(PrefabSourceHistoryEffect)+
            // Immutable intent contains escaped copies (up to twice the raw
            // validated JSON bytes), plus writer and replay snapshots. Reserve
            // initial/transient retained copies too, not just the final receipt.
            6*(revision.bytes.size()+revision.metadataBytes.size()+next.dump().size()+revision.guid.size())+
            2*revision.definition.dump().size()+16384+
            16*(project.GetProjectFile().native().size()+revision.file.native().size()+revision.metadata.native().size()+4096)*sizeof(wchar_t);
    }
    void OpenProject(Base::ProjectContext& project) const {
        std::string error;if(!project.Open(manifest_,error))throw std::runtime_error(error);
        if(std::filesystem::canonical(project.GetRootPath())!=projectRoot_ ||
            std::filesystem::canonical(project.ResolvePath(project.GetSettings().assetDirectory))!=root_)
            throw std::runtime_error("Native Apply project/asset root changed");
        if(std::filesystem::exists(root_/".kubo/asset-move.pending.json"))throw std::runtime_error("Recover pending asset move before native Apply");
    }
    void RequireNoPending(const Base::ProjectContext& project) const {
        std::vector<PrefabPendingEntry> entries;std::string error;
        if(!PrefabPendingTransactions::Scan(project,entries,error))throw std::runtime_error(error);
        if(!entries.empty())throw std::runtime_error("Recover pending Prefab transactions before native Apply");
    }
    void CheckArchiveParent() const {
        const auto parent=archive_.parent_path();
        if(!ProjectPrefabDetail::Within(std::filesystem::weakly_canonical(parent),root_))throw std::runtime_error("Apply archive parent escapes asset root");
        const auto attributes=GetFileAttributesW(parent.c_str());
        if(attributes!=INVALID_FILE_ATTRIBUTES && (!(attributes&FILE_ATTRIBUTE_DIRECTORY) || (attributes&FILE_ATTRIBUTE_REPARSE_POINT)))
            throw std::runtime_error("Apply archive parent is not ordinary storage");
        if(attributes==INVALID_FILE_ATTRIBUTES && GetLastError()!=ERROR_FILE_NOT_FOUND && GetLastError()!=ERROR_PATH_NOT_FOUND)
            throw std::runtime_error("Cannot inspect Apply archive parent");
    }
    PrefabFileIdentity ReadHeld(NativeFile& file,const std::filesystem::path& path,const std::string& expected,size_t limit,
        const PrefabFileIdentity* expectedIdentity=nullptr) const {
        LARGE_INTEGER zero{};if(!SetFilePointerEx(file.handle,zero,nullptr,FILE_BEGIN))throw std::runtime_error("Cannot rewind native Apply file");
        std::string bytes;PrefabFileIdentity identity;std::filesystem::path final;
        HeldProjectPrefabRevision::Read(file,root_,limit,bytes,identity,final);
        if(bytes!=expected || (expectedIdentity && identity!=*expectedIdentity) || final!=std::filesystem::canonical(path))
            throw std::runtime_error("Native Apply file identity/bytes/path changed");
        return identity;
    }
    void RevalidatePair(bool published) const {
        Base::ProjectContext project;OpenProject(project);
        const auto& transaction=*initial_;const auto& writer=transaction.writer_;
        ReadHeld(*writer.source_,transaction.backup_,expected_.bytes,Scene::SceneDocument::MaxBytes,&expected_.fileIdentity);
        ReadHeld(*writer.metadata_,expected_.metadata,expected_.metadataBytes,16384,&expected_.metadataIdentity);
        ReadHeld(*transaction.stageFile_,published?transaction.source_:transaction.stage_,nextBytes_,Scene::SceneDocument::MaxBytes,&nextIdentity_);
        ReadHeld(*transaction.journal_,pending_/"pending.json",journalBytes_,20*1024*1024,&journalIdentity_);
        if(published) {
            const auto attributes=GetFileAttributesW(transaction.stage_.c_str());const auto code=GetLastError();
            if(attributes!=INVALID_FILE_ATTRIBUTES || (code!=ERROR_FILE_NOT_FOUND && code!=ERROR_PATH_NOT_FOUND))
                throw std::runtime_error("Native Apply stage name became occupied");
        }
        Assets::AssetRegistry registry;std::string error;if(!registry.Scan(root_,error,Assets::AssetRegistry::MetadataPolicy::ReadOnly))throw std::runtime_error(error);
        const auto* record=registry.FindByGuid(expected_.guid);
        if((published && !record) || (record && (record->type!=Assets::AssetType::Prefab || Base::PathFromUtf8(record->absolutePath)!=expected_.file)))
            throw std::runtime_error("Native Apply GUID became ambiguous/moved");
    }
public:
    NativePrefabApplyEffect(const Base::ProjectContext& project,ProjectPrefabRevision revision,const PrefabValue& next,Hooks hooks={}):
        PrefabApplyEffect(Cost(project,revision,next)),manifest_(project.GetProjectFile()),
        projectRoot_(std::filesystem::canonical(project.GetRootPath())),root_(std::filesystem::canonical(project.ResolvePath(project.GetSettings().assetDirectory))),
        expected_(std::move(revision)),hooks_(std::move(hooks)) {
        std::string error;PrefabValue original;
        if(expected_.bytes.size()>Scene::SceneDocument::MaxBytes || expected_.metadataBytes.size()>16384 ||
            !ValidatePrefabDocument(expected_.definition,original,error) || !ValidatePrefabDocument(next,next_,error))
            throw std::runtime_error(error.empty()?"Native Apply revision exceeds bounds":error);
        expected_.definition=std::move(original);
        if(!Scene::ModelComponent::IsValidGuid(expected_.guid) || !ProjectPrefabDetail::Within(root_,projectRoot_) ||
            next_["root"].get<crude_json::number>()!=expected_.definition["root"].get<crude_json::number>())
            throw std::runtime_error("Native Apply source/root identity invalid");
        nextBytes_=next_.dump();pending_=root_/".kubo/prefab-transactions"/expected_.guid;
        static std::atomic<unsigned long long> sequence{0};
        archive_=root_/".kubo/prefab-archive"/(expected_.guid+"-apply-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64())+"-"+std::to_string(++sequence));
        CheckArchiveParent();
    }
    ~NativePrefabApplyEffect() override{End();}
    Completion CompletionState() const noexcept{return completion_;}
    bool HasRetainedPending() const noexcept{return completion_==Completion::Pending || (replay_ && replay_->HasRetainedPending());}
    const std::filesystem::path& PendingPath() const noexcept{return pending_;}
    const std::filesystem::path& ArchivePath() const noexcept{return archive_;}
    bool PublishInitial(const PrefabValue& before,const PrefabValue& after,const std::string& guid,std::string& error) override {
        try {
            if(used_){error="Native Apply initial publication already used";return false;}used_=true;
            if(guid!=expected_.guid || before.dump()!=expected_.definition.dump() || after.dump()!=nextBytes_)
                throw std::runtime_error("Native Apply admission/source target mismatch");
            Base::ProjectContext project;OpenProject(project);RequireNoPending(project);CheckArchiveParent();
            Assets::AssetRegistry registry;if(!registry.Scan(root_,error,Assets::AssetRegistry::MetadataPolicy::ReadOnly))return false;
            {HeldProjectPrefabRevision revision;if(!revision.Open(project,registry,expected_.guid,error,&expected_))return false;
                if(revision.Snapshot().definition.dump()!=expected_.definition.dump())throw std::runtime_error("Native Apply definition differs from held source bytes");}
            initial_=std::make_unique<PrefabSourcePublication>();
            if(!initial_->Prepare(project,registry,expected_,next_,error)) {if(!initial_->Directory().empty())completion_=Completion::Pending;return false;}
            completion_=Completion::Pending;
            nextIdentity_=ReadHeld(*initial_->stageFile_,initial_->stage_,nextBytes_,Scene::SceneDocument::MaxBytes);
            PrefabSourceRecoveryReport receipt;receipt.state=PrefabSourceRecoveryState::Published;receipt.directory=archive_;receipt.source=expected_.file;
            receipt.guid=expected_.guid;receipt.originalBytes=expected_.bytes;receipt.nextBytes=nextBytes_;receipt.metadataBytes=expected_.metadataBytes;
            receipt.originalIdentity=expected_.fileIdentity;receipt.nextIdentity=nextIdentity_;receipt.metadataIdentity=expected_.metadataIdentity;
            PrefabValue intent;intent["format"]="kubo-prefab-source-publication-1";intent["guid"]=receipt.guid;
            intent["sourceRelative"]=Base::PathToUtf8(receipt.source.lexically_relative(root_));intent["originalBytes"]=receipt.originalBytes;intent["nextBytes"]=receipt.nextBytes;intent["metadataBytes"]=receipt.metadataBytes;
            intent["originalIdentity"]=PrefabSourcePublication::Identity(receipt.originalIdentity);intent["nextIdentity"]=PrefabSourcePublication::Identity(receipt.nextIdentity);intent["metadataIdentity"]=PrefabSourcePublication::Identity(receipt.metadataIdentity);
            journalBytes_=intent.dump();journalIdentity_=ReadHeld(*initial_->journal_,pending_/"pending.json",journalBytes_,20*1024*1024);
            replay_=std::make_unique<PrefabSourceHistoryEffect>(project,std::move(receipt));
            if(hooks_.afterPrepare)hooks_.afterPrepare();
            // Pair validation while parked can still fail safely through the
            // publication core's no-replace rollback/retained-conflict path.
            if(!initial_->Publish(error,[&]{if(hooks_.afterPark)hooks_.afterPark();RevalidatePair(false);}))return false;
            if(hooks_.afterPublish)hooks_.afterPublish();
            // Production has no allocation after successful physical publication
            // before returning ownership to the no-throw admission publication.
            initialSucceeded_=true;error.clear();return true;
        }catch(const std::exception& exception){error=exception.what();return false;}
    }
    bool Begin(bool undo,std::string& error) override {
        try {
            if(completion_!=Completion::Archived || initial_ || !replay_){error="Native Apply pending/unacknowledged; recover before history replay";return false;}
            Base::ProjectContext project;OpenProject(project);RequireNoPending(project);
            return replay_->Begin(undo,error);
        }catch(const std::exception& exception){error=exception.what();return false;}
    }
    void End() noexcept override {
        if(initial_) {
            if(initialSucceeded_) {
                try {
                    if(hooks_.beforeCompletion)hooks_.beforeCompletion();
                    CheckArchiveParent();std::filesystem::create_directories(archive_.parent_path());CheckArchiveParent();
                    Directory directory(pending_,root_);
                    if(hooks_.beforeArchive)hooks_.beforeArchive(archive_);
                    RevalidatePair(true);
                    // Keep active NEW source, metadata and GUID writer owned.
                    // Only the pending journal/old backup child readers close
                    // for the whole-directory Windows rename, preserving unknowns.
                    initial_->journal_.reset();initial_->writer_.source_.reset();
                    PrefabSourcePublication::Rename(directory.handle,archive_);completion_=Completion::Archived;
                    if(hooks_.afterArchive)hooks_.afterArchive();
                }catch(...){/* World/source succeeded: preserve Pending on archive failure. */}
            }
            initial_.reset();initialSucceeded_=false;
        }
        if(replay_)replay_->End();
    }
};
}
