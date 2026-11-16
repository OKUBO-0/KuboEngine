#pragma once
#include "AnimatorControllerEdits.h"
#include "ControllerDocumentEpoch.h"
#include "AnimatorControllerAcknowledgement.h"
#include "AnimatorControllerCreationRecovery.h"
#include <type_traits>
#include "../assets/AnimatorControllerAsset.h"

namespace Engine::Editor {
// Detached source baseline + private document; synchronous main-thread saving.
// No automatic World reload or GUI ownership. Borrowed pointers expire on switch.
class AnimatorControllerSession final {
    std::unique_ptr<AnimatorControllerDocument> document_;
    std::shared_ptr<const Assets::AnimatorControllerRevision> source_;
    std::unique_ptr<ControllerRecoveryReport> pending_;
    std::unique_ptr<ControllerCreationReport> creationPending_;
    std::filesystem::path lastArchive_;
    uint64_t guiOwnerEpoch_=0;
    bool MayReplace(bool discardApproved,std::string& error) const {
        if(HasPendingAcknowledgement()){error="Controller source is saved but acknowledgement is pending; retry acknowledgement before switching";return false;}
        if(document_ && document_->IsDirty() && !discardApproved){error="Controller has unsaved edits; save, discard or cancel first";return false;}
        return true;
    }
public:
    enum class SaveOutcome { Failed, Saved, SavedPendingAcknowledgement };
    uint64_t GuiOwnerEpoch() const noexcept{return guiOwnerEpoch_;}
    bool HasPendingAcknowledgement() const noexcept{return pending_!=nullptr || creationPending_!=nullptr;}
    bool HasPendingCreation() const noexcept{return creationPending_!=nullptr;}
    const std::filesystem::path& LastArchive() const noexcept{return lastArchive_;}
    AnimatorControllerDocument* Document() noexcept{return document_.get();}
    const AnimatorControllerDocument* Document() const noexcept{return document_.get();}
    const Assets::AnimatorControllerRevision* Source() const noexcept{return source_.get();}
    bool AdoptCreated(const Base::ProjectContext& project,Assets::AssetRegistry& registry,std::string& error,const std::function<void(const std::filesystem::path&)>& beforeArchive={}) {
        try{
            if(!creationPending_){error.clear();return true;}
            if(!source_){error="Pending created Controller baseline missing";return false;}
            auto candidate=registry;
            const auto scan=[&]{
                const auto root=std::filesystem::canonical(project.ResolvePath(project.GetSettings().assetDirectory));
                if(!registry.GetRoot().empty() && registry.GetRoot()!=root)throw std::runtime_error("Created Controller registry belongs to another asset root");
                if(!candidate.Scan(project.ResolvePath(project.GetSettings().assetDirectory),error,Assets::AssetRegistry::MetadataPolicy::ReadOnly))throw std::runtime_error(error);
                // Retain existing import fingerprints for stat-unchanged assets.
                // This is field preservation, not new content-hash validation;
                // downstream import/dependency validation remains required.
                for(const auto& old:registry.GetAssets())if(const auto* current=candidate.FindByGuid(old.guid);current && current->fingerprint==old.fingerprint && !old.importFingerprint.empty())candidate.SetImportFingerprint(old.guid,old.importFingerprint);
            };
            scan();Assets::HeldAnimatorControllerAsset lease;if(!lease.OpenForRecovery(project,candidate,source_->guid,*source_,error))return false;
            std::filesystem::path archive;
            if(!AnimatorControllerCreationRecovery::ArchiveResolved(project,source_->guid,*creationPending_,archive,error,[&](const std::filesystem::path& destination){if(beforeArchive)beforeArchive(destination);scan();}))return false;
            // All allocations and filesystem admission precede this commit.
            // Borrowed registry records expire; registry object identity stays.
            static_assert(std::is_nothrow_move_assignable_v<Assets::AssetRegistry>);
            registry=std::move(candidate);lastArchive_.swap(archive);creationPending_.reset();error.clear();return true;
        }catch(const std::exception& exception){error=exception.what();return false;}
    }
    SaveOutcome SaveAs(const Base::ProjectContext& project,Assets::AssetRegistry& registry,const std::string& assetRelative,std::string& error,const std::function<void(const std::filesystem::path&)>& beforeArchive={}) {
        try{
            if(!document_){error="Create/open a Controller draft first";return SaveOutcome::Failed;}
            if(HasPendingAcknowledgement()){error="Resolve pending Controller acknowledgement before Save As";return SaveOutcome::SavedPendingAcknowledgement;}
            const auto root=std::filesystem::canonical(project.ResolvePath(project.GetSettings().assetDirectory));
            if(!registry.GetRoot().empty() && registry.GetRoot()!=root){error="Save As registry belongs to another asset root";return SaveOutcome::Failed;}
            if(source_ && !AnimatorControllerPublication::Within(source_->source,root)){error="Save As cannot switch the Controller's owning project";return SaveOutcome::Failed;}
            // Save As copies the current private draft, never overwrites or
            // silently reloads an externally modified original source.
            AnimatorControllerDocument::PreparedSave receipt;if(!document_->PrepareSave(receipt,error))return SaveOutcome::Failed;
            {
                AnimatorControllerCreation creation;if(!creation.Prepare(project,assetRelative,receipt.Text(),error))return SaveOutcome::Failed;
                auto source=creation.next_;auto report=std::make_unique<ControllerCreationReport>();report->state=ControllerCreationState::Published;report->source=source->source;report->directory=creation.directory_;report->intent=creation.journalBytes_;
                if(!creation.Publish(error))return SaveOutcome::Failed;
                if(!document_->CommitSaved(receipt)){error="Controller created but receipt stale; recover retained creation";return SaveOutcome::Failed;}
                source_.swap(source);creationPending_.swap(report);
            }
            return AdoptCreated(project,registry,error,beforeArchive)?SaveOutcome::Saved:SaveOutcome::SavedPendingAcknowledgement;
        }catch(const std::exception& exception){error=exception.what();return creationPending_?SaveOutcome::SavedPendingAcknowledgement:SaveOutcome::Failed;}
    }
    bool AcknowledgePending(const Base::ProjectContext& project,const Assets::AssetRegistry& registry,std::string& error,const std::function<void(const std::filesystem::path&)>& beforeArchive={}) {
        if(creationPending_){error="Created Controller needs AdoptCreated with its mutable registry";return false;}
        if(!pending_){error.clear();return true;}
        if(!source_){error="Pending Controller source baseline missing";return false;}
        // Keep the actual registered source/meta lease while acknowledging;
        // registry conflicts and changed source baselines must not be hidden.
        Assets::HeldAnimatorControllerAsset lease;if(!lease.OpenForRecovery(project,registry,source_->guid,*source_,error))return false;
        std::filesystem::path archive;
        if(!AnimatorControllerAcknowledgement::ArchiveResolved(project,source_->guid,*pending_,archive,error,beforeArchive))return false;
        lastArchive_.swap(archive);pending_.reset();error.clear();return true;
    }
    SaveOutcome Save(const Base::ProjectContext& project,const Assets::AssetRegistry& registry,std::string& error,const std::function<void(const std::filesystem::path&)>& beforeArchive={}) {
        try{
            if(!document_ || !source_){error="Untitled Controller requires Save As/asset registration";return SaveOutcome::Failed;}
            if(creationPending_){error="Created Controller registration/acknowledgement pending; retry AdoptCreated";return SaveOutcome::SavedPendingAcknowledgement;}
            if(pending_ && !AcknowledgePending(project,registry,error,beforeArchive))return SaveOutcome::SavedPendingAcknowledgement;
            const auto transaction=project.ResolvePath(project.GetSettings().assetDirectory)/".kubo/controller-transactions"/source_->guid;
            if(std::filesystem::exists(transaction) || std::filesystem::exists(project.ResolvePath(project.GetSettings().assetDirectory)/".kubo/controller-creations"/source_->guid)){error="Recover retained Controller transaction before Save";return SaveOutcome::Failed;}
            if(!document_->IsDirty())return Revalidate(project,registry,error)?SaveOutcome::Saved:SaveOutcome::Failed;
            AnimatorControllerDocument::PreparedSave receipt;if(!document_->PrepareSave(receipt,error))return SaveOutcome::Failed;
            {
                AnimatorControllerPublication publication;
                if(!publication.Prepare(project,registry,*source_,receipt.Text(),error))return SaveOutcome::Failed;
                // Prepare every allocating member before the first source rename.
                auto source=publication.next_;auto pending=std::make_unique<ControllerRecoveryReport>();
                pending->state=ControllerRecoveryState::Published;pending->source=source_->source;pending->directory=publication.directory_;pending->intent=publication.journalBytes_;
                if(!publication.Publish(error))return SaveOutcome::Failed;
                // No callbacks/edit commands occur between receipt preparation
                // and this main-thread commit. History remains asset-local.
                if(!document_->CommitSaved(receipt)){error="Controller source published but draft receipt stale; recover retained transaction";return SaveOutcome::Failed;}
                source_.swap(source);pending_.swap(pending);
            } // Release publication handles before acquiring acknowledgement.
            return AcknowledgePending(project,registry,error,beforeArchive)?SaveOutcome::Saved:SaveOutcome::SavedPendingAcknowledgement;
        }catch(const std::exception& exception){error=exception.what();return pending_?SaveOutcome::SavedPendingAcknowledgement:SaveOutcome::Failed;}
    }
    bool Open(const Base::ProjectContext& project,const Assets::AssetRegistry& registry,const std::string& guid,
        std::string& error,bool discardApproved=false,const Assets::AnimatorControllerRevision* expected=nullptr) {
        try {
            if(!MayReplace(discardApproved,error))return false;
            const auto nextEpoch=ControllerDocumentEpoch::Next(guiOwnerEpoch_);
            Assets::HeldAnimatorControllerAsset lease;if(!lease.Open(project,registry,guid,error,expected))return false;
            std::shared_ptr<const Assets::AnimatorControllerRevision> source=std::make_shared<const Assets::AnimatorControllerRevision>(*lease.Snapshot());
            auto document=std::make_unique<AnimatorControllerDocument>();if(!document->Load(source->bytes,error))return false;
            source_.swap(source);document_.swap(document);guiOwnerEpoch_=nextEpoch;error.clear();return true;
        }catch(const std::exception& exception){error=exception.what();return false;}
    }
    bool New(std::string& error,bool discardApproved=false) {
        try {
            if(!MayReplace(discardApproved,error))return false;
            const auto nextEpoch=ControllerDocumentEpoch::Next(guiOwnerEpoch_);
            auto document=std::make_unique<AnimatorControllerDocument>();if(!AnimatorControllerEdits(*document).New(error))return false;
            document_.swap(document);source_.reset();guiOwnerEpoch_=nextEpoch;error.clear();return true;
        }catch(const std::exception& exception){error=exception.what();return false;}
    }
    // Detached recovered bytes never inherit a source GUID/baseline. A later
    // SaveAs uses the normal new-GUID publication/adoption/acknowledgement path.
    bool NewFromText(std::string_view text,std::string& error,bool discardApproved=false) {
        try {
            if(!MayReplace(discardApproved,error))return false;
            const auto nextEpoch=ControllerDocumentEpoch::Next(guiOwnerEpoch_);
            auto document=std::make_unique<AnimatorControllerDocument>();
            if(!document->Load(text,error,true))return false;
            document_.swap(document);source_.reset();guiOwnerEpoch_=nextEpoch;error.clear();return true;
        }catch(const std::exception& exception){error=exception.what();return false;}
    }
    bool Close(std::string& error,bool discardApproved=false) {
        if(!MayReplace(discardApproved,error))return false;
        document_.reset();source_.reset();error.clear();return true;
    }
    // Observation for a future writer, not a lock held until publication.
    bool Revalidate(const Base::ProjectContext& project,const Assets::AssetRegistry& registry,std::string& error) const {
        if(!source_){error="Untitled Controller has no registered source baseline";return false;}
        Assets::HeldAnimatorControllerAsset lease;return HasPendingAcknowledgement()?lease.OpenForRecovery(project,registry,source_->guid,*source_,error):lease.Open(project,registry,source_->guid,error,source_.get());
    }
};
}
