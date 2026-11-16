#include "Game.h"
#include "engine/editor/ControllerRecordSalvage.h"
#if defined(KUBO_WITH_EDITOR) || defined(_DEBUG)
#include <imgui.h>
#endif
namespace Engine::Scene {
bool Game::CanRecoverController(std::string& error) const {
    if(!editorMode_ || worldPlayback_.GetState()!=WorldPlayback::State::Editing || documentState_.HasPendingAction() || controllerClosePending_ || prefabRecoveryBlocked_ || gizmoDragActive_ || assetImportJob_.IsBusy() || exportJob_.GetState()==Engine::Services::ExportJob::State::Running || !controllerInventoryError_.empty()){error="Controller recovery requires idle Editing and valid inventory";return false;}
    if(controllerEditor_.NeedsResolution()){error="Resolve the live Controller draft with Save/retry or Discard before recovery";return false;}
    if(controllerRecoverySelection_ && controllerEditor_.Session().Source() && controllerEditor_.Session().Source()->guid==controllerRecoverySelection_->guid){error="Close the affected Controller document before filesystem recovery";return false;}
    if(controllerRecordSelection_ && controllerEditor_.Session().Source() && controllerEditor_.Session().Source()->guid==controllerRecordSelection_->guid){error="Close the affected Controller document before partial-record recovery";return false;}
    return true;
}
bool Game::InspectControllerRecovery(Engine::Editor::ControllerPendingFamily family,const std::string& guid,std::string& error) {
    controllerRecoverySelection_.reset();controllerRecordSelection_.reset();controllerRecordConfirmSelection_.reset();controllerRecordConfirmAction_=0;controllerRecoveryConfirm_=false;
    try{
        if(family!=Engine::Editor::ControllerPendingFamily::Publication && family!=Engine::Editor::ControllerPendingFamily::Creation){error="Unknown Controller recovery family";return false;}
        if(!editorMode_ || worldPlayback_.GetState()!=WorldPlayback::State::Editing || assetImportJob_.IsBusy() || exportJob_.GetState()==Engine::Services::ExportJob::State::Running){error="Inspect requires idle Editing";return false;}
        Engine::Base::ProjectContext project;if(!project.Open(projectFile_,error))return false;
        if(project.GetRootPath()!=GetProjectRoot()){error="Controller recovery project root changed";return false;}
        auto selection=std::make_unique<ControllerRecoverySelection>();selection->family=family;selection->guid=guid;
        if(family==Engine::Editor::ControllerPendingFamily::Publication){
            if(!Engine::Editor::AnimatorControllerRecovery::Inspect(project,guid,selection->publication,error))return false;
            const char* names[]={"Prepared","Parked","Published","Conflict"};controllerInspectStatus_=std::string(names[static_cast<size_t>(selection->publication.state)])+" source="+Engine::Base::PathToUtf8(selection->publication.source);
        }else{
            if(!Engine::Editor::AnimatorControllerCreationRecovery::Inspect(project,guid,selection->creation,error))return false;
            const char* names[]={"Prepared","Metadata only","Published","Conflict"};controllerInspectStatus_=std::string(names[static_cast<size_t>(selection->creation.state)])+" source="+Engine::Base::PathToUtf8(selection->creation.source);
        }
        controllerRecoverySelection_.swap(selection);error.clear();return true;
    }catch(const std::exception& exception){error=exception.what();return false;}
}
bool Game::RestoreControllerRecovery(std::string& error) {
    if(!controllerRecoverySelection_){error="Inspect a Controller record first";return false;}
    if(!CanRecoverController(error))return false;
    Engine::Base::ProjectContext project;if(!project.Open(projectFile_,error))return false;
    if(project.GetRootPath()!=GetProjectRoot()){error="Controller recovery project root changed";return false;}
    const auto& selected=*controllerRecoverySelection_;
    auto guid=selected.guid;const auto family=selected.family;
    const bool success=selected.family==Engine::Editor::ControllerPendingFamily::Publication?
        Engine::Editor::AnimatorControllerRecovery::RestoreOriginal(project,selected.guid,selected.publication,error):
        Engine::Editor::AnimatorControllerCreationRecovery::Restage(project,selected.guid,selected.creation,error);
    if(!success)return false;
    controllerRecoverySelection_.reset();
    return InspectControllerRecovery(family,guid,error);
}
bool Game::InspectControllerRecord(Engine::Editor::ControllerPendingFamily family,const std::string& guid,std::string& error,const std::filesystem::path& archive) {
    controllerRecoverySelection_.reset();controllerRecordSelection_.reset();controllerRecordConfirmSelection_.reset();controllerRecoveryConfirm_=false;controllerRecordConfirmAction_=0;
    try{
        if(!editorMode_ || worldPlayback_.GetState()!=WorldPlayback::State::Editing || documentState_.HasPendingAction() || controllerClosePending_ || prefabRecoveryBlocked_ || gizmoDragActive_ || assetImportJob_.IsBusy() || exportJob_.GetState()==Engine::Services::ExportJob::State::Running){error="Record diagnosis requires idle Editing";return false;}
        Engine::Base::ProjectContext project;if(!project.Open(projectFile_,error))return false;
        if(project.GetRootPath()!=GetProjectRoot()){error="Controller record project root changed";return false;}
        auto report=std::make_shared<Engine::Editor::ControllerRecordReport>();
        const bool success=archive.empty()?Engine::Editor::ControllerRecordInspection::Inspect(project,family,guid,*report,error):
            Engine::Editor::ControllerRecordInspection::InspectArchive(project,family,guid,archive,*report,error);
        if(!success)return false;
        std::string status="Owned diagnosis: "+std::to_string(report->files.size())+" files; "+(report->hasIntent?"intent present (not validity); ":"intent absent; ")+
            (report->validStage?"valid stage available for new draft":report->hasStage?"invalid stage: "+report->stageError:"stage absent");
        controllerRecordSelection_.swap(report);controllerRecordArchived_=!archive.empty();controllerInspectStatus_.swap(status);error.clear();return true;
    }catch(const std::exception& exception){error=exception.what();return false;}
}
bool Game::QuarantineControllerRecord(std::string& error) {
    if(!controllerRecordSelection_ || controllerRecordArchived_){error="Diagnose a pending partial record before quarantine";return false;}
    if(!CanRecoverController(error))return false;
    bool committed=false;
    try{
        Engine::Base::ProjectContext project;if(!project.Open(projectFile_,error))return false;
        if(project.GetRootPath()!=GetProjectRoot()){error="Controller record project root changed";return false;}
        std::string status="Record quarantined with all contents preserved. Source and Scene owners were not changed.";
        std::filesystem::path archived;
        if(!Engine::Editor::ControllerRecordInspection::Quarantine(project,*controllerRecordSelection_,archived,error))return false;
        controllerRecordSelection_.reset();controllerRecordConfirmSelection_.reset();controllerRecordConfirmAction_=0;controllerInspectStatus_.swap(status);committed=true;
        std::string refreshError;RefreshControllerInventory(refreshError);error.clear();return true;
    }catch(const std::exception& exception){if(committed){error.clear();return true;}error=exception.what();return false;}
}
bool Game::RecoverControllerDraft(std::string& error) {
    if(!controllerRecordSelection_){error="Diagnose a Controller stage before recovering a draft";return false;}
    if(!CanRecoverController(error))return false;
    try{
        Engine::Base::ProjectContext project;if(!project.Open(projectFile_,error))return false;
        if(project.GetRootPath()!=GetProjectRoot()){error="Controller record project root changed";return false;}
        std::string status="Recovered untitled draft. Save As creates a new GUID; source, pending record and Scene owners are unchanged.";
        if(!Engine::Editor::ControllerRecordSalvage::OpenDraft(project,*controllerRecordSelection_,controllerEditor_.Session(),error))return false;
        controllerEditor_.ShowPreparedDraft();controllerRecordSelection_.reset();controllerRecordConfirmSelection_.reset();controllerRecordConfirmAction_=0;controllerInspectStatus_.swap(status);error.clear();return true;
    }catch(const std::exception& exception){error=exception.what();return false;}
}
bool Game::RequestControllerRecordAction(bool quarantine,std::string& error) {
    controllerRecordConfirmAction_=0;controllerRecordConfirmSelection_.reset();
    if(!controllerRecordSelection_){error="Diagnose a Controller record before confirmation";return false;}
    if(!CanRecoverController(error))return false;
    if(quarantine && controllerRecordArchived_){error="A quarantined record cannot be quarantined again";return false;}
    if(!quarantine && !controllerRecordSelection_->validStage){error="Recover requires a valid diagnosed stage";return false;}
    controllerRecordConfirmSelection_=controllerRecordSelection_;controllerRecordConfirmAction_=quarantine?1:2;error.clear();return true;
}
bool Game::ConfirmControllerRecordAction(bool approved,std::string& error) {
    if(!approved){controllerRecordConfirmAction_=0;controllerRecordConfirmSelection_.reset();error.clear();return true;}
    if((controllerRecordConfirmAction_!=1 && controllerRecordConfirmAction_!=2) || !controllerRecordConfirmSelection_ ||
       controllerRecordConfirmSelection_!=controllerRecordSelection_){controllerRecordConfirmAction_=0;controllerRecordConfirmSelection_.reset();error="Controller confirmation is absent or selection changed; request again";return false;}
    const bool success=controllerRecordConfirmAction_==1?QuarantineControllerRecord(error):RecoverControllerDraft(error);
    if(success){controllerRecordConfirmAction_=0;controllerRecordConfirmSelection_.reset();}
    return success;
}
bool Game::CompleteControllerRecovery(std::string& error) {
    bool committed=false;
    try{
        if(!controllerRecoverySelection_){error="Inspect a Controller record first";return false;}
        if(!CanRecoverController(error))return false;
        Engine::Base::ProjectContext project;if(!project.Open(projectFile_,error))return false;
        if(project.GetRootPath()!=GetProjectRoot()){error="Controller recovery project root changed";return false;}
        auto candidate=projectAssets_;if(!candidate.Scan(project.ResolvePath(project.GetSettings().assetDirectory),error,Engine::Assets::AssetRegistry::MetadataPolicy::ReadOnly))return false;
        for(const auto& old:projectAssets_.GetAssets())if(const auto* current=candidate.FindByGuid(old.guid);current && current->fingerprint==old.fingerprint && !old.importFingerprint.empty())candidate.SetImportFingerprint(old.guid,old.importFingerprint);
        const auto& selected=*controllerRecoverySelection_;std::filesystem::path archived;
        std::string successMessage="Resolved record archived; Scene/runtime owners were not reassigned.";
        const bool success=selected.family==Engine::Editor::ControllerPendingFamily::Publication?
            Engine::Editor::AnimatorControllerAcknowledgement::ArchiveResolved(project,selected.guid,selected.publication,archived,error):
            Engine::Editor::AnimatorControllerCreationRecovery::ArchiveResolved(project,selected.guid,selected.creation,archived,error);
        if(!success)return false;
        static_assert(std::is_nothrow_move_assignable_v<Engine::Assets::AssetRegistry>);projectAssets_=std::move(candidate);controllerRecoverySelection_.reset();controllerInspectStatus_.swap(successMessage);committed=true;
        // Namespace observation failure does not undo an already committed archive.
        std::string refreshError;RefreshControllerInventory(refreshError);error.clear();return true;
    }catch(const std::exception& exception){if(committed){error.clear();return true;}error=exception.what();return false;}
}
bool Game::RefreshControllerInventory(std::string& error) {
    if(projectFile_.empty()){error.clear();return true;}
    Engine::Base::ProjectContext project;
    if(!project.Open(projectFile_,error) || project.GetRootPath()!=GetProjectRoot()){if(error.empty())error="Controller inventory project root changed";controllerInventoryError_=error;controllerRecoveryWindowOpen_=true;return false;}
    std::vector<Engine::Editor::ControllerPendingEntry> pending;
    std::vector<Engine::Editor::ControllerArchiveEntry> archives;
    if(!Engine::Editor::ControllerPendingTransactions::Scan(project,pending,error) || !Engine::Editor::ControllerPendingTransactions::ScanArchives(project,archives,error)){controllerInventoryError_=error;controllerRecoveryWindowOpen_=true;return false;}
    controllerPendingEntries_.swap(pending);controllerArchiveEntries_.swap(archives);
    controllerInventoryError_.clear();if(!controllerPendingEntries_.empty() || !controllerArchiveEntries_.empty())controllerRecoveryWindowOpen_=true;return true;
}
void Game::DrawControllerRecoveryWindow() {
#if defined(KUBO_WITH_EDITOR) || defined(_DEBUG)
    if(!controllerRecoveryWindowOpen_)return;
    if(ImGui::Begin("Controller Recovery",&controllerRecoveryWindowOpen_)){
        ++controllerRecoveryFrames_;
        ImGui::TextWrapped("Startup never changes files automatically. Inspect, then explicitly restore/re-stage or archive a resolved record. No files are deleted. Close the affected document first; live saved-pending drafts should use Retry Save.");
        if(!controllerInventoryError_.empty())ImGui::TextWrapped("%s",controllerInventoryError_.c_str());
        ImGui::BeginDisabled(!controllerInventoryError_.empty());
        for(size_t i=0;i<controllerPendingEntries_.size();++i){
            const auto& entry=controllerPendingEntries_[i];ImGui::PushID(static_cast<int>(i));
            ImGui::TextUnformatted(entry.family==Engine::Editor::ControllerPendingFamily::Publication?"Source publication":"New asset creation");ImGui::SameLine();ImGui::TextUnformatted(entry.name.c_str());
            if(entry.kind!=Engine::Editor::ControllerPendingKind::Intent)ImGui::TextUnformatted("Incomplete/unknown record; preserve files for explicit review.");
            ImGui::BeginDisabled(entry.kind!=Engine::Editor::ControllerPendingKind::Intent || worldPlayback_.GetState()!=WorldPlayback::State::Editing || assetImportJob_.IsBusy() || exportJob_.GetState()==Engine::Services::ExportJob::State::Running);
            if(ImGui::Button("Inspect")){
                std::string error;if(!InspectControllerRecovery(entry.family,entry.guid,error))controllerInspectStatus_=error;
            }
            ImGui::EndDisabled();ImGui::PopID();
            ImGui::PushID(static_cast<int>(i));ImGui::BeginDisabled(entry.guid.empty());
            if(ImGui::Button("Diagnose preserved record")){std::string error;if(!InspectControllerRecord(entry.family,entry.guid,error))controllerInspectStatus_=error;}
            ImGui::EndDisabled();ImGui::PopID();
        }
        ImGui::EndDisabled();
        if(!controllerArchiveEntries_.empty()){
            ImGui::Separator();ImGui::TextUnformatted("Preserved quarantine records");
            ImGui::TextWrapped("Names identify candidates only, not validated contents. Quarantine records are retained and do not block normal export. Recovery actions require a separate owned diagnosis.");
            for(size_t i=0;i<controllerArchiveEntries_.size();++i){
                const auto& entry=controllerArchiveEntries_[i];
                ImGui::TextUnformatted(entry.name.c_str());
                if(!entry.diagnosable)ImGui::TextUnformatted("Unknown record: preserve for manual review.");
                ImGui::PushID(static_cast<int>(i)+1000);ImGui::BeginDisabled(!entry.diagnosable || !controllerInventoryError_.empty());
                if(ImGui::Button("Diagnose quarantine stage")){std::string error;if(!InspectControllerRecord(entry.family,entry.guid,error,entry.directory))controllerInspectStatus_=error;}
                ImGui::EndDisabled();ImGui::PopID();
            }
        }
        if(!controllerInspectStatus_.empty())ImGui::TextWrapped("%s",controllerInspectStatus_.c_str());
        std::string guard;ImGui::BeginDisabled(!controllerRecoverySelection_ || !CanRecoverController(guard));
        if(ImGui::Button("Restore original / re-stage created pair")){controllerRecoveryConfirm_=true;controllerRecoveryConfirmArchive_=false;}
        ImGui::SameLine();if(ImGui::Button("Archive resolved record")){controllerRecoveryConfirm_=true;controllerRecoveryConfirmArchive_=true;}
        ImGui::EndDisabled();if(!guard.empty())ImGui::TextWrapped("%s",guard.c_str());
        std::string partialGuard;ImGui::BeginDisabled(!controllerRecordSelection_ || !CanRecoverController(partialGuard));
        ImGui::BeginDisabled(controllerRecordArchived_);
        if(ImGui::Button("Quarantine partial record")){std::string error;if(!RequestControllerRecordAction(true,error))controllerInspectStatus_=error;}
        ImGui::EndDisabled();ImGui::SameLine();
        ImGui::BeginDisabled(!controllerRecordSelection_ || !controllerRecordSelection_->validStage);
        if(ImGui::Button("Recover stage as new draft")){std::string error;if(!RequestControllerRecordAction(false,error))controllerInspectStatus_=error;}
        ImGui::EndDisabled();ImGui::EndDisabled();
        if(!partialGuard.empty())ImGui::TextWrapped("%s",partialGuard.c_str());
        if(controllerRecordConfirmAction_)ImGui::OpenPopup("Confirm partial Controller recovery");
        if(ImGui::BeginPopupModal("Confirm partial Controller recovery",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
            if(!controllerRecordConfirmAction_ || !controllerRecordConfirmSelection_ || controllerRecordConfirmSelection_!=controllerRecordSelection_){ImGui::CloseCurrentPopup();ImGui::EndPopup();}
            else{
            ImGui::TextWrapped(controllerRecordConfirmAction_==1?"Move this revalidated incomplete record to quarantine, preserving every file? No source or Scene references are changed.":"Prepare an untitled Controller draft from this revalidated stage? Save As will create a new GUID. Original assets and Scene references remain unchanged.");
            if(ImGui::Button("Confirm")){std::string error;if(ConfirmControllerRecordAction(true,error)){ImGui::CloseCurrentPopup();}else controllerInspectStatus_=error;}
            ImGui::SameLine();if(ImGui::Button("Cancel")){std::string error;ConfirmControllerRecordAction(false,error);ImGui::CloseCurrentPopup();}
            ImGui::EndPopup();
            }
        }
        if(controllerRecoveryConfirm_)ImGui::OpenPopup("Confirm Controller recovery");
        if(ImGui::BeginPopupModal("Confirm Controller recovery",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
            ImGui::TextWrapped(controllerRecoveryConfirmArchive_?"Archive this revalidated Prepared/Published record, preserving all contents? This does not change Scene references or automatically reload runtime owners.":"Restore the original source or move the created pair back into staging? Pending records remain until separately archived.");
            if(ImGui::Button("Confirm")){std::string error;const bool success=controllerRecoveryConfirmArchive_?CompleteControllerRecovery(error):RestoreControllerRecovery(error);if(success){controllerRecoveryConfirm_=false;ImGui::CloseCurrentPopup();}else controllerInspectStatus_=error;}
            ImGui::SameLine();if(ImGui::Button("Cancel")){controllerRecoveryConfirm_=false;ImGui::CloseCurrentPopup();}
            ImGui::EndPopup();
        }
    }ImGui::End();
#endif
}
}
