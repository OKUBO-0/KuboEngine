#pragma once
#include "PrefabSourcePublication.h"
#include <vector>

namespace Engine::Editor {
enum class PrefabSourceRecoveryState { Prepared, Parked, Published, Conflict };
struct PrefabSourceRecoveryReport {
    PrefabSourceRecoveryState state=PrefabSourceRecoveryState::Conflict;
    std::filesystem::path directory,source;
    std::string guid,originalBytes,nextBytes,metadataBytes;
    PrefabFileIdentity originalIdentity,nextIdentity,metadataIdentity;
};
// Inspection is read-only. Restore reacquires ownership and validates a report
// again; a released inspection alone is never mutation authorization.
class PrefabSourceRecovery final {
    friend class PrefabSourceAcknowledgement;
    friend class PrefabSourceHistoryEffect;
    friend class PrefabSourceHistoryRecovery;
    using NativeFile=HeldProjectPrefabRevision::NativeFile;
    struct Observed {bool exists=false;std::string bytes;PrefabFileIdentity identity;NativeFile* file=nullptr;std::filesystem::path path;size_t limit=0;};
    struct Ownership {
        std::unique_ptr<NativeFile> lock;std::vector<std::unique_ptr<NativeFile>> files;
        void Release() noexcept{files.clear();lock.reset();}
    };
public:
    static bool Inspect(const Base::ProjectContext& project,const std::string& guid,
        PrefabSourceRecoveryReport& output,std::string& error) {
        return InspectHeld(project,guid,output,error,false,{});
    }
    // Restore only source files. Keep journal/stage and the pending directory:
    // this does not acknowledge World/history, clean up or unblock new writers.
    static bool RestoreOriginal(const Base::ProjectContext& project,const std::string& guid,
        const PrefabSourceRecoveryReport& expected,std::string& error,const std::function<void()>& afterNextPark={}) {
        PrefabSourceRecoveryReport unused;
        return InspectHeld(project,guid,unused,error,true,[&](const PrefabSourceRecoveryReport& current,const Observed& source,const Observed& backup,const Observed&,const std::function<void()>&,const std::function<void()>&){
            if(!Same(current,expected) || current.state==PrefabSourceRecoveryState::Conflict)
                throw std::runtime_error("Recovery state changed or conflicts; inspect again before restoring");
            if(current.state==PrefabSourceRecoveryState::Prepared)return;
            if(current.state==PrefabSourceRecoveryState::Parked){PrefabSourcePublication::Rename(backup.file->handle,current.source);return;}
            // Retain the new file with its identity intact before restoring the
            // original. Both destinations are no-replace, even after callbacks.
            PrefabSourcePublication::Rename(source.file->handle,current.directory/"next.prefab");
            try {
                if(afterNextPark)afterNextPark();
                PrefabSourcePublication::Rename(backup.file->handle,current.source);
            }catch(const std::exception& exception){
                std::string detail=exception.what();
                try {PrefabSourcePublication::Rename(source.file->handle,current.source);}
                catch(const std::exception&){detail+="; next stage and original backup retained because source is occupied/inaccessible";}
                throw std::runtime_error(detail);
            }
        });
    }
private:
    using Action=std::function<void(const PrefabSourceRecoveryReport&,const Observed&,const Observed&,const Observed&,const std::function<void()>&,const std::function<void()>&)>;
    using BeforeRead=std::function<void(const std::filesystem::path&,const std::filesystem::path&)>;
    static bool Same(const PrefabSourceRecoveryReport& a,const PrefabSourceRecoveryReport& b){
        return a.state==b.state && a.directory==b.directory && a.source==b.source && a.guid==b.guid &&
            a.originalBytes==b.originalBytes && a.nextBytes==b.nextBytes && a.metadataBytes==b.metadataBytes &&
            a.originalIdentity==b.originalIdentity && a.nextIdentity==b.nextIdentity && a.metadataIdentity==b.metadataIdentity;
    }
    static bool InspectHeld(const Base::ProjectContext& project,const std::string& guid,
        PrefabSourceRecoveryReport& output,std::string& error,bool ownDelete,const Action& action,
        const BeforeRead& beforeRead={},const std::filesystem::path& archive={},Ownership* retain=nullptr,
        const std::filesystem::path& heldHistoryPending={}) {
        try {
            if(!project.IsOpen() || !Scene::ModelComponent::IsValidGuid(guid))throw std::runtime_error("Recovery requires an open project and valid Prefab GUID");
            const auto root=std::filesystem::canonical(project.ResolvePath(project.GetSettings().assetDirectory));
            if(!ProjectPrefabDetail::Within(root,std::filesystem::canonical(project.GetRootPath())))throw std::runtime_error("Recovery asset root escapes project");
            auto directory=root/".kubo/prefab-transactions"/guid;
            if(!heldHistoryPending.empty() && (archive.empty() || heldHistoryPending!=directory))
                throw std::runtime_error("History inspection pending path mismatch");
            if(!archive.empty()){
                directory=archive.lexically_normal();
                if(directory.parent_path()!=root/".kubo/prefab-archive" || !Base::PathToUtf8(directory.filename()).starts_with(guid+"-") ||
                   (heldHistoryPending.empty() && std::filesystem::exists(root/".kubo/prefab-transactions"/guid)))throw std::runtime_error("History archive is unsafe or another source transaction is pending");
            }
            const auto attributes=GetFileAttributesW(directory.c_str());
            if(attributes==INVALID_FILE_ATTRIBUTES || !(attributes&FILE_ATTRIBUTE_DIRECTORY) || (attributes&FILE_ATTRIBUTE_REPARSE_POINT) ||
                !ProjectPrefabDetail::Within(std::filesystem::canonical(directory),root))throw std::runtime_error("Recovery transaction directory is missing/linked/unsafe");
            // Existing persistent writer lock only: inspection creates no files.
            const auto lockPath=root/".kubo/prefab-writers"/(guid+".lock");
            auto lock=std::make_unique<NativeFile>(lockPath,GENERIC_READ|GENERIC_WRITE);
            std::string lockBytes;PrefabFileIdentity lockIdentity;std::filesystem::path lockFinal;
            HeldProjectPrefabRevision::Read(*lock,root,0,lockBytes,lockIdentity,lockFinal);
            if(lockFinal!=std::filesystem::canonical(lockPath))throw std::runtime_error("Recovery writer lock pathname changed");
            if(beforeRead)beforeRead(directory,root);
            std::vector<std::unique_ptr<NativeFile>> held;
            const auto journal=Observe(directory/"pending.json",root,20*1024*1024,held);
            if(!journal.exists)throw std::runtime_error("Incomplete transaction has no recovery intent; preserve files for explicit recovery");
            const auto record=HeldProjectPrefabRevision::ParseHeldJson(journal.bytes);
            if(!record.is_object() || record.get<crude_json::object>().size()!=9)throw std::runtime_error("Unsupported recovery intent fields");
            for(const auto* key:{"format","guid","sourceRelative","metadataBytes","originalBytes","nextBytes"})
                if(!record.contains(key) || !record[key].is_string())throw std::runtime_error("Recovery intent has invalid text fields");
            for(const auto* key:{"originalIdentity","nextIdentity","metadataIdentity"})
                if(!record.contains(key))throw std::runtime_error("Recovery intent is missing a native identity");
            if(record["format"].get<crude_json::string>()!="kubo-prefab-source-publication-1" || record["guid"].get<crude_json::string>()!=guid)
                throw std::runtime_error("Recovery intent format/GUID mismatch");
            const auto relative=Base::PathFromUtf8(record["sourceRelative"].get<crude_json::string>());
            if(relative.empty() || relative.is_absolute() || relative.has_root_name())throw std::runtime_error("Recovery source must be asset-relative");
            for(const auto& part:relative)if(part==L"." || part==L"..")throw std::runtime_error("Recovery source traversal refused");
            const auto lexical=ProjectPrefabDetail::Path(project,project.ResolveAssetPath(relative));
            PrefabSourceRecoveryReport candidate;candidate.directory=directory;candidate.guid=guid;candidate.source=root/relative;
            if(std::filesystem::weakly_canonical(lexical)!=std::filesystem::weakly_canonical(candidate.source))throw std::runtime_error("Recovery source path changed");
            candidate.originalBytes=record["originalBytes"].get<crude_json::string>();candidate.nextBytes=record["nextBytes"].get<crude_json::string>();candidate.metadataBytes=record["metadataBytes"].get<crude_json::string>();
            if(candidate.originalBytes.size()>Scene::SceneDocument::MaxBytes || candidate.nextBytes.size()>Scene::SceneDocument::MaxBytes || candidate.metadataBytes.size()>16384)
                throw std::runtime_error("Recovery intent byte fields exceed limits");
            PrefabValue before,after;
            if(!ValidatePrefabDocument(HeldProjectPrefabRevision::ParseHeldJson(candidate.originalBytes),before,error) ||
               !ValidatePrefabDocument(HeldProjectPrefabRevision::ParseHeldJson(candidate.nextBytes),after,error))return false;
            if(before["root"].get<crude_json::number>()!=after["root"].get<crude_json::number>())throw std::runtime_error("Recovery intent root identity changed");
            const auto metadataDocument=HeldProjectPrefabRevision::ParseHeldJson(candidate.metadataBytes);
            if(!metadataDocument.is_object() || !metadataDocument.contains("version") || !metadataDocument["version"].is_number() ||
                metadataDocument["version"].get<crude_json::number>()!=1 || !metadataDocument.contains("guid") ||
                !metadataDocument["guid"].is_string() || metadataDocument["guid"].get<crude_json::string>()!=guid)throw std::runtime_error("Recovery metadata intent GUID mismatch");
            candidate.originalIdentity=Identity(record["originalIdentity"]);candidate.nextIdentity=Identity(record["nextIdentity"]);candidate.metadataIdentity=Identity(record["metadataIdentity"]);
            if(candidate.originalIdentity==candidate.nextIdentity || candidate.originalIdentity==candidate.metadataIdentity || candidate.nextIdentity==candidate.metadataIdentity)
                throw std::runtime_error("Recovery intent aliases file identities");
            const auto metadata=Observe(Assets::AssetRegistry::MetadataPath(candidate.source),root,16384,held);
            if(!Matches(metadata,candidate.metadataIdentity,candidate.metadataBytes))throw std::runtime_error("Recovery metadata was replaced/edited/missing");
            const auto access=GENERIC_READ|(ownDelete?DELETE:0);
            const auto source=Observe(candidate.source,root,Scene::SceneDocument::MaxBytes,held,access);
            const auto backup=Observe(directory/"original.prefab",root,Scene::SceneDocument::MaxBytes,held,access);
            const auto stage=Observe(directory/"next.prefab",root,Scene::SceneDocument::MaxBytes,held,access);
            if(Matches(source,candidate.originalIdentity,candidate.originalBytes) && !backup.exists && Matches(stage,candidate.nextIdentity,candidate.nextBytes))candidate.state=PrefabSourceRecoveryState::Prepared;
            else if(!source.exists && Matches(backup,candidate.originalIdentity,candidate.originalBytes) && Matches(stage,candidate.nextIdentity,candidate.nextBytes))candidate.state=PrefabSourceRecoveryState::Parked;
            else if(Matches(source,candidate.nextIdentity,candidate.nextBytes) && Matches(backup,candidate.originalIdentity,candidate.originalBytes) && !stage.exists)candidate.state=PrefabSourceRecoveryState::Published;
            else candidate.state=PrefabSourceRecoveryState::Conflict;
            if(action)action(candidate,source,backup,stage,
                [&]{for(const auto* observed:{&journal,&metadata,&source,&backup,&stage})Revalidate(*observed,root);},
                [&]{for(auto& file:held)if(file && (file.get()==journal.file || file.get()==backup.file || file.get()==stage.file))file.reset();});
            if(retain){retain->files=std::move(held);retain->lock=std::move(lock);}
            output=std::move(candidate);error.clear();return true;
        }catch(const std::exception& exception){error=exception.what();return false;}
    }
private:
    static PrefabFileIdentity Identity(const PrefabValue& value) {
        if(!value.is_object() || value.get<crude_json::object>().size()!=3)throw std::runtime_error("Invalid recovery native identity");
        const auto number=[&](const char* key){if(!value.contains(key) || !value[key].is_number())throw std::runtime_error("Invalid recovery identity field");
            const auto n=value[key].get<crude_json::number>();if(!std::isfinite(n) || n<0 || n>4294967295.0 || std::floor(n)!=n)throw std::runtime_error("Invalid recovery DWORD identity");return static_cast<DWORD>(n);};
        PrefabFileIdentity result{number("volume"),number("high"),number("low")};if(!result.indexHigh && !result.indexLow)throw std::runtime_error("Missing recovery file index");return result;
    }
    static bool Matches(const Observed& file,const PrefabFileIdentity& identity,const std::string& bytes){return file.exists && file.identity==identity && file.bytes==bytes;}
    static void Revalidate(const Observed& observed,const std::filesystem::path& root){
        if(!observed.exists){
            const auto attributes=GetFileAttributesW(observed.path.c_str());const auto code=GetLastError();
            if(attributes!=INVALID_FILE_ATTRIBUTES || (code!=ERROR_FILE_NOT_FOUND && code!=ERROR_PATH_NOT_FOUND))throw std::runtime_error("Recovery absent destination changed");return;
        }
        LARGE_INTEGER zero{};if(!SetFilePointerEx(observed.file->handle,zero,nullptr,FILE_BEGIN))throw std::runtime_error("Cannot rewind recovery ownership");
        std::string bytes;PrefabFileIdentity identity;std::filesystem::path final;
        HeldProjectPrefabRevision::Read(*observed.file,root,observed.limit,bytes,identity,final);
        if(bytes!=observed.bytes || identity!=observed.identity || final!=std::filesystem::canonical(observed.path))throw std::runtime_error("Recovery file changed before action");
    }
    static Observed Observe(const std::filesystem::path& path,const std::filesystem::path& root,size_t limit,std::vector<std::unique_ptr<NativeFile>>& held,DWORD access=GENERIC_READ) {
        Observed result;result.path=path;result.limit=limit;
        const auto attributes=GetFileAttributesW(path.c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES) {const auto code=GetLastError();if(code==ERROR_FILE_NOT_FOUND || code==ERROR_PATH_NOT_FOUND)return result;throw std::runtime_error("Cannot inspect recovery filename");}
        auto file=std::make_unique<NativeFile>(path,access);result.exists=true;result.file=file.get();std::filesystem::path final;
        HeldProjectPrefabRevision::Read(*file,root,limit,result.bytes,result.identity,final);
        if(final!=std::filesystem::canonical(path))throw std::runtime_error("Recovery pathname changed during read");held.push_back(std::move(file));return result;
    }
};
}
