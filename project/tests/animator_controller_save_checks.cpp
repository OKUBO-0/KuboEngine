#include "engine/editor/AnimatorControllerSession.h"
#include <fstream>
#include <iostream>
static void Need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static void Write(const std::filesystem::path& path,const std::string& text){std::ofstream stream(path,std::ios::binary);stream<<text;stream.close();Need(!stream.fail(),"Save fixture write failed");}
static std::string Read(const std::filesystem::path& path){std::ifstream stream(path,std::ios::binary);Need(stream.good(),"Save fixture read failed");return {std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};}
int main(){namespace fs=std::filesystem;using namespace Engine::Editor;using namespace Engine::Assets;using Outcome=AnimatorControllerSession::SaveOutcome;
    const auto fixture=fs::temp_directory_path()/(L"kubo-controller-save-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try{
        for(const char* scenario:{"normal","conflict","metadata","pending","pending-edited","pending-conflict","untitled","wrong-project","unowned-pending"}){
            const std::string mode=scenario;const auto root=fixture/scenario;fs::create_directories(root/"Assets");Write(root/"entry.kubo",R"({"name":"Controller Save","assetDirectory":"Assets","startupScene":"entry.scene"})");
            Engine::Base::ProjectContext project;std::string error;Need(project.Open(root/"entry.kubo",error),error.c_str());
            AnimatorControllerDocument document;Need(AnimatorControllerEdits(document).New(error),error.c_str());const auto file=root/L"Assets/保存.animfsm";
            // Opening canonicalizes only the private draft. Clean Save must
            // retain the original source formatting and native identity.
            const auto originalBytes=std::string(" \n")+document.Text()+"\n";Write(file,originalBytes);
            AssetRegistry registry;Need(registry.Scan(root/"Assets",error),error.c_str());const auto guid=registry.FindByPath(file)->guid;
            AnimatorControllerSession session;Need(session.Open(project,registry,guid,error),error.c_str());const auto original=*session.Source();auto* draft=session.Document();
            Need(session.Save(project,registry,error)==Outcome::Saved && Read(file)==originalBytes && session.Source()->sourceIdentity==original.sourceIdentity && session.LastArchive().empty(),"Clean Save rewrote formatted source");
            if(mode=="unowned-pending"){fs::create_directories(root/"Assets/.kubo/controller-transactions"/guid);Need(session.Save(project,registry,error)==Outcome::Failed && !draft->IsDirty() && Read(file)==originalBytes,"Clean Save ignored incomplete pending transaction");continue;}
            if(mode=="untitled"){Need(session.New(error),error.c_str());const auto before=*session.Document();Need(session.Save(project,registry,error)==Outcome::Failed && session.Document()->HasSameState(before) && Read(file)==originalBytes,"Untitled Save overwrote existing source");continue;}
            auto layer=draft->Definition().layers[0];layer.weight=.5f;Need(AnimatorControllerEdits(*draft).ReplaceLayer(0,layer,error),error.c_str());const auto dirty=*draft;
            if(mode=="conflict" || mode=="metadata"){
                const auto target=mode=="conflict"?file:original.metadata;Write(target,"foreign data");
                Need(session.Save(project,registry,error)==Outcome::Failed && draft->HasSameState(dirty) && session.Source()->sourceIdentity==original.sourceIdentity && Read(target)=="foreign data","Conflict Save changed baseline/draft/foreign source");continue;
            }
            if(mode=="wrong-project"){
                fs::create_directories(root/"Other/Assets");Write(root/"Other/entry.kubo",R"({"name":"Other","assetDirectory":"Assets","startupScene":"entry.scene"})");Engine::Base::ProjectContext other;Need(other.Open(root/"Other/entry.kubo",error),error.c_str());
                Need(session.Save(other,registry,error)==Outcome::Failed && draft->HasSameState(dirty) && Read(file)==originalBytes,"Wrong project Save changed source");continue;
            }
            const bool pendingMode=mode.starts_with("pending");
            const auto failAck=[&](const fs::path&){Need(session.HasPendingAcknowledgement() && !draft->IsDirty() && !session.Close(error,true),"Saved pending session allowed replacement");throw std::runtime_error("ack injection");};
            const auto outcome=session.Save(project,registry,error,pendingMode?std::function<void(const fs::path&)>(failAck):std::function<void(const fs::path&)>{});
            Need(outcome==(pendingMode?Outcome::SavedPendingAcknowledgement:Outcome::Saved) && draft==session.Document() && !draft->IsDirty() && draft->UndoCount()==1 && session.Source()->bytes==draft->Text() && session.Source()->sourceIdentity!=original.sourceIdentity && Read(file)==draft->Text() && Read(original.metadata)==original.metadataBytes,"Saved document/source/history not coordinated");
            Need(session.Revalidate(project,registry,error),error.c_str());
            if(pendingMode){
                Need(session.HasPendingAcknowledgement() && session.LastArchive().empty() && !session.New(error,true) && !session.Open(project,registry,guid,error,true),"Pending acknowledgement silently discarded");
                if(mode=="pending-conflict"){
                    Write(file,"foreign after publication");const auto before=*draft;
                    Need(!session.AcknowledgePending(project,registry,error) && session.HasPendingAcknowledgement() && draft->HasSameState(before) && Read(file)=="foreign after publication","Pending Ack hid source conflict");continue;
                }
                if(mode=="pending-edited"){auto changed=draft->Definition().layers[0];changed.weight=.75f;Need(AnimatorControllerEdits(*draft).ReplaceLayer(0,changed,error),error.c_str());}
                Need(session.Save(project,registry,error)==Outcome::Saved && !session.HasPendingAcknowledgement() && !draft->IsDirty() && session.Source()->bytes==draft->Text(),"Pending retry did not acknowledge/save current draft");
            }
            Need(fs::exists(session.LastArchive()/"pending.json") && !fs::exists(root/"Assets/.kubo/controller-transactions"/guid),"Saved transaction not retained in archive");
            const auto saved=*session.Source();Need(session.Save(project,registry,error)==Outcome::Saved && session.Source()->sourceIdentity==saved.sourceIdentity,"Repeated clean Save changed source identity");
            Need(draft->Undo(error) && draft->IsDirty() && Read(file)==saved.bytes,"Undo changed source or lost saved baseline");
            Need(draft->Redo(error) && !draft->IsDirty() && Read(file)==saved.bytes,"Redo did not return to saved baseline");
            Need(draft->Undo(error) && session.Save(project,registry,error)==Outcome::Saved && !draft->IsDirty() && draft->RedoCount()==1,"Saving Undo lost redo/history");
            Need(session.Close(error),error.c_str());
        }
        std::cout<<"SAVE_FIXTURE="<<Engine::Base::PathToUtf8(fixture)<<'\n';
        std::cout<<"PASS: Controller registered synchronous Save/document+source baseline/history/raw clean no-op/external conflict/pending Ack retry+edited draft/untitled+wrong project refusal (GUI SaveAs/owner reload pending)\n";return 0;
    }catch(const std::exception& exception){std::cerr<<exception.what()<<"\nRetained Save fixture: "<<fixture<<'\n';return 1;}
}
