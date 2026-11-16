#include "engine/editor/AnimatorControllerSession.h"
#include <fstream>
#include <iostream>
static void Need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
static void Write(const std::filesystem::path& file,const std::string& text){std::ofstream stream(file,std::ios::binary);stream<<text;stream.close();Need(!stream.fail(),"Session fixture write failed");}
int main(){
    namespace fs=std::filesystem;using namespace Engine::Editor;
    const auto root=fs::temp_directory_path()/(L"kubo-controller-session-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try {
        fs::create_directories(root/"Assets");Write(root/"entry.kubo",R"({"name":"Controller Session","assetDirectory":"Assets","startupScene":"entry.scene"})");
        Engine::Base::ProjectContext project;std::string error;Need(project.Open(root/"entry.kubo",error),error.c_str());
        AnimatorControllerDocument fresh;Need(AnimatorControllerEdits(fresh).New(error),"Fixture graph failed");
        const auto file=root/L"Assets/移動.animfsm";Write(file,fresh.Text());Write(root/"Assets/wrong.lua","return {}");
        Engine::Assets::AssetRegistry registry;Need(registry.Scan(root/"Assets",error),error.c_str());
        const auto guid=registry.FindByPath(file)->guid;const auto wrong=registry.FindByPath(root/"Assets/wrong.lua")->guid;
        AnimatorControllerSession session;Need(session.Open(project,registry,guid,error) && session.Document() && !session.Document()->IsDirty() && session.Source()->guid==guid,"Session Open failed");
        const auto firstOwner=session.GuiOwnerEpoch();Need(firstOwner!=0,"Open has no GUI owner epoch");
        const auto original=*session.Source();auto* heldDocument=session.Document();const auto* heldSource=session.Source();
        Need(session.Revalidate(project,registry,error),error.c_str());
        // A detached session does not leave a read lease blocking external tools.
        const auto writer=CreateFileW(file.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        Need(writer!=INVALID_HANDLE_VALUE,"Session retained a source read lease");CloseHandle(writer);
        Need(!session.Open(project,registry,wrong,error) && session.Document()==heldDocument && session.Source()==heldSource,"Wrong-type Open replaced session");
        Need(session.GuiOwnerEpoch()==firstOwner,"Failed Open retired active GUI owner");
        auto layer=session.Document()->Definition().layers[0];layer.weight=0.5f;Need(AnimatorControllerEdits(*session.Document()).ReplaceLayer(0,layer,error),"Session edit failed");
        const auto dirty=*session.Document();
        Need(!session.Close(error) && !session.New(error) && !session.Open(project,registry,guid,error) && session.Document()->HasSameState(dirty),"Dirty session silently replaced");
        Write(file,"{}");Need(!session.Open(project,registry,guid,error,true) && session.Document()->HasSameState(dirty) && session.Source()==heldSource,"Approved discard before failed Open lost draft");
        Write(file,original.bytes);Need(session.Revalidate(project,registry,error),"Restored source baseline failed");
        const auto metadata=Engine::Assets::AssetRegistry::MetadataPath(file);
        Write(metadata,"{\"version\":1,\"guid\":\""+guid+"\",\"note\":\"changed outside\"}");
        Need(!session.Revalidate(project,registry,error) && session.Document()->HasSameState(dirty),"External metadata edit missed conflict");Write(metadata,original.metadataBytes);
        auto changed=original.bytes;const auto weight=changed.find("\"weight\":1");Need(weight!=std::string::npos,"Same-size fixture field missing");changed.replace(weight,10,"\"weight\":0");
        Need(changed.size()==original.bytes.size(),"Same-size fixture length differs");
        const auto timestamp=fs::last_write_time(file);Write(file,changed);fs::last_write_time(file,timestamp);
        Need(!session.Revalidate(project,registry,error) && session.Document()->HasSameState(dirty),"External same-time source edit missed conflict");
        Need(!session.Open(project,registry,guid,error,true,&original) && session.Document()->HasSameState(dirty),"Stale expected source accepted");
        Need(session.GuiOwnerEpoch()==firstOwner,"Refused lifecycle action changed GUI owner");
        Need(session.Open(project,registry,guid,error,true) && !session.Document()->IsDirty() && session.Source()->bytes==changed && session.Document()->Definition().layers[0].weight==0,"Explicit discard/reload did not adopt new baseline");
        Need(session.GuiOwnerEpoch()==firstOwner+1,"Successful Open reused GUI owner");
        Need(session.Close(error) && !session.Document() && !session.Source() && session.Close(error),"Clean/idempotent Close failed");
        Need(session.New(error) && session.Document()->IsDirty() && session.Document()->IsUntitled() && !session.Source() && !session.Revalidate(project,registry,error),"Untitled session source semantics failed");
        Need(session.GuiOwnerEpoch()==firstOwner+2,"Close/New reset GUI owner counter");
        Need(session.Close(error,true) && !session.Document(),"Approved discard Close failed");
        const auto prior=session.GuiOwnerEpoch();Need(!session.NewFromText("{}",error) && session.GuiOwnerEpoch()==prior && session.NewFromText(fresh.Text(),error) && session.GuiOwnerEpoch()==prior+1,"Recovered draft epoch publication failed");
        bool exhausted=false;try{ControllerDocumentEpoch::Next((std::numeric_limits<uint64_t>::max)());}catch(const std::overflow_error&){exhausted=true;}Need(exhausted,"GUI owner counter wrapped");
        std::cout<<"SESSION_FIXTURE="<<Engine::Base::PathToUtf8(root)<<'\n';
        std::cout<<"PASS: project Controller session held GUID admission/detached baseline/private document/dirty switch guards/failed-open retention/explicit reload/stale source/idempotent close/untitled semantics (no save or GUI)\n";return 0;
    }catch(const std::exception& exception){std::cerr<<exception.what()<<"\nRetained session fixture: "<<root<<'\n';return 1;}
}
