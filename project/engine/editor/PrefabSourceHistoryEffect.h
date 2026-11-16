#pragma once
#include "EntityHistoryEffect.h"
#include "PrefabSourceAcknowledgement.h"

namespace Engine::Editor {
// Concrete archived source-pair effect. Initial Apply/history admission and
// startup marker recovery/UI are separate callers, not implemented here.
class PrefabSourceHistoryEffect final:public EntityHistoryEffect {
    using NativeFile=HeldProjectPrefabRevision::NativeFile;
    using Directory=PrefabSourceAcknowledgement::Directory;
    std::filesystem::path manifest_,pending_;
    PrefabSourceRecoveryReport receipt_;
    PrefabSourceRecovery::Ownership ownership_;
    std::unique_ptr<NativeFile> marker_;std::unique_ptr<Directory> pendingDirectory_;
    bool resolved_=false,retainedPending_=false;
    std::function<void()> afterPark_;
    static size_t Cost(const Base::ProjectContext& project,const PrefabSourceRecoveryReport& receipt){
        return sizeof(PrefabSourceHistoryEffect)+receipt.originalBytes.size()+receipt.nextBytes.size()+receipt.metadataBytes.size()+receipt.guid.size()+
            (project.GetProjectFile().native().size()+receipt.source.native().size()+receipt.directory.native().size()+4096)*sizeof(wchar_t);
    }
public:
    PrefabSourceHistoryEffect(const Base::ProjectContext& project,PrefabSourceRecoveryReport archivedReceipt,const std::function<void()>& afterPark={}):
        EntityHistoryEffect(Cost(project,archivedReceipt)),manifest_(project.GetProjectFile()),receipt_(std::move(archivedReceipt)),afterPark_(afterPark){}
    ~PrefabSourceHistoryEffect() override{End();}
    bool HasRetainedPending() const noexcept{return retainedPending_;}
    const std::filesystem::path& PendingPath() const noexcept{return pending_;}
    bool Begin(bool undo,std::string& error) override{
        try {
        if(ownership_.lock || marker_ || pendingDirectory_){error="Source history effect is already owned";return false;}
        resolved_=false;retainedPending_=false;
        Base::ProjectContext project;if(!project.Open(manifest_,error))return false;
        const auto root=std::filesystem::canonical(project.ResolvePath(project.GetSettings().assetDirectory));
        pending_=root/".kubo/prefab-transactions"/receipt_.guid;
        auto expected=receipt_;expected.state=undo?PrefabSourceRecoveryState::Published:PrefabSourceRecoveryState::Prepared;
        PrefabSourceRecoveryReport unused;
        const bool success=PrefabSourceRecovery::InspectHeld(project,receipt_.guid,unused,error,true,
            [&](const PrefabSourceRecoveryReport& current,const PrefabSourceRecovery::Observed& source,const PrefabSourceRecovery::Observed& backup,
                const PrefabSourceRecovery::Observed& stage,const std::function<void()>& revalidate,const std::function<void()>&){
                if(!PrefabSourceRecovery::Same(current,expected))throw std::runtime_error("Archived source history changed; refuse replay");
                Assets::AssetRegistry fresh;std::string diagnostic;
                if(!fresh.Scan(root,diagnostic,Assets::AssetRegistry::MetadataPolicy::ReadOnly))throw std::runtime_error(diagnostic);
                const auto* record=fresh.FindByGuid(current.guid);
                if(!record || record->type!=Assets::AssetType::Prefab || std::filesystem::canonical(Base::PathFromUtf8(record->absolutePath))!=current.source)
                    throw std::runtime_error("Source history GUID/path is stale or ambiguous");
                const auto parent=pending_.parent_path();
                if(!ProjectPrefabDetail::Within(std::filesystem::canonical(parent),root) || (GetFileAttributesW(parent.c_str())&FILE_ATTRIBUTE_REPARSE_POINT))
                    throw std::runtime_error("Unsafe history pending parent");
                if(!std::filesystem::create_directory(pending_))throw std::runtime_error("History pending directory occupied");
                pendingDirectory_=std::make_unique<Directory>(pending_,root);resolved_=true;retainedPending_=true;
                PrefabValue intent;intent["format"]="kubo-prefab-source-publication-1";intent["guid"]=current.guid;
                intent["sourceRelative"]=Base::PathToUtf8(current.source.lexically_relative(root));intent["originalBytes"]=current.originalBytes;intent["nextBytes"]=current.nextBytes;intent["metadataBytes"]=current.metadataBytes;
                intent["originalIdentity"]=PrefabSourcePublication::Identity(current.originalIdentity);intent["nextIdentity"]=PrefabSourcePublication::Identity(current.nextIdentity);intent["metadataIdentity"]=PrefabSourcePublication::Identity(current.metadataIdentity);
                PrefabValue marker;marker["format"]="kubo-prefab-source-history-1";marker["guid"]=current.guid;marker["archiveRelative"]=Base::PathToUtf8(current.directory.lexically_relative(root));marker["undo"]=undo;marker["intent"]=std::move(intent);
                const auto bytes=marker.dump();if(bytes.size()>24*1024*1024)throw std::runtime_error("Source history marker exceeds budget");
                marker_=std::make_unique<NativeFile>(pending_/"history.json",GENERIC_READ|GENERIC_WRITE|DELETE,CREATE_NEW);
                std::string empty;PrefabFileIdentity id;std::filesystem::path final;
                HeldProjectPrefabRevision::Read(*marker_,root,0,empty,id,final);
                if(final!=std::filesystem::canonical(pending_/"history.json"))throw std::runtime_error("History marker path changed");
                PrefabSourcePublication::Write(marker_->handle,bytes);revalidate();
                if(std::filesystem::exists(root/".kubo/asset-move.pending.json"))throw std::runtime_error("Recover pending asset move before source history replay");
                PrefabSourcePublication::Rename(source.file->handle,current.directory/(undo?"next.prefab":"original.prefab"));resolved_=false;
                try {
                    if(afterPark_)afterPark_();
                    PrefabSourcePublication::Rename((undo?backup.file:stage.file)->handle,current.source);resolved_=true;
                }catch(const std::exception& exception){
                    std::string detail=exception.what();
                    try {PrefabSourcePublication::Rename(source.file->handle,current.source);resolved_=true;}
                    catch(const std::exception&){detail+="; source pair and history pending retained; explicit recovery required";}
                    throw std::runtime_error(detail);
                }
            },{},receipt_.directory,&ownership_);
        if(!success)End();return success;
        }catch(const std::exception& exception){error=exception.what();End();return false;}
    }
    void End() noexcept override{
        // Delete only this held marker and the now-empty held directory. Unknown
        // files make directory disposition fail and remain protected as pending.
        if(resolved_ && pendingDirectory_){
            bool removed=true;FILE_DISPOSITION_INFO disposition{TRUE};
            if(marker_)removed=SetFileInformationByHandle(marker_->handle,FileDispositionInfo,&disposition,sizeof(disposition))!=0;
            marker_.reset();
            if(removed)removed=SetFileInformationByHandle(pendingDirectory_->handle,FileDispositionInfo,&disposition,sizeof(disposition))!=0;
            retainedPending_=!removed;
        }
        marker_.reset();pendingDirectory_.reset();ownership_.Release();resolved_=false;
    }
};
}
