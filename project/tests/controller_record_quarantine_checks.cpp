#include "engine/editor/ControllerRecordInspection.h"
#include "engine/editor/AnimatorControllerEdits.h"
#include <fstream>
#include <iostream>
static void Need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static void Write(const std::filesystem::path& path,const std::string& bytes){std::ofstream file(path,std::ios::binary);file<<bytes;file.close();Need(!file.fail(),"Quarantine fixture write failed");}
static std::string Read(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);Need(file.good(),"Quarantine fixture read failed");return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};}
int main(){namespace fs=std::filesystem;using namespace Engine::Editor;
    const auto fixture=fs::temp_directory_path()/(L"kubo-controller-quarantine-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try{
        for(const auto family:{ControllerPendingFamily::Publication,ControllerPendingFamily::Creation}){
            const auto root=fixture/(family==ControllerPendingFamily::Publication?"publication":"creation");
            fs::create_directories(root/"Assets/.kubo/controller-writers");
            Write(root/"entry.kubo",R"({"name":"Quarantine","assetDirectory":"Assets","startupScene":"entry.scene"})");
            Engine::Base::ProjectContext project;std::string error;Need(project.Open(root/"entry.kubo",error),error.c_str());
            AnimatorControllerDocument doc;Need(AnimatorControllerEdits(doc).New(error),error.c_str());
            Write(root/"Assets/original.animfsm",doc.Text());Write(root/"Assets/original.animfsm.meta","original metadata bytes");
            const std::string guid(32,'a');const auto storage=root/"Assets/.kubo";
            const auto directory=storage/(family==ControllerPendingFamily::Publication?"controller-transactions":"controller-creations")/guid;
            fs::create_directories(directory);
            const auto lockPath=family==ControllerPendingFamily::Publication?storage/"controller-writers"/(guid+".lock"):storage/"controller-create.lock";Write(lockPath,"");
            const auto stage=family==ControllerPendingFamily::Publication?"next.source":"source.stage";
            Write(directory/stage,doc.Text());Write(directory/"pending.json","{truncated");Write(directory/L"unknown 日本語.bin",std::string("a\0b",3));
            ControllerRecordReport report;Need(ControllerRecordInspection::Inspect(project,family,guid,report,error),error.c_str());
            const auto oldReport=report;fs::path archive="unchanged output";const auto sentinel=archive;
            HANDLE active=CreateFileW(lockPath.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);Need(active!=INVALID_HANDLE_VALUE,"Active writer fixture failed");
            const bool blocked=!ControllerRecordInspection::Quarantine(project,report,archive,error);CloseHandle(active);
            Need(blocked && archive==sentinel && fs::exists(directory),"Active writer lost record/output");
            Write(directory/"added-child","extra");Need(!ControllerRecordInspection::Quarantine(project,report,archive,error) && archive==sentinel && fs::exists(directory/"added-child"),"Stale report accepted/output changed");
            Need(ControllerRecordInspection::Inspect(project,family,guid,report,error),error.c_str());
            fs::path collision;
            Need(!ControllerRecordInspection::Quarantine(project,report,archive,error,[&](const fs::path& target){collision=target;fs::create_directory(target);Write(target/"foreign","occupied");}) && archive==sentinel && Read(collision/"foreign")=="occupied" && fs::exists(directory),"No-replace collision overwrote or lost record");
            Need(!ControllerRecordInspection::Quarantine(project,report,archive,error,[&](const fs::path&){Write(storage/"asset-move.pending.json","move");}) && archive==sentinel && fs::exists(directory),"Late asset move admitted");
            fs::rename(storage/"asset-move.pending.json",root/"retained-move");
            crude_json::value intent;intent["format"]=family==ControllerPendingFamily::Publication?"kubo-controller-source-publication-1":"kubo-controller-creation-1";intent["guid"]=guid;
            for(size_t i=2;i<(family==ControllerPendingFamily::Publication?9u:7u);++i)intent["field"+std::to_string(i)]=0.0;
            Write(directory/"pending.json",intent.dump());Need(ControllerRecordInspection::Inspect(project,family,guid,report,error),error.c_str());
            Need(!ControllerRecordInspection::Quarantine(project,report,archive,error) && archive==sentinel && fs::exists(directory),"Known canonical intent bypassed normal recovery");
            Write(directory/"pending.json","{truncated");Need(ControllerRecordInspection::Inspect(project,family,guid,report,error),error.c_str());
            bool pendingRefused=false;try{Engine::Assets::AssertControllerResolved(root/"Assets",guid);}catch(const std::exception&){pendingRefused=true;}Need(pendingRefused,"Pending reader guard was inactive");
            Need(ControllerRecordInspection::Quarantine(project,report,archive,error),error.c_str());
            Need(!fs::exists(directory) && fs::is_directory(archive) && archive!=sentinel && Read(archive/stage)==doc.Text() && Read(archive/"pending.json")=="{truncated" && Read(archive/L"unknown 日本語.bin")==std::string("a\0b",3) && Read(archive/"added-child")=="extra","Quarantine lost contents");
            Need(Read(root/"Assets/original.animfsm")==doc.Text() && Read(root/"Assets/original.animfsm.meta")=="original metadata bytes" && oldReport.files.size()==3,"Original asset/report changed");
            Engine::Assets::AssertControllerResolved(root/"Assets",guid);Engine::Assets::AssertControllerExportResolved(root/"Assets");
            const auto committed=archive;Need(!ControllerRecordInspection::Quarantine(project,report,archive,error) && archive==committed,"Repeated action duplicated archive/output changed");
            fs::create_directory(directory);ControllerRecordReport empty;Need(ControllerRecordInspection::Inspect(project,family,guid,empty,error),error.c_str());
            Need(!ControllerRecordInspection::Quarantine(project,report,archive,error) && archive==committed && fs::exists(directory),"Replacement directory identity ignored");
            Need(ControllerRecordInspection::Quarantine(project,empty,archive,error) && fs::is_empty(archive),"Empty partial record quarantine failed");
        }
        std::cout<<"QUARANTINE_FIXTURE="<<Engine::Base::PathToUtf8(fixture)<<'\n';
        std::cout<<"PASS: expected owned record quarantine/stale+active+late-move+occupied+canonical-intent refusal/no-replace/all contents+original source+metadata retention/empty record/repeat+replacement identity/reader+export unblock (no salvage/GUI/hostile tree sandbox)\n";return 0;
    }catch(const std::exception& exception){std::cerr<<exception.what()<<"\nRetained fixture: "<<fixture<<'\n';return 1;}
}
