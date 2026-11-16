#include "engine/editor/ControllerCanvasLayoutFile.h"
#include "engine/editor/AnimatorControllerEdits.h"
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <new>
static long failAfter=-1;static bool counting=false;static size_t allocations=0;
void* operator new(size_t size){if(counting)++allocations;if(failAfter==0){failAfter=-1;throw std::bad_alloc();}if(failAfter>0)--failAfter;if(auto* p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept{std::free(p);}void operator delete(void* p,size_t) noexcept{std::free(p);}
static void Need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
static void Write(const std::filesystem::path& path,const std::string& bytes){std::ofstream file(path,std::ios::binary);file<<bytes;file.close();Need(!file.fail(),"Fixture write failed");}
static std::string Read(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);Need(file.good(),"Fixture read failed");return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};}
int main(){namespace fs=std::filesystem;using namespace Engine;using namespace Engine::Editor;using Canvas=AnimatorControllerCanvasState;using Kind=Canvas::Kind;
    const auto root=fs::temp_directory_path()/(L"kubo-controller-layout-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try {
        fs::create_directories(root/"Assets");Write(root/"entry.kubo",R"({"name":"Layout fixture","startupScene":"Assets/Main.scene"})");
        Base::ProjectContext project;std::string error;Need(project.Open(root/"entry.kubo",error),error.c_str());
        AnimatorControllerDocument doc;Need(AnimatorControllerEdits(doc).New(error),error.c_str());Need(AnimatorControllerEdits(doc).AddState(0,{"走る###state","default",1,true},error),error.c_str());
        auto extra=doc.Definition().layers[0];extra.name="Overlay";Need(AnimatorControllerEdits(doc).AddLayer(extra,error),error.c_str());
        AnimatorControllerDocument::PreparedSave save;Need(doc.PrepareSave(save,error) && doc.CommitSaved(save),"Clean fixture failed");const auto original=doc;
        const std::string guid(32,'a');Canvas canvas;ControllerCanvasLayoutSnapshot snapshot;bool applied=true;
        Need(ControllerCanvasLayoutFile::Load(project,guid,doc,canvas,snapshot,applied,error) && !applied && !snapshot.exists && !fs::exists(root/"UserSettings"),"Read-only missing layout created directories");
        Need(!ControllerCanvasLayoutFile::Load(project,"",doc,canvas,snapshot,applied,error) && !ControllerCanvasLayoutFile::Load(project,"../bad",doc,canvas,snapshot,applied,error) && !fs::exists(root/"UserSettings"),"Empty/traversal GUID admitted");
        const auto stateKey=Canvas::Key(Kind::StateNode,doc.AuthoringIds().layers[1].states[1]);
        const auto anyKey=Canvas::Key(Kind::AnyNode,doc.AuthoringIds().layers[1].id);
        Need(canvas.Store(stateKey,{731,-194}) && canvas.Store(anyKey,{-55,76}),"Position fixture failed");canvas.Select(doc.AuthoringIds(),1,1);
        Need(ControllerCanvasLayoutFile::SaveChecked(project,guid,doc,canvas,snapshot,error),error.c_str());const auto path=snapshot.path;const auto good=snapshot.bytes;
        Need(Read(path)==good && doc.HasSameState(original) && !fs::exists(root/"Assets"/(guid+".json")),"Layout save changed source/document or wrong directory");
        Need(good.find("authoring")==std::string::npos,"Session IDs leaked into disk format");
        AnimatorControllerDocument reopened;Need(reopened.Load(doc.Text(),error) && reopened.Load(doc.Text(),error),error.c_str());
        Need(reopened.AuthoringIds()!=doc.AuthoringIds(),"Fresh ID epoch fixture ineffective");Canvas restored;ControllerCanvasLayoutSnapshot loaded;
        Need(ControllerCanvasLayoutFile::Load(project,guid,reopened,restored,loaded,applied,error) && applied,"Reopen did not match layout");
        const auto restoredKey=Canvas::Key(Kind::StateNode,reopened.AuthoringIds().layers[1].states[1]);
        size_t layer=0,state=0;restored.Resolve(reopened.AuthoringIds(),layer,state);
        Need(layer==1 && state==1 && restored.Position(restoredKey,{})==Canvas::Point{731,-194} && restored.Position(Canvas::Key(Kind::AnyNode,reopened.AuthoringIds().layers[1].id),{})==Canvas::Point{-55,76},"Fresh IDs lost selection/position mapping");
        auto changed=reopened.Definition().layers[0].states[0];changed.name="Externally changed graph";Need(AnimatorControllerEdits(reopened).ReplaceState(0,0,changed,error),error.c_str());
        const auto retainedCount=restored.PositionCount();
        Need(ControllerCanvasLayoutFile::Load(project,guid,reopened,restored,loaded,applied,error) && !applied && restored.PositionCount()==retainedCount && restored.Position(restoredKey,{})==Canvas::Point{731,-194},"Different graph guessed a mapping");
        Need(!ControllerCanvasLayoutFile::SaveChecked(project,guid,reopened,restored,loaded,error) && Read(path)==good,"Dirty graph layout published");
        Need(reopened.Undo(error),error.c_str());
        // Foreign project and GUID retain caller layout, even with a copied file.
        fs::create_directories(root/"Other/Assets");Write(root/"Other/entry.kubo",Read(root/"entry.kubo"));Base::ProjectContext other;Need(other.Open(root/"Other/entry.kubo",error),error.c_str());fs::create_directories(root/"Other/UserSettings/ControllerLayouts");Write(root/"Other/UserSettings/ControllerLayouts"/(guid+".json"),good);
        Need(ControllerCanvasLayoutFile::Load(other,guid,reopened,restored,loaded,applied,error) && !applied,"Copied foreign project layout applied");
        Write(path.parent_path()/(std::string(32,'b')+".json"),good);Need(ControllerCanvasLayoutFile::Load(project,std::string(32,'b'),reopened,restored,loaded,applied,error) && !applied,"Foreign GUID layout applied");
        Need(ControllerCanvasLayoutFile::Load(project,guid,reopened,restored,loaded,applied,error) && applied,"Restore receipt failed");
        const auto refused=[&](const std::string& bytes){Write(path,bytes);const auto prior=loaded.bytes;const auto priorPath=loaded.path;const auto priorApplied=applied;
            Need(!ControllerCanvasLayoutFile::Load(project,guid,reopened,restored,loaded,applied,error) && loaded.bytes==prior && loaded.path==priorPath && applied==priorApplied && restored.Position(restoredKey,{})==Canvas::Point{731,-194},"Bad layout published output/receipt");
            Need(!ControllerCanvasLayoutFile::SaveChecked(project,guid,reopened,restored,loaded,error) && Read(path)==bytes,"Save overwrote foreign/malformed preferences");Write(path,good);};
        refused("{}");refused(good+" trailing");refused(std::string(ControllerCanvasLayoutFile::Limit+1,' '));
        auto json=crude_json::value::parse(good);json["selection"]["state"]=1.5;refused(json.dump());json=crude_json::value::parse(good);json["layers"][0]["states"][0]=crude_json::array{10000001.0,0.0};refused(json.dump());
        json=crude_json::value::parse(good);json["extra"]=true;refused(json.dump());
        const auto duplicate=std::string("{\"format\":\"wrong\",")+good.substr(1);refused(duplicate);
        Write(path,good+" ");Need(!ControllerCanvasLayoutFile::SaveChecked(project,guid,reopened,restored,loaded,error) && Read(path)==good+" ","Changed existing valid settings silently overwritten");
        Need(ControllerCanvasLayoutFile::Load(project,guid,reopened,restored,loaded,applied,error) && applied && ControllerCanvasLayoutFile::SaveChecked(project,guid,reopened,restored,loaded,error),"Explicit reload/retry failed");
        const auto lockPath=fs::path(path.wstring()+L".lock");HANDLE busy=CreateFileW(lockPath.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);Need(busy!=INVALID_HANDLE_VALUE,"Busy fixture failed");const auto beforeBusy=Read(path);const bool busySaved=ControllerCanvasLayoutFile::SaveChecked(project,guid,reopened,restored,loaded,error);CloseHandle(busy);Need(!busySaved && Read(path)==beforeBusy,"Busy lock overwrote preferences");
        auto countedCanvas=restored;auto countedSnapshot=loaded;allocations=0;counting=true;const bool counted=ControllerCanvasLayoutFile::Load(project,guid,reopened,countedCanvas,countedSnapshot,applied,error);counting=false;Need(counted && applied,"Counted load failed");const auto positions=allocations;
        for(size_t i=0;i<positions;++i){auto candidate=restored;auto receipt=loaded;bool candidateApplied=false;failAfter=static_cast<long>(i);const bool result=ControllerCanvasLayoutFile::Load(project,guid,reopened,candidate,receipt,candidateApplied,error);failAfter=-1;
            Need(!result && !candidateApplied && receipt.bytes==loaded.bytes && receipt.path==loaded.path && receipt.exists==loaded.exists && candidate.PositionCount()==restored.PositionCount() && candidate.Position(restoredKey,{})==Canvas::Point{731,-194},"Allocation failure published layout or receipt");size_t selectedLayer=0,selectedState=0;candidate.Resolve(reopened.AuthoringIds(),selectedLayer,selectedState);Need(selectedLayer==1 && selectedState==1,"Allocation failure changed selection");}
        std::cout<<"LAYOUT_LOAD_ALLOCATION_POSITIONS="<<positions<<'\n';
        fs::rename(lockPath,lockPath.wstring()+L".original");Write(root/"foreign-lock","");Need(CreateHardLinkW(lockPath.c_str(),(root/"foreign-lock").c_str(),nullptr)!=FALSE,"Hardlink lock fixture failed");
        Need(!ControllerCanvasLayoutFile::SaveChecked(project,guid,reopened,restored,loaded,error) && Read(path)==beforeBusy,"Hardlinked lock admitted");fs::rename(lockPath,lockPath.wstring()+L".hardlinked-retained");fs::rename(lockPath.wstring()+L".original",lockPath);
        fs::rename(path,path.wstring()+L".original");Write(root/"foreign.json",good);Need(CreateHardLinkW(path.c_str(),(root/"foreign.json").c_str(),nullptr)!=FALSE,"Hardlink fixture failed");
        Need(!ControllerCanvasLayoutFile::Load(project,guid,reopened,restored,loaded,applied,error) && !ControllerCanvasLayoutFile::SaveChecked(project,guid,reopened,restored,loaded,error) && Read(root/"foreign.json")==good,"Hardlinked layout admitted");
        fs::rename(path,path.wstring()+L".hardlinked-retained");fs::rename(path.wstring()+L".original",path);
        // An ancestor reparse point is never accepted, including read-only load.
        fs::rename(root/"UserSettings",root/"UserSettings.original");fs::create_directory(root/"Outside");
        const bool linked=CreateSymbolicLinkW((root/"UserSettings").c_str(),(root/"Outside").c_str(),SYMBOLIC_LINK_FLAG_DIRECTORY|SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)!=FALSE;
        if(linked)Need(!ControllerCanvasLayoutFile::Load(project,guid,reopened,restored,loaded,applied,error) && !ControllerCanvasLayoutFile::SaveChecked(project,guid,reopened,restored,loaded,error) && fs::is_empty(root/"Outside"),"Linked settings ancestor followed");
        std::cout<<"LAYOUT_REPARSE_TEST="<<(linked?"PASS":"UNAVAILABLE")<<'\n';
        Need(doc.HasSameState(original),"Preference operations changed document/history");
        std::cout<<"LAYOUT_FIXTURE="<<Base::PathToUtf8(root)<<'\n';
        std::cout<<"PASS: Unicode project-local layout persistence/fresh ID remap/selection/all layers+AnyState/content+project+GUID isolation/read-only absence/strict bounded malformed+duplicate refusal/checked external edits/explicit reload retry/busy and hardlink guards; fixtures retained\n";return 0;
    }catch(const std::exception& e){failAfter=-1;std::cerr<<e.what()<<" FIXTURE="<<Base::PathToUtf8(root)<<'\n';return 1;}}
