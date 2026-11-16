#include "engine/editor/ControllerRecordSalvage.h"
#include <fstream>
#include <iostream>
static void Need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static void Write(const std::filesystem::path& path,const std::string& text){std::ofstream out(path,std::ios::binary);out<<text;out.close();Need(!out.fail(),"Salvage fixture write failed");}
static std::string Read(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);Need(in.good(),"Salvage fixture read failed");return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};}
int main(){namespace fs=std::filesystem;using namespace Engine::Editor;using namespace Engine::Assets;
    const auto fixture=fs::temp_directory_path()/(L"kubo-controller-salvage-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try{
        for(const auto family:{ControllerPendingFamily::Publication,ControllerPendingFamily::Creation}){
            const auto root=fixture/(family==ControllerPendingFamily::Publication?"publication":"creation");fs::create_directories(root/"Assets/.kubo/controller-writers");
            Write(root/"entry.kubo",R"({"name":"Salvage","assetDirectory":"Assets","startupScene":"entry.scene"})");
            Engine::Base::ProjectContext project;std::string error;Need(project.Open(root/"entry.kubo",error),error.c_str());
            AnimatorControllerDocument doc;Need(AnimatorControllerEdits(doc).New(error),error.c_str());Write(root/"Assets/original.animfsm",doc.Text());
            AssetRegistry registry;Need(registry.Scan(root/"Assets",error),error.c_str());const auto oldGuid=registry.FindByPath(root/"Assets/original.animfsm")->guid;const auto originalMeta=AssetRegistry::MetadataPath(root/"Assets/original.animfsm");const auto oldMeta=Read(originalMeta);
            const std::string guid(32,'e');const auto storage=root/"Assets/.kubo",directory=storage/(family==ControllerPendingFamily::Publication?"controller-transactions":"controller-creations")/guid;fs::create_directories(directory);
            Write(family==ControllerPendingFamily::Publication?storage/"controller-writers"/(guid+".lock"):storage/"controller-create.lock","");
            const auto stage=directory/(family==ControllerPendingFamily::Publication?"next.source":"source.stage");Write(stage,doc.Text());Write(directory/"pending.json","{truncated");
            ControllerRecordReport report;Need(ControllerRecordInspection::Inspect(project,family,guid,report,error),error.c_str());
            AnimatorControllerSession session;Need(ControllerRecordSalvage::OpenDraft(project,report,session,error),error.c_str());
            Need(session.Document() && session.Document()->IsUntitled() && session.Document()->IsDirty() && !session.Source() && session.Document()->Text()==doc.Text(),"Recovered draft inherited saved source identity");
            const auto draft=*session.Document();const auto* address=session.Document();
            Need(!ControllerRecordSalvage::OpenDraft(project,report,session,error) && session.Document()==address && session.Document()->HasSameState(draft),"Repeated dirty salvage replaced draft");
            Write(stage,"invalid");Need(!ControllerRecordSalvage::OpenDraft(project,report,session,error,true) && session.Document()->HasSameState(draft),"Stale stage replaced draft");Write(stage,doc.Text());
            Write(directory/"metadata.stage",R"({"version":1,"guid":"ffffffffffffffffffffffffffffffff"})");Need(ControllerRecordInspection::Inspect(project,family,guid,report,error),error.c_str());
            Need(!ControllerRecordSalvage::OpenDraft(project,report,session,error,true) && session.Document()->HasSameState(draft),"Mismatched metadata stage admitted");
            fs::rename(directory/"metadata.stage",root/"bad-metadata.retained");Need(ControllerRecordInspection::Inspect(project,family,guid,report,error),error.c_str());
            Write(root/"Assets/occupied.animfsm","foreign");Need(session.SaveAs(project,registry,"occupied.animfsm",error)==AnimatorControllerSession::SaveOutcome::Failed && Read(root/"Assets/occupied.animfsm")=="foreign" && session.Document()->HasSameState(draft),"Salvage SaveAs overwrote existing asset/draft");
            const auto outcome=session.SaveAs(project,registry,"recovered.animfsm",error,[](const fs::path&){throw std::runtime_error("Injected acknowledgement failure");});
            Need(outcome==AnimatorControllerSession::SaveOutcome::SavedPendingAcknowledgement && session.HasPendingCreation() && session.Source() && !session.Document()->IsDirty(),"Published salvage did not retain pending acknowledgement");
            const auto newGuid=session.Source()->guid;Need(newGuid!=guid && newGuid!=oldGuid,"Salvage reused original GUID");
            Need(session.SaveAs(project,registry,"duplicate.animfsm",error)==AnimatorControllerSession::SaveOutcome::SavedPendingAcknowledgement && !fs::exists(root/"Assets/duplicate.animfsm"),"Pending salvage duplicated publication");
            Need(!ControllerRecordSalvage::OpenDraft(project,report,session,error,true) && session.Source()->guid==newGuid,"Pending adoption replaced session");
            Need(session.AdoptCreated(project,registry,error) && !session.HasPendingAcknowledgement() && registry.FindByGuid(newGuid),error.c_str());
            Need(Read(root/"Assets/original.animfsm")==doc.Text() && Read(originalMeta)==oldMeta && Read(stage)==doc.Text() && Read(directory/"pending.json")=="{truncated","Salvage mutated original assets/record");
            fs::path archived;Need(ControllerRecordInspection::Quarantine(project,report,archived,error),error.c_str());
            AssertControllerExportResolved(root/"Assets");Need(session.Revalidate(project,registry,error),error.c_str());
            ControllerRecordReport archivedReport;Need(ControllerRecordInspection::InspectArchive(project,family,guid,archived,archivedReport,error),error.c_str());
            Need(archivedReport.directoryIdentity==report.directoryIdentity && archivedReport.files==report.files,"Archive lost native identity/bytes");
            AnimatorControllerSession archivedSession;
            Need(ControllerRecordSalvage::OpenDraft(project,archivedReport,archivedSession,error) && archivedSession.Document()->IsUntitled() && !archivedSession.Source(),"Archived stage could not prepare detached draft");
            const auto heldDraft=*archivedSession.Document();Write(archived/stage.filename(),"changed archived stage");
            Need(!ControllerRecordSalvage::OpenDraft(project,archivedReport,archivedSession,error,true) && archivedSession.Document()->HasSameState(heldDraft),"Stale archive replaced draft");
            const auto retained=archivedReport;
            Need(!ControllerRecordInspection::InspectArchive(project,family,guid,root,archivedReport,error) && archivedReport==retained,"Arbitrary folder accepted as archive/output changed");
        }
        std::cout<<"SALVAGE_FIXTURE="<<Engine::Base::PathToUtf8(fixture)<<'\n';std::cout<<"PASS: held pending+quarantined stage -> untitled draft -> new GUID SaveAs, original asset/record retention, dirty/stale/metadata/occupied refusal, published-pending acknowledgement/no duplicate/retry adoption/quarantine/archive identity+bytes/arbitrary archive refusal (no GUI)\n";return 0;
    }catch(const std::exception& exception){std::cerr<<exception.what()<<"\nRetained fixture: "<<fixture<<'\n';return 1;}
}
