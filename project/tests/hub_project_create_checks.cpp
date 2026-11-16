#define KUBO_PROJECT_CREATE_TEST_HOOK 1
#include "engine/services/ProjectCreate.h"
#include "engine/services/ExportJob.h"
#include <fstream>
#include <iostream>
#include <thread>

std::thread g_directorySharingRelease;

void Require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void Write(const std::filesystem::path& path,const std::string& bytes){std::ofstream out(path,std::ios::binary);out<<bytes;out.close();Require(!out.fail(),"Fixture write failed");}
std::string Read(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};}
void Crash(const std::filesystem::path& manifest,const wchar_t* mode,DWORD expected){
    wchar_t executable[32768]{};Require(GetModuleFileNameW(nullptr,executable,32768)!=0,"Cannot locate child runner");
    auto command=Engine::Services::ExportJob::QuoteArgument(executable)+L" "+mode+L" "+Engine::Services::ExportJob::QuoteArgument(manifest.wstring());
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    Require(CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)!=FALSE,"Cannot start owned crash child");CloseHandle(process.hThread);
    const auto waited=WaitForSingleObject(process.hProcess,30000);if(waited!=WAIT_OBJECT_0){TerminateProcess(process.hProcess,99);WaitForSingleObject(process.hProcess,5000);}DWORD code=0;const bool got=GetExitCodeProcess(process.hProcess,&code)!=FALSE;CloseHandle(process.hProcess);
    Require(waited==WAIT_OBJECT_0 && got && code==expected,"Owned child missed crash boundary");
}
int wmain(int argc,wchar_t** argv){
    namespace fs=std::filesystem;using namespace Engine::Services;using Engine::Base::ProjectContext;
    if(argc==3){
        if(std::wstring(argv[1])==L"--before")ProjectCreate::BeforePublish=[](const auto&,const auto&){TerminateProcess(GetCurrentProcess(),77);ExitProcess(77);};
        else if(std::wstring(argv[1])==L"--after")ProjectCreate::AfterPublish=[](const auto&){TerminateProcess(GetCurrentProcess(),78);ExitProcess(78);};else return 2;
        ProjectContext context;std::string error;return ProjectCreate::Create(argv[2],context,error)?0:1;
    }
    const auto fixture=fs::temp_directory_path()/(L"kubo-hub-create-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try {
        fs::create_directories(fixture);ProjectContext context;std::string error;Require(context.Create(fixture/"Old","Old",error),error.c_str());const auto old=context.GetProjectFile();
        const auto destination=fixture/L"新規 Project/開始.kubo";Require(ProjectCreate::Create(destination,context,error),error.c_str());
        Require(context.GetProjectFile()==destination && fs::is_regular_file(destination.parent_path()/"Assets/Scenes/Main.scene") && context.GetSettings().name=="開始","Generic staged template/context retarget failed");
        fs::create_directory(fixture/"Empty");Require(ProjectCreate::Create(fixture/"Empty/Empty.kubo",context,error),error.c_str());
        Require(context.Open(old,error),error.c_str());
        // Real Win32 sharing denial, not a mocked return value: a short-lived
        // reader of the stage must not make the user's first creation fail.
        ProjectCreate::BeforePublish=[](const auto&,const auto& stage){
            const auto handle=CreateFileW(stage.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);
            Require(handle!=INVALID_HANDLE_VALUE,"Cannot hold staged directory for sharing check");
            g_directorySharingRelease=std::thread([handle]{Sleep(60);CloseHandle(handle);});
        };
        const bool shared=ProjectCreate::Create(fixture/"Transient/Shared.kubo",context,error);ProjectCreate::BeforePublish=nullptr;
        if(g_directorySharingRelease.joinable())g_directorySharingRelease.join();
        Require(shared && fs::is_regular_file(fixture/"Transient/Shared.kubo"),"Short-lived directory sharing denial was not recovered");
        Require(context.Open(old,error),error.c_str());
        fs::create_directory(fixture/"Occupied");Write(fixture/"Occupied/user.txt","preserve");
        Require(!ProjectCreate::Create(fixture/"Occupied/Game.kubo",context,error) && Read(fixture/"Occupied/user.txt")=="preserve" && !fs::exists(fixture/"Occupied/Assets") && context.GetProjectFile()==old,"Occupied refusal modified output/context");
        fs::create_directory(fixture/"Invalid");Require(!ProjectCreate::Create(fixture/"Invalid/CON.kubo",context,error) && fs::is_empty(fixture/"Invalid") && context.GetProjectFile()==old,"Invalid-name failure published partial project");
        fs::create_directory(fixture/"Rollback");ProjectCreate::BeforePublish=[](const auto&,const auto&){throw std::runtime_error("Injected generic failure");};
        const bool failed=ProjectCreate::Create(fixture/"Rollback/Fail.kubo",context,error);ProjectCreate::BeforePublish=nullptr;
        Require(!failed && fs::is_empty(fixture/"Rollback") && !fs::exists(NativeCreateJournal::Path(fixture/"Rollback")) && context.GetProjectFile()==old,"Failure did not restore empty folder/cleanup journal");
        fs::create_directory(fixture/"Concurrent");ProjectCreate::BeforePublish=[](const auto& root,const auto&){fs::create_directory(root);Write(root/"user.txt","concurrent");};
        const bool concurrent=ProjectCreate::Create(fixture/"Concurrent/Fail.kubo",context,error);ProjectCreate::BeforePublish=nullptr;
        Require(!concurrent && Read(fixture/"Concurrent/user.txt")=="concurrent" && error.find("original output retained at")!=std::string::npos && !fs::exists(fixture/"Concurrent/Assets"),"Concurrent output overwritten or original backup lost");
        fs::create_directory(fixture/"Live");ProjectCreate::BeforePublish=[](const auto& root,const auto&){ProjectContext other;std::string error;Require(!ProjectCreate::Create(root/"Live.kubo",other,error) && error.find("active or")!=std::string::npos,"Generic recovery ignored live journal lock");};
        const bool live=ProjectCreate::Create(fixture/"Live/Live.kubo",context,error);ProjectCreate::BeforePublish=nullptr;Require(live,"Live journal guard broke owning creation");
        Require(context.Open(old,error),error.c_str());
        for(const auto& entry:fs::directory_iterator(fixture))Require(!entry.path().filename().wstring().starts_with(L".kubo-project-stage-"),"Ordinary failed creation leaked stage");
        const auto before=fixture/"CrashBefore/Before.kubo";fs::create_directory(before.parent_path());Crash(before,L"--before",77);
        const auto journal=NativeCreateJournal::Path(before.parent_path());const auto bytes=Read(journal);
        Require(NativeCreateJournal::Recover(before,error)==NativeCreateJournal::Recovery::Attention && Read(journal)==bytes && !fs::exists(before.parent_path()),"Native recovery adopted generic journal");
        auto forged=crude_json::value::parse(bytes);forged["stage"]="../outside";Write(journal,forged.dump());
        Require(!ProjectCreate::Create(before,context,error) && Read(journal)==forged.dump() && context.GetProjectFile()==old,"Forged journal modified output/context");Write(journal,bytes);
        Require(!ProjectCreate::Create(before,context,error) && fs::is_empty(before.parent_path()) && !fs::exists(journal) && error.find("Restored original")!=std::string::npos,"Crash-before recovery did not restore/archive");
        Require(ProjectCreate::Create(before,context,error),error.c_str());
        const auto after=fixture/"CrashAfter/After.kubo";fs::create_directory(after.parent_path());Crash(after,L"--after",78);Write(after.parent_path()/"user.txt","after crash");
        Require(ProjectCreate::Create(after,context,error) && context.GetProjectFile()==after && Read(after.parent_path()/"user.txt")=="after crash" && error.find("already published")!=std::string::npos,"Crash-after adoption regenerated completed project");
        const auto target=fixture/"LinkTarget",link=fixture/"Linked";fs::create_directory(target);
        if(CreateSymbolicLinkW(link.c_str(),target.c_str(),SYMBOLIC_LINK_FLAG_DIRECTORY|SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)) {
            Require(!ProjectCreate::Create(link/"Nested/Game.kubo",context,error) && fs::is_empty(target),"Linked ancestor accepted");fs::remove(link);
            std::cout<<"PASS: linked-parent refusal\n";
        }else std::cout<<"SKIP: directory symlink creation unavailable; linked-parent policy inspected, not executed\n";
        std::error_code cleanup;fs::remove_all(fixture,cleanup);Require(!cleanup,"Owned fixture cleanup failed");
        std::cout<<"PASS: generic staged new/empty creation, prior context/nonempty protection, rollback/concurrent output preservation, real before/after crash recovery, template-kind/traversal refusal and published adoption\n";return 0;
    }catch(const std::exception& failure){std::cerr<<"FAIL: "<<failure.what()<<"; retained fixture "<<fixture<<'\n';return 1;}
}
