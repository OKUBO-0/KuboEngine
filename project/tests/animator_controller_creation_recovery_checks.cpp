#include "engine/editor/AnimatorControllerCreationRecovery.h"
#include "engine/editor/AnimatorControllerEdits.h"
#include <fstream>
#include <iostream>
static void Need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static void Write(const std::filesystem::path& path,const std::string& bytes){std::ofstream stream(path,std::ios::binary);stream<<bytes;stream.close();Need(!stream.fail(),"Creation recovery fixture write failed");}
static std::string Read(const std::filesystem::path& path){std::ifstream stream(path,std::ios::binary);Need(stream.good(),"Creation recovery fixture read failed");return {std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};}
static int ChildMain(const std::filesystem::path& root,const std::wstring& phase){
    using namespace Engine::Editor;Engine::Base::ProjectContext project;std::string error;Need(project.Open(root/"entry.kubo",error),error.c_str());
    AnimatorControllerDocument document;Need(AnimatorControllerEdits(document).New(error),error.c_str());std::string guid;
    {AnimatorControllerCreation creation;Need(creation.Prepare(project,"Graphs/新規.animfsm",document.Text(),error),error.c_str());guid=Engine::Base::PathToUtf8(creation.Directory().filename());Write(root/"child-guid",guid);
        if(phase==L"metadata")Need(creation.Publish(error,[]{ExitProcess(91);}),error.c_str());
        else Need(creation.Publish(error),error.c_str());}
    ControllerCreationReport report;Need(AnimatorControllerCreationRecovery::Inspect(project,guid,report,error),error.c_str());
    Need(AnimatorControllerCreationRecovery::Restage(project,guid,report,error,[]{ExitProcess(91);}),error.c_str());return 2;
}
struct OwnedChild {
    PROCESS_INFORMATION info{};
    OwnedChild(const std::filesystem::path& root,const wchar_t* phase){
        wchar_t executable[32768]{};Need(GetModuleFileNameW(nullptr,executable,32768)!=0,"Missing child executable");auto command=L"\""+std::wstring(executable)+L"\" --child \""+root.wstring()+L"\" "+phase;
        STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
        Need(CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&info)!=0,"Owned child start failed");
    }
    void Wait(){Need(WaitForSingleObject(info.hProcess,5000)==WAIT_OBJECT_0,"Owned child did not finish");DWORD code=0;Need(GetExitCodeProcess(info.hProcess,&code) && code==91,"Child did not reach intended interruption");}
    ~OwnedChild(){if(info.hProcess){if(WaitForSingleObject(info.hProcess,0)==WAIT_TIMEOUT){TerminateProcess(info.hProcess,92);WaitForSingleObject(info.hProcess,5000);}CloseHandle(info.hThread);CloseHandle(info.hProcess);}}
};
int wmain(int argc,wchar_t** argv){namespace fs=std::filesystem;using namespace Engine::Editor;using namespace Engine::Assets;
    const auto fixture=fs::temp_directory_path()/(L"kubo-controller-create-recovery-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try{
        if(argc==4 && std::wstring(argv[1])==L"--child")return ChildMain(argv[2],argv[3]);
        for(const char* scenario:{"prepared","published","restaged","foreign","replaced-source","invalid-intent","missing-identity","stale-report","throw-archive","occupied-archive","changed-archive-parent","unknown-child","reader","active","restore-failure","restore-foreign","duplicate-guid","late-duplicate","interrupted-metadata","interrupted-restage"}){
            const std::string mode=scenario;const auto root=fixture/scenario;fs::create_directories(root/"Assets/Graphs");Write(root/"entry.kubo",R"({"name":"Creation recovery","assetDirectory":"Assets","startupScene":"entry.scene"})");
            Engine::Base::ProjectContext project;std::string error;Need(project.Open(root/"entry.kubo",error),error.c_str());AnimatorControllerDocument document;Need(AnimatorControllerEdits(document).New(error),error.c_str());
            std::string guid;fs::path directory;const auto path=root/L"Assets/Graphs/新規.animfsm";const auto meta=AssetRegistry::MetadataPath(path);
            if(mode.starts_with("interrupted")){OwnedChild child(root,mode=="interrupted-metadata"?L"metadata":L"restage");child.Wait();guid=Read(root/"child-guid");directory=root/"Assets/.kubo/controller-creations"/guid;}
            else{
                AnimatorControllerCreation creation;Need(creation.Prepare(project,"Graphs/新規.animfsm",document.Text(),error),error.c_str());directory=creation.Directory();guid=Engine::Base::PathToUtf8(directory.filename());
                if(mode=="active"){ControllerCreationReport report;report.intent="unchanged";Need(!AnimatorControllerCreationRecovery::Inspect(project,guid,report,error) && report.intent=="unchanged","Recovery bypassed live creator/changed failed output");continue;}
                if(mode!="prepared")Need(creation.Publish(error),error.c_str());
            }
            const auto intent=Read(directory/"pending.json");ControllerCreationReport report;Need(AnimatorControllerCreationRecovery::Inspect(project,guid,report,error),error.c_str());
            if(mode=="invalid-intent"){Write(directory/"pending.json",std::string(1000,'[')+"0"+std::string(1000,']'));Need(!AnimatorControllerCreationRecovery::Inspect(project,guid,report,error) && Read(path)==document.Text(),"Deep invalid intent admitted/mutated source");continue;}
            if(mode=="missing-identity"){auto value=crude_json::value::parse(intent);value.get<crude_json::object>().erase("sourceIdentity");value["unknownIdentity"]=0.0;Write(directory/"pending.json",value.dump());Need(!AnimatorControllerCreationRecovery::Inspect(project,guid,report,error),"Missing identity admitted");continue;}
            if(mode=="foreign"){Write(path,"foreign source");Need(!AnimatorControllerCreationRecovery::Restage(project,guid,report,error) && Read(path)=="foreign source","Foreign source overwritten");continue;}
            if(mode=="replaced-source"){fs::rename(path,root/"retained-original");Write(path,document.Text());Need(!AnimatorControllerCreationRecovery::Restage(project,guid,report,error) && !fs::exists(directory/"source.stage") && Read(path)==document.Text(),"Same-byte replacement source admitted");continue;}
            if(mode=="stale-report"){report.source=root/"foreign.animfsm";Need(!AnimatorControllerCreationRecovery::Restage(project,guid,report,error) && Read(path)==document.Text(),"Altered report accepted");continue;}
            if(mode=="reader"){
                const auto reader=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);Need(reader!=INVALID_HANDLE_VALUE,"Native foreign reader failed");
                const bool restored=AnimatorControllerCreationRecovery::Restage(project,guid,report,error);CloseHandle(reader);Need(!restored,"Restage bypassed held reader DELETE exclusion");
            }
            if(mode=="restore-failure"){
                Need(!AnimatorControllerCreationRecovery::Restage(project,guid,report,error,[]{throw std::runtime_error("restage injected failure");}) && Read(path)==document.Text() && !fs::exists(directory/"source.stage"),"Restage failure did not roll back published source");
                ControllerCreationReport current;Need(AnimatorControllerCreationRecovery::Inspect(project,guid,current,error) && current==report,"Restage rollback changed identities/state");
            }
            if(mode=="restore-foreign"){
                Need(!AnimatorControllerCreationRecovery::Restage(project,guid,report,error,[&]{Write(path,"foreign owner");throw std::runtime_error("occupied restage rollback");}) && Read(path)=="foreign owner" && Read(directory/"source.stage")==document.Text() && fs::exists(meta),"Restage foreign rollback lost records");
                Need(AnimatorControllerCreationRecovery::Inspect(project,guid,report,error) && report.state==ControllerCreationState::Conflict,"Foreign rollback not Conflict");
                fs::rename(path,root/"foreign-retained");Need(AnimatorControllerCreationRecovery::Inspect(project,guid,report,error) && report.state==ControllerCreationState::MetadataOnly,"Cleared source not MetadataOnly");
            }
            if(mode=="restaged" || mode=="reader" || mode=="restore-failure" || mode=="restore-foreign" || mode.starts_with("interrupted")){
                const auto previous=report;
                if(mode.starts_with("interrupted")){Need(report.state==ControllerCreationState::MetadataOnly,"Abrupt interruption not MetadataOnly");fs::path denied="unchanged";Need(!AnimatorControllerCreationRecovery::ArchiveResolved(project,guid,report,denied,error) && denied=="unchanged" && fs::exists(meta),"MetadataOnly acknowledged before restage");}
                Need(AnimatorControllerCreationRecovery::Restage(project,guid,report,error),error.c_str());
                Need(!fs::exists(path) && !fs::exists(meta) && Read(directory/"source.stage")==document.Text() && fs::exists(directory/"metadata.stage"),"Restage failed to retain source+metadata stages");
                Need(!AnimatorControllerCreationRecovery::Restage(project,guid,previous,error),"Stale restage state accepted");
                Need(AnimatorControllerCreationRecovery::Inspect(project,guid,report,error) && report.state==ControllerCreationState::Prepared && AnimatorControllerCreationRecovery::Restage(project,guid,report,error),"Prepared restage not idempotent");
            }
            if(mode=="duplicate-guid"){Write(root/"Assets/duplicate.animfsm",document.Text());Write(AssetRegistry::MetadataPath(root/"Assets/duplicate.animfsm"),Read(meta));}
            if(mode=="unknown-child")Write(directory/"unknown-child","preserve unknown owner data");
            fs::path archive="unchanged",occupied;
            const auto hook=[&](const fs::path& destination){if(mode=="throw-archive")throw std::runtime_error("archive injection");if(mode=="occupied-archive"){occupied=destination;fs::create_directory(destination);Write(destination/"foreign","archive owner");}if(mode=="late-duplicate"){Write(root/"Assets/duplicate.animfsm",document.Text());Write(AssetRegistry::MetadataPath(root/"Assets/duplicate.animfsm"),Read(meta));}if(mode=="changed-archive-parent")fs::rename(destination.parent_path(),root/"retained-archive-parent");};
            const bool success=AnimatorControllerCreationRecovery::ArchiveResolved(project,guid,report,archive,error,hook);
            if(mode=="throw-archive" || mode=="occupied-archive" || mode=="changed-archive-parent" || mode=="duplicate-guid" || mode=="late-duplicate"){
                Need(!success && archive=="unchanged" && Read(directory/"pending.json")==intent,"Rejected archive changed pending/output");if(mode=="occupied-archive")Need(Read(occupied/"foreign")=="archive owner","Foreign archive overwritten");continue;
            }
            Need(success && archive!="unchanged" && !fs::exists(directory) && Read(archive/"pending.json")==intent,"Resolved creation not archived intact");
            if(report.state==ControllerCreationState::Prepared)Need(Read(archive/"source.stage")==document.Text() && fs::exists(archive/"metadata.stage") && !fs::exists(path),"Prepared abandonment lost revisions");
            else{
                AssetRegistry registry;Need(registry.Scan(root/"Assets",error,AssetRegistry::MetadataPolicy::ReadOnly),error.c_str());AnimatorControllerRevision current;
                {HeldAnimatorControllerAsset reader;Need(reader.Open(project,registry,guid,error),error.c_str());current=*reader.Snapshot();}
                HeldAnimatorControllerWriter writer;Need(writer.Open(project,registry,current,error),"Published acknowledgement did not unblock writer");
            }
            if(mode=="unknown-child")Need(Read(archive/"unknown-child")=="preserve unknown owner data","Unknown child discarded");
            fs::path again="unchanged";Need(!AnimatorControllerCreationRecovery::ArchiveResolved(project,guid,report,again,error) && again=="unchanged","Repeated archive accepted");
        }
        std::cout<<"CREATION_RECOVERY_FIXTURE="<<Engine::Base::PathToUtf8(fixture)<<'\n';
        std::cout<<"PASS: creation Prepared/MetadataOnly/Published/Conflict/held restage+rollback/foreign+stale+reader+active refusal/owned abrupt publication+restage interruption/archive+registration admission/unknown preservation/next writer\n";return 0;
    }catch(const std::exception& exception){std::cerr<<exception.what()<<"\nRetained creation recovery fixture: "<<fixture<<'\n';return 1;}
}
