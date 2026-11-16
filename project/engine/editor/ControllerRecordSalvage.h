#pragma once
#include "ControllerRecordInspection.h"
#include "AnimatorControllerSession.h"

namespace Engine::Editor {
// Explicit private draft preparation from a re-owned pending record. Files,
// registry, World and runtime owners are untouched. Publishing the new asset
// remains Session::SaveAs; SavedPendingAcknowledgement must use AdoptCreated.
class ControllerRecordSalvage final {
public:
    static bool OpenDraft(const Base::ProjectContext& project,const ControllerRecordReport& expected,
                          AnimatorControllerSession& session,std::string& error,bool discardApproved=false){
        ControllerRecordReport unused;
        return ControllerRecordInspection::Run(project,expected.family,expected.guid,unused,error,
            [&](const ControllerRecordReport& report,HANDLE,const std::function<void()>& revalidate,const std::function<void()>&){
                if(report!=expected || !report.hasStage || !report.validStage)
                    throw std::runtime_error("Salvage requires unchanged diagnosed valid Controller stage");
                const auto stage=report.family==ControllerPendingFamily::Publication?"next.source":"source.stage";
                const ControllerRecordFile* staged=nullptr;
                for(const auto& file:report.files){
                    if(file.name==stage)staged=&file;
                    if(file.name=="metadata.stage"){
                        std::string metadataError;
                        if(file.bytes.size()>16384 || !AnimationSystem::Controller::TextCodec::ValidateJson(file.bytes,metadataError))
                            throw std::runtime_error("Salvage metadata stage is invalid");
                        const auto value=crude_json::value::parse(file.bytes);
                        if(!value.is_object() || !value.contains("guid") || !value["guid"].is_string() ||
                           value["guid"].get<crude_json::string>()!=report.guid || !value.contains("version") ||
                           !value["version"].is_number() || value["version"].get<double>()!=1)
                            throw std::runtime_error("Salvage metadata stage GUID/version mismatch");
                    }
                }
                if(!staged)throw std::runtime_error("Salvage stage is absent");
                revalidate();
                // NewFromText prepares a private strict-codec document, then
                // swaps it only on success. No allocating work follows commit.
                std::string draftError;
                if(!session.NewFromText(staged->bytes,draftError,discardApproved))throw std::runtime_error(draftError);
            },expected.directory==expected.root/".kubo"/(expected.family==ControllerPendingFamily::Publication?"controller-transactions":"controller-creations")/expected.guid?
               std::filesystem::path{}:expected.directory);
    }
};
}
