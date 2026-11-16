#include "engine/editor/AnimatorControllerSession.h"
#include <fstream>
#include <iostream>
static void Need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static void Write(const std::filesystem::path& path,const std::string& bytes){std::ofstream stream(path,std::ios::binary);stream<<bytes;stream.close();Need(!stream.fail(),"Save As fixture write failed");}
static std::string Read(const std::filesystem::path& path){std::ifstream stream(path,std::ios::binary);Need(stream.good(),"Save As fixture read failed");return {std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};}
int main(){namespace fs=std::filesystem;using namespace Engine::Editor;using namespace Engine::Assets;using Outcome=AnimatorControllerSession::SaveOutcome;
    const auto fixture=fs::temp_directory_path()/(L"kubo-controller-save-as-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try{
        for(const char* scenario:{"untitled","copy","occupied","outside","pending","pending-edit","late-asset","duplicate","changed-source","wrong-project","wrong-registry"}){
            const std::string mode=scenario;const auto root=fixture/scenario;fs::create_directories(root/"Assets/Graphs");Write(root/"entry.kubo",R"({"name":"Save As","assetDirectory":"Assets","startupScene":"entry.scene"})");
            Engine::Base::ProjectContext project;std::string error;Need(project.Open(root/"entry.kubo",error),error.c_str());AnimatorControllerDocument fresh;Need(AnimatorControllerEdits(fresh).New(error),error.c_str());
            const auto original=root/"Assets/original.animfsm";const auto oldBytes=fresh.Text();Write(original,oldBytes);AssetRegistry registry;Need(registry.Scan(root/"Assets",error),error.c_str());const auto oldGuid=registry.FindByPath(original)->guid;const auto oldMeta=Read(AssetRegistry::MetadataPath(original));
            const std::string imported(64,'a');Need(registry.SetImportFingerprint(oldGuid,imported),"Import fixture setup failed");
            AnimatorControllerSession session;Need((mode=="untitled" || mode=="wrong-registry")?session.New(error):session.Open(project,registry,oldGuid,error),error.c_str());auto* document=session.Document();
            auto layer=document->Definition().layers[0];layer.weight=.5f;Need(AnimatorControllerEdits(*document).ReplaceLayer(0,layer,error),error.c_str());const auto draft=*document;
            const auto destination=root/L"Assets/Graphs/別名.animfsm";
            if(mode=="wrong-registry"){fs::create_directory(root/"OtherAssets");AssetRegistry other;Need(other.Scan(root/"OtherAssets",error,AssetRegistry::MetadataPolicy::ReadOnly),error.c_str());const auto otherRoot=other.GetRoot();Need(session.SaveAs(project,other,"Graphs/別名.animfsm",error)==Outcome::Failed && document->HasSameState(draft) && !fs::exists(destination) && other.GetRoot()==otherRoot,"Untitled Save As replaced another registry root");continue;}
            if(mode=="occupied")Write(destination,"foreign destination");
            if(mode=="wrong-project"){
                fs::create_directories(root/"Other/Assets");Write(root/"Other/entry.kubo",R"({"name":"Other","assetDirectory":"Assets","startupScene":"entry.scene"})");Engine::Base::ProjectContext other;Need(other.Open(root/"Other/entry.kubo",error),error.c_str());
                Need(session.SaveAs(other,registry,"Graphs/other.animfsm",error)==Outcome::Failed && document->HasSameState(draft) && Read(original)==oldBytes,"Save As switched owning project");continue;
            }
            const bool pending=mode=="pending" || mode=="pending-edit" || mode=="duplicate" || mode=="changed-source";
            const auto hook=[&](const fs::path&){
                Need(!document->IsDirty() && session.HasPendingCreation() && !session.Close(error,true),"Pending Save As allowed replacement before adoption");
                if(mode=="late-asset")Write(root/"Assets/late.lua","return {}");
                if(mode=="duplicate"){Write(root/"Assets/duplicate.animfsm",fresh.Text());Write(AssetRegistry::MetadataPath(root/"Assets/duplicate.animfsm"),session.Source()->metadataBytes);}
                else if(pending)throw std::runtime_error("Save As acknowledgement injection");
            };
            const auto outcome=session.SaveAs(project,registry,mode=="outside"?"../outside.animfsm":"Graphs/別名.animfsm",error,hook);
            if(mode=="occupied" || mode=="outside"){
                Need(outcome==Outcome::Failed && document->HasSameState(draft) && session.Source()->guid==oldGuid && registry.GetAssets().size()==1,"Rejected Save As changed session/registry");
                if(mode=="occupied")Need(Read(destination)=="foreign destination","Save As overwrote destination");continue;
            }
            Need(outcome==(pending?Outcome::SavedPendingAcknowledgement:Outcome::Saved) && session.Document()==document && !document->IsDirty() && !document->IsUntitled() && document->UndoCount()==1 && session.Source()->guid!=oldGuid && Read(destination)==document->Text() && Read(original)==oldBytes && Read(AssetRegistry::MetadataPath(original))==oldMeta,"Save As source/draft/history/original preservation failed");
            const auto guid=session.Source()->guid;
            if(pending){
                Need(session.HasPendingCreation() && !registry.FindByGuid(guid) && registry.GetAssets().size()==1 && session.Save(project,registry,error)==Outcome::SavedPendingAcknowledgement && !session.New(error,true),"Pending Save As published registry/ignored guards");
                if(mode=="changed-source"){Write(destination,"foreign source");Need(!session.AdoptCreated(project,registry,error) && session.HasPendingCreation() && !registry.FindByGuid(guid) && Read(destination)=="foreign source","Pending adoption hid source conflict");continue;}
                if(mode=="duplicate"){Need(!session.AdoptCreated(project,registry,error) && !registry.FindByGuid(guid),"Duplicate GUID adoption accepted");continue;}
                if(mode=="pending-edit"){auto changed=document->Definition().layers[0];changed.weight=.75f;Need(AnimatorControllerEdits(*document).ReplaceLayer(0,changed,error),error.c_str());}
                const bool wasDirty=document->IsDirty();Need(session.AdoptCreated(project,registry,error) && document->IsDirty()==wasDirty,"Adoption marked a newer edit saved");
            }
            Need(!session.HasPendingAcknowledgement() && registry.FindByGuid(guid) && registry.FindByGuid(oldGuid)->importFingerprint==imported && fs::exists(session.LastArchive()/"pending.json") && session.Revalidate(project,registry,error),"Adoption lost registry/import/pending state");
            if(mode=="late-asset")Need(registry.FindByPath(root/"Assets/late.lua"),"Save As registry omitted asset created before acknowledgement");
            Need(session.Save(project,registry,error)==Outcome::Saved && !document->IsDirty() && Read(destination)==document->Text(),"Save after adoption failed");
            Need(document->Undo(error) && document->IsDirty() && Read(original)==oldBytes && Read(AssetRegistry::MetadataPath(original))==oldMeta,"Undo after Save As modified original");
            Need(session.Save(project,registry,error)==Outcome::Saved && !document->IsDirty() && document->RedoCount()==1 && Read(original)==oldBytes,"Saving Undo after Save As targeted original/lost history");
        }
        std::cout<<"SAVE_AS_FIXTURE="<<Engine::Base::PathToUtf8(fixture)<<'\n';
        std::cout<<"PASS: Controller Save As/untitled/new GUID/no overwrite/original retention/saved source+document/history/pending adoption+retry+edited draft/private registry/import preservation/late asset+duplicate+external conflict guards (GUI pending)\n";return 0;
    }catch(const std::exception& exception){std::cerr<<exception.what()<<"\nRetained Save As fixture: "<<fixture<<'\n';return 1;}
}
