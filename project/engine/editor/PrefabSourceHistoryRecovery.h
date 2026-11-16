#pragma once
#include "PrefabSourceAcknowledgement.h"
#include <type_traits>

namespace Engine::Editor {
struct PrefabSourceHistoryRecoveryReport {
    bool undo=false;
    std::filesystem::path pending;
    std::string markerBytes;
    PrefabFileIdentity markerIdentity;
    PrefabSourceRecoveryReport source;
};
// Read-only diagnosis of a retained history replay. Physical source state does
// not prove that the vanished process published its Scene/history/document.
class PrefabSourceHistoryRecovery final {
    using NativeFile=HeldProjectPrefabRevision::NativeFile;
    using Directory=PrefabSourceAcknowledgement::Directory;
public:
    static bool Inspect(const Base::ProjectContext& project,const std::string& guid,
        PrefabSourceHistoryRecoveryReport& output,std::string& error) {
        return InspectHeld(project,guid,output,error,false,{});
    }
    // Explicit source-only rollback to the version active before Undo/Redo.
    // The marker remains: this neither acknowledges Scene/history nor permits
    // another writer. Released reports are revalidated under fresh ownership.
    static bool RestoreBeforeReplay(const Base::ProjectContext& project,const std::string& guid,
        const PrefabSourceHistoryRecoveryReport& expected,std::string& error,const std::function<void()>& afterPark={}) {
        PrefabSourceHistoryRecoveryReport unused;
        return InspectHeld(project,guid,unused,error,true,
            [&](const PrefabSourceHistoryRecoveryReport& current,const PrefabSourceRecovery::Observed& source,
                const PrefabSourceRecovery::Observed& backup,const PrefabSourceRecovery::Observed& stage,const std::function<void()>& revalidate,
                const std::function<void(const std::filesystem::path&)>&){
                if(!Same(current,expected) || current.source.state==PrefabSourceRecoveryState::Conflict)
                    throw std::runtime_error("History recovery state changed/conflicts; inspect again before restoring");
                const auto root=std::filesystem::canonical(project.ResolvePath(project.GetSettings().assetDirectory));
                if(std::filesystem::exists(root/".kubo/asset-move.pending.json"))throw std::runtime_error("Recover pending asset move before restoring source history");
                revalidate();
                const bool original=!current.undo;
                const auto target=original?PrefabSourceRecoveryState::Prepared:PrefabSourceRecoveryState::Published;
                if(current.source.state==target)return;
                const auto* desired=original?backup.file:stage.file;
                if(current.source.state==PrefabSourceRecoveryState::Parked){PrefabSourcePublication::Rename(desired->handle,current.source.source);return;}
                PrefabSourcePublication::Rename(source.file->handle,current.source.directory/(original?"next.prefab":"original.prefab"));
                try {
                    if(afterPark)afterPark();
                    PrefabSourcePublication::Rename(desired->handle,current.source.source);
                }catch(const std::exception& exception){
                    std::string detail=exception.what();
                    try {PrefabSourcePublication::Rename(source.file->handle,current.source.source);}
                    catch(const std::exception&){detail+="; source pair and history marker retained because rollback destination is occupied/inaccessible";}
                    throw std::runtime_error(detail);
                }
            });
    }
    // Source-only completion after explicit rollback. Archive the entire marker
    // directory, retaining unknown files. The caller coordinates Scene/history.
    static bool ArchiveRestored(const Base::ProjectContext& project,const std::string& guid,
        const PrefabSourceHistoryRecoveryReport& expected,std::filesystem::path& archived,std::string& error,
        const std::function<void(const std::filesystem::path&)>& beforeArchive={}) {
        PrefabSourceHistoryRecoveryReport unused;std::filesystem::path destination;
        const bool success=InspectHeld(project,guid,unused,error,false,
            [&](const PrefabSourceHistoryRecoveryReport& current,const PrefabSourceRecovery::Observed&,const PrefabSourceRecovery::Observed&,
                const PrefabSourceRecovery::Observed&,const std::function<void()>& revalidate,const std::function<void(const std::filesystem::path&)>& archivePending){
                if(!Same(current,expected) || current.source.state!=(current.undo?PrefabSourceRecoveryState::Published:PrefabSourceRecoveryState::Prepared))
                    throw std::runtime_error("Only a revalidated pre-replay source can complete history recovery");
                const auto root=std::filesystem::canonical(project.ResolvePath(project.GetSettings().assetDirectory));
                if(std::filesystem::exists(root/".kubo/asset-move.pending.json"))throw std::runtime_error("Recover pending asset move before completing source history");
                static std::atomic<unsigned long long> sequence{0};
                destination=current.source.directory/("history-recovery-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64())+"-"+std::to_string(++sequence));
                if(beforeArchive)beforeArchive(destination);
                revalidate();archivePending(destination);
            });
        if(success)archived=std::move(destination);return success;
    }
private:
    using Action=std::function<void(const PrefabSourceHistoryRecoveryReport&,const PrefabSourceRecovery::Observed&,
        const PrefabSourceRecovery::Observed&,const PrefabSourceRecovery::Observed&,const std::function<void()>&,
        const std::function<void(const std::filesystem::path&)>&)>;
    static bool Same(const PrefabSourceHistoryRecoveryReport& a,const PrefabSourceHistoryRecoveryReport& b){
        return a.undo==b.undo && a.pending==b.pending && a.markerBytes==b.markerBytes && a.markerIdentity==b.markerIdentity && PrefabSourceRecovery::Same(a.source,b.source);
    }
    static bool InspectHeld(const Base::ProjectContext& project,const std::string& guid,
        PrefabSourceHistoryRecoveryReport& output,std::string& error,bool ownDelete,const Action& action) {
        static_assert(std::is_nothrow_move_assignable_v<PrefabSourceHistoryRecoveryReport>);
        try {
            if(!project.IsOpen() || !Scene::ModelComponent::IsValidGuid(guid))throw std::runtime_error("History recovery requires an open project and valid GUID");
            const auto root=std::filesystem::canonical(project.ResolvePath(project.GetSettings().assetDirectory));
            Base::ProjectContext freshProject;if(!freshProject.Open(project.GetProjectFile(),error))return false;
            if(std::filesystem::canonical(freshProject.ResolvePath(freshProject.GetSettings().assetDirectory))!=root)
                throw std::runtime_error("History recovery asset root changed in project manifest");
            if(!ProjectPrefabDetail::Within(root,std::filesystem::canonical(project.GetRootPath())))throw std::runtime_error("History asset root escapes project");
            const auto pending=root/".kubo/prefab-transactions"/guid;
            // Hold the pending namespace/marker before trusting its archive path.
            // DELETE access denies namespace replacement but inspection does not
            // rename/delete or create anything. A live replay's ownership refuses it.
            Directory pendingDirectory(pending,root);
            std::vector<std::unique_ptr<NativeFile>> held;
            const auto marker=PrefabSourceRecovery::Observe(pending/"history.json",root,24*1024*1024,held);
            if(!marker.exists)throw std::runtime_error("Incomplete history transaction has no marker; preserve pending data");
            const auto document=HeldProjectPrefabRevision::ParseHeldJson(marker.bytes);
            if(!document.is_object() || document.get<crude_json::object>().size()!=5)throw std::runtime_error("Unsupported history recovery marker fields");
            for(const auto* key:{"format","guid","archiveRelative"})
                if(!document.contains(key) || !document[key].is_string())throw std::runtime_error("Invalid history marker text fields");
            if(!document.contains("undo") || !document["undo"].is_boolean() || !document.contains("intent") || !document["intent"].is_object() ||
               document["format"].get<crude_json::string>()!="kubo-prefab-source-history-1" || document["guid"].get<crude_json::string>()!=guid)
                throw std::runtime_error("History marker format/GUID/direction mismatch");
            const auto archiveText=document["archiveRelative"].get<crude_json::string>();
            if(archiveText.find('\0')!=std::string::npos || archiveText.find(':')!=std::string::npos)
                throw std::runtime_error("Invalid history archive path text");
            const auto relative=Base::PathFromUtf8(archiveText);
            if(relative.empty() || relative.is_absolute() || relative.has_root_name())throw std::runtime_error("History archive must be asset-relative");
            for(const auto& part:relative){const auto name=part.native();
                if(part==L"." || part==L".." || (!name.empty() && (name.back()==L'.' || name.back()==L' ')))
                    throw std::runtime_error("History archive traversal/ambiguous component refused");
            }
            const auto archive=root/relative;
            PrefabSourceHistoryRecoveryReport candidate;candidate.undo=document["undo"].get<crude_json::boolean>();candidate.pending=pending;
            candidate.markerBytes=marker.bytes;candidate.markerIdentity=marker.identity;
            std::unique_ptr<Directory> archiveDirectory;
            const bool success=PrefabSourceRecovery::InspectHeld(freshProject,guid,candidate.source,error,ownDelete,
                [&](const PrefabSourceRecoveryReport& current,const PrefabSourceRecovery::Observed& source,const PrefabSourceRecovery::Observed& backup,
                    const PrefabSourceRecovery::Observed& stage,const std::function<void()>& revalidate,const std::function<void()>&){
                    PrefabValue intent;intent["format"]="kubo-prefab-source-publication-1";intent["guid"]=current.guid;
                    intent["sourceRelative"]=Base::PathToUtf8(current.source.lexically_relative(root));intent["originalBytes"]=current.originalBytes;
                    intent["nextBytes"]=current.nextBytes;intent["metadataBytes"]=current.metadataBytes;
                    intent["originalIdentity"]=PrefabSourcePublication::Identity(current.originalIdentity);intent["nextIdentity"]=PrefabSourcePublication::Identity(current.nextIdentity);
                    intent["metadataIdentity"]=PrefabSourcePublication::Identity(current.metadataIdentity);
                    if(document["intent"].dump()!=intent.dump())throw std::runtime_error("History marker intent differs from archived journal");
                    Assets::AssetRegistry registry;std::string diagnostic;
                    if(!registry.Scan(root,diagnostic,Assets::AssetRegistry::MetadataPolicy::ReadOnly))throw std::runtime_error(diagnostic);
                    const auto* record=registry.FindByGuid(guid);
                    // A parked source need not be indexed. Any other asset with
                    // this GUID still makes recovery ambiguous, even if alone.
                    if(record && (record->type!=Assets::AssetType::Prefab || std::filesystem::canonical(Base::PathFromUtf8(record->absolutePath))!=current.source))
                        throw std::runtime_error("History source GUID is ambiguous or has moved");
                    revalidate();PrefabSourceRecovery::Revalidate(marker,root);
                    if(action){
                        candidate.source=current;
                        // A DELETE-owned directory with read-only sharing also
                        // blocks adding our parked source into that directory.
                        // Children are now held (including the immutable journal),
                        // so release only this extra namespace-validation handle;
                        // keep marker/pending directory, writer lock and all file
                        // ownership throughout source restoration. Do not relax
                        // native source/metadata sharing to permit the rename.
                        archiveDirectory.reset();
                        action(candidate,source,backup,stage,[&]{revalidate();PrefabSourceRecovery::Revalidate(marker,root);},
                            [&](const std::filesystem::path& destination){
                                revalidate();PrefabSourceRecovery::Revalidate(marker,root);
                                // Windows requires the pending directory's child
                                // readers to close before its rename. Keep source,
                                // metadata, archive journal/pair and GUID lock held.
                                held.clear();PrefabSourcePublication::Rename(pendingDirectory.handle,destination);
                            });
                    }
                },[&](const std::filesystem::path& directory,const std::filesystem::path&){
                    archiveDirectory=std::make_unique<Directory>(directory,root);PrefabSourceRecovery::Revalidate(marker,root);
                },archive,nullptr,pending);
            if(!success)return false;
            output=std::move(candidate);error.clear();return true;
        }catch(const std::exception& exception){error=exception.what();return false;}
    }
};
}
