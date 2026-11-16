#include "engine/editor/AnimatorControllerPanel.h"
#include "engine/editor/ControllerEditorPreferencesFile.h"
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <new>
static long failAfter=-1;static bool counting=false;static size_t allocations=0;
void* operator new(size_t size){if(counting)++allocations;if(failAfter==0){failAfter=-1;throw std::bad_alloc();}if(failAfter>0)--failAfter;if(auto* p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept{std::free(p);}void operator delete(void* p,size_t) noexcept{std::free(p);}
static void Need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static void Write(const std::filesystem::path& path,const std::string& bytes){std::ofstream file(path,std::ios::binary);file<<bytes;file.close();Need(!file.fail(),"Fixture write failed");}
static std::string Read(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);Need(file.good(),"Fixture read failed");return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};}
int main(){namespace fs=std::filesystem;using namespace Engine;using namespace Engine::Editor;
    const auto root=fs::temp_directory_path()/(L"kubo-controller-preferences-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try{
        fs::create_directories(root/"Assets");Write(root/"entry.kubo",R"({"name":"Preferences","startupScene":"Assets/Main.scene"})");
        Base::ProjectContext project;std::string error;Need(project.Open(root/"entry.kubo",error),error.c_str());
        bool enabled=false;ControllerCanvasLayoutSnapshot receipt;
        Need(ControllerEditorPreferencesFile::Load(project,enabled,receipt,error) && enabled && !receipt.exists && !fs::exists(root/"UserSettings"),"Absent preferences did not load default without writes");
        Need(ControllerEditorPreferencesFile::SaveChecked(project,false,receipt,error),error.c_str());const auto path=receipt.path,dir=path.parent_path();const auto disabled=receipt.bytes;
        Need(Read(path)==disabled && path.filename()==L"editor.json","Preference stored in wrong destination");
        AnimatorControllerPanel first;Need(first.ConfigureLayoutProject(project,error) && first.EditorPreferencesReady() && !first.AutoSaveLayoutEnabled() && first.New(error),"New Panel did not restore disabled project preference");
        const auto before=*first.Session().Document();Need(first.SetAutoSaveLayout(true,error) && first.Session().Document()->HasSameState(before),"Preference toggle changed source/history");
        AnimatorControllerPanel second;Need(second.ConfigureLayoutProject(project,error) && second.AutoSaveLayoutEnabled(),"Fresh Panel did not restore enabled preference");
        Need(first.SetAutoSaveLayout(false,error),error.c_str());const auto current=Read(path);
        Need(!second.SetAutoSaveLayout(true,error) && second.AutoSaveLayoutEnabled() && Read(path)==current,"Stale writer overwrote settings or changed local preference");
        Write(path,"broken external preferences");Need(!second.ReloadEditorPreferences(error) && !second.EditorPreferencesReady() && !second.AutoSaveLayoutEnabled() && Read(path)=="broken external preferences","Failed explicit reload left automatic writes enabled or changed external bytes");
        Need(!second.SetAutoSaveLayout(true,error),"Failed reload admitted preference save without fresh receipt");Write(path,current);
        Need(second.ReloadEditorPreferences(error) && !second.AutoSaveLayoutEnabled(),"Explicit reload did not accept fresh external settings");
        fs::create_directories(root/"Other/Assets");Write(root/"Other/entry.kubo",Read(root/"entry.kubo"));Base::ProjectContext other;Need(other.Open(root/"Other/entry.kubo",error),error.c_str());
        Need(second.ConfigureLayoutProject(other,error) && second.AutoSaveLayoutEnabled() && !fs::exists(root/"Other/UserSettings"),"Project switch leaked disabled setting or wrote defaults");
        Need(second.ConfigureLayoutProject(project,error) && !second.AutoSaveLayoutEnabled(),"Switch back did not restore project setting");
        enabled=true;Need(ControllerEditorPreferencesFile::Load(project,enabled,receipt,error) && !enabled,error.c_str());
        const auto checkBad=[&](const std::string& bytes){Write(path,bytes);const auto old=receipt;enabled=false;
            Need(!ControllerEditorPreferencesFile::Load(project,enabled,receipt,error) && !enabled && receipt.path==old.path && receipt.bytes==old.bytes && receipt.exists==old.exists,"Malformed load changed output/receipt");
            AnimatorControllerPanel malformed;Need(malformed.ConfigureLayoutProject(project,error) && !malformed.EditorPreferencesReady() && !malformed.AutoSaveLayoutEnabled() && malformed.New(error),"Malformed optional settings blocked source editing or enabled automatic writes");
            Need(!malformed.SetAutoSaveLayout(true,error) && Read(path)==bytes,"Malformed settings were overwritten");
        };
        for(const auto& bytes:std::vector<std::string>{"{}",R"({"version":2,"autoSaveLayout":false})",R"({"version":1,"autoSaveLayout":0})",R"({"version":1,"autoSaveLayout":true,"extra":0})",R"({"version":1,"autoSaveLayout":true,"autoSaveLayout":false})",std::string(4097,'x'),std::string("\xff",1)})checkBad(bytes);
        Write(path,current);Need(second.ReloadEditorPreferences(error),error.c_str());
        const auto lockPath=fs::path(path.native()+L".lock");HANDLE lock=CreateFileW(lockPath.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);Need(lock!=INVALID_HANDLE_VALUE,"Fixture cannot acquire preferences lock");
        const bool accepted=second.SetAutoSaveLayout(true,error);CloseHandle(lock);Need(!accepted && !second.AutoSaveLayoutEnabled() && Read(path)==current,"Busy preferences lock changed setting/file");
        Need(ControllerEditorPreferencesFile::Load(project,enabled,receipt,error),error.c_str());
        allocations=0;counting=true;Need(ControllerEditorPreferencesFile::Load(project,enabled,receipt,error),error.c_str());counting=false;const auto positions=allocations;
        for(size_t i=0;i<positions;++i){auto tested=receipt;bool testedEnabled=false;failAfter=static_cast<long>(i);const bool ok=ControllerEditorPreferencesFile::Load(project,testedEnabled,tested,error);failAfter=-1;
            Need(!ok && !testedEnabled && tested.path==receipt.path && tested.bytes==receipt.bytes && tested.exists==receipt.exists,"Allocation failure published loaded preference/receipt");}
        fs::rename(dir,root/"preferences-held-check");fs::rename(root/"preferences-held-check",dir); // No leaked native directory handles.
        const auto originalReceipt=receipt;auto savedReceipt=receipt;allocations=0;counting=true;
        Need(ControllerEditorPreferencesFile::SaveChecked(project,true,savedReceipt,error),error.c_str());counting=false;const auto savePositions=allocations;
        for(size_t i=0;i<savePositions;++i){Write(path,current);auto tested=originalReceipt;failAfter=static_cast<long>(i);
            const bool ok=ControllerEditorPreferencesFile::SaveChecked(project,true,tested,error);failAfter=-1;
            Need(!ok && Read(path)==current && tested.path==originalReceipt.path && tested.bytes==originalReceipt.bytes && tested.exists==originalReceipt.exists,"Save allocation failure published preference or receipt");}
        Write(path,current);
        Need(!ControllerEditorPreferencesFile::SaveChecked(other,false,receipt,error) && !fs::exists(root/"Other/UserSettings"),"Foreign receipt saved into another project");
        fs::rename(path,dir/"ordinary-retained.json");
        Need(CreateHardLinkW(path.c_str(),(dir/"ordinary-retained.json").c_str(),nullptr)!=FALSE,"Hardlink fixture unavailable");
        const auto heldReceipt=receipt;enabled=false;
        Need(!ControllerEditorPreferencesFile::Load(project,enabled,receipt,error) && !enabled && receipt.bytes==heldReceipt.bytes && !ControllerEditorPreferencesFile::SaveChecked(project,true,receipt,error),"Multi-link preferences admitted");
        fs::rename(path,root/"hardlink-preference-retained.json");Write(path,current);
        fs::rename(dir,root/"preferences-final-held-check");fs::rename(root/"preferences-final-held-check",dir);
        Need(first.Session().Document()->HasSameState(before),"Preference operations changed Controller document");
        std::cout<<"PASS: per-project Controller preference default/no-create, fresh Panel persistence, project isolation, stale/busy writer refusal, malformed fail-closed optional editing, multi-link refusal and load/save allocation isolation positions="<<positions<<'/'<<savePositions<<'\n';return 0;
    }catch(const std::exception& e){failAfter=-1;counting=false;std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
