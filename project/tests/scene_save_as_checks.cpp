#include "engine/editor/ProjectSceneSave.h"
#include "engine/editor/EntityDocumentState.h"
#include "engine/base/AtomicFile.h"
#include <Windows.h>
#include <fstream>
#include <iostream>
#include <limits>
namespace {
void Require(bool value,const char* reason) {if(!value) throw std::runtime_error(reason);}
void Write(const std::filesystem::path& path,const std::string& bytes) {std::ofstream output(path,std::ios::binary);output<<bytes;output.close();Require(!output.fail(),"Fixture write failed");}
std::string Read(const std::filesystem::path& path) {std::ifstream input(path,std::ios::binary);return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};}
}
int main() {
    namespace fs=std::filesystem;using namespace Engine::Scene;using Engine::Editor::EntityDocumentState;
    const auto fixture=fs::temp_directory_path()/(L"kubo-save-as-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    const auto previous=fs::current_path();
    try {
        const auto root=fixture/"Project";fs::create_directories(root);
        Write(root/"Game.kubo",R"({"name":"Save As","startupScene":"original.scene"})");
        Engine::Base::ProjectContext project;std::string error;Require(project.Open(root/"Game.kubo",error),error.c_str());
        EntityWorld world;const auto entity=world.CreateEntity("Authored");
        Require(world.Save(root/"original.scene",error),error.c_str());const auto originalBytes=Read(root/"original.scene");
        const auto manifestBytes=Read(root/"Game.kubo");
        EntityDocumentState document;EntityWorld empty;
        document.Opened(empty,{});document.Observe(empty);
        Require(document.IsUntitled() && document.IsDirty() && document.Path().empty(),"Empty new Scene was treated as saved");
        document.MarkSaved(empty);Require(document.IsDirty(),"Untitled MarkSaved falsely cleared dirty state");
        Require(!document.MarkSavedAt(empty,root/"original.scene"),"Unrelated save marked untitled document saved");
        Require(document.Request(EntityDocumentState::Action::New) && document.NeedsConfirmation(),"New action discarded untitled Scene without confirmation");
        document.Cancel();Require(document.IsDirty() && document.IsUntitled(),"New cancellation lost untitled state");
        fs::create_directories(fixture/"Unrelated");fs::current_path(fixture/"Unrelated");
        fs::path output="retained";
        for(const auto& invalid : {fs::path{},fixture/"outside.scene",fs::path(L"../outside.scene"),root/"Game.kubo",root/"bad.txt",root/L"AUX.scene",root/L"COM1.scene",root/L"LPT\u00b2.scene",root/L"stream:extra.scene",root/L"folder./bad.scene",fs::path(std::wstring(L"x\0y.scene",9))}) {
            Require(!Engine::Editor::SaveProjectSceneAs(project,world,invalid,false,output,error) && output=="retained","Invalid Save As changed result");
        }
        const auto target=root/L"Assets/場面/保存.scene";
        Require(Engine::Editor::SaveProjectSceneAs(project,world,target,false,output,error) && output==target,error.c_str());
        EntityWorld loaded;Require(loaded.Load(target,error) && loaded.Capture()==world.Capture(),"Unicode Save As did not roundtrip");
        const auto savedBytes=Read(target);world.Find(entity)->name="Edited";
        Require(!Engine::Editor::SaveProjectSceneAs(project,world,target,false,output,error) && Read(target)==savedBytes,"Unapproved Save As replaced existing Scene");
        const HANDLE lock=CreateFileW(target.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        Require(lock!=INVALID_HANDLE_VALUE,"Lock fixture failed");
        const bool lockedRejected=!Engine::Editor::SaveProjectSceneAs(project,world,target,true,output,error);CloseHandle(lock);
        Require(lockedRejected && Read(target)==savedBytes,"Failed overwrite changed destination");
        Require(Engine::Editor::SaveProjectSceneAs(project,world,target,true,output,error) && loaded.Load(target,error) && loaded.Capture()==world.Capture(),"Approved valid Scene replacement failed");
        Write(root/"settings.json","{\"configuration\":true}");const auto otherBytes=Read(root/"settings.json");
        Require(!Engine::Editor::SaveProjectSceneAs(project,world,root/"settings.json",true,output,error) && Read(root/"settings.json")==otherBytes,"Save As overwrote a non-Scene JSON file");
        const auto newTarget=root/L"Assets/場面/新規.scene";
        Require(document.RequestOpen(target),"Untitled Open request failed");
        Require(Engine::Editor::SaveProjectSceneAs(project,empty,newTarget,false,output,error) && document.SavedAs(empty,output),error.c_str());
        Require(!document.IsUntitled() && !document.IsDirty() && document.PendingTarget()==target && document.HasPendingAction(),"Save As lost pending action/target or dirty state");
        Require(document.SavedAndProceed() && document.TakeReadyAction()==EntityDocumentState::Action::Open,"Save As did not allow Save and continue");
        Require(Read(root/"original.scene")==originalBytes && Read(root/"Game.kubo")==manifestBytes,"Save As changed original Scene/startup manifest");
        const auto exclusive=root/"exclusive.scene";
        Require(Engine::Base::AtomicFile::Write(exclusive,"first",100,error,false),error.c_str());
        Require(!Engine::Base::AtomicFile::Write(exclusive,"competitor",100,error,false) && Read(exclusive)=="first","Final no-replace move clobbered an existing destination");
        world.Find(entity)->transform.position.x=std::numeric_limits<float>::quiet_NaN();
        const auto invalidWorldTarget=root/"invalid.scene";
        Require(!Engine::Editor::SaveProjectSceneAs(project,world,invalidWorldTarget,false,output,error) && !fs::exists(invalidWorldTarget),"Invalid authored world was saved");
        world.Find(entity)->transform.position.x=0;world.Find(entity)->name=std::string("\xff",1);
        Require(!Engine::Editor::SaveProjectSceneAs(project,world,invalidWorldTarget,false,output,error) && !fs::exists(invalidWorldTarget),"Save As wrote a Scene that cannot be read as UTF-8");
        for(const auto& entry:fs::recursive_directory_iterator(root)) Require(entry.path().filename().native().find(L".kubo-save-")==std::wstring::npos,"Save As leaked owned temporary");
        fs::current_path(previous);fs::remove_all(fixture);
        std::cout<<"PASS: untitled dirty/confirmation state, project-owned Unicode Save As, original/startup preservation, explicit generic overwrite, atomic no-replace, locked/non-Scene failures and pending Save-and-continue\n";return 0;
    } catch(const std::exception& exception) {fs::current_path(previous);std::error_code cleanup;fs::remove_all(fixture,cleanup);std::cerr<<exception.what()<<'\n';return 1;}
}
