#define KUBO_NATIVE_CREATE_TEST_HOOK 1
#include "engine/services/NativeProjectCreate.h"
#include "engine/services/ExportJob.h"
#include <iostream>

void Require(bool condition,const char* reason) {if(!condition) throw std::runtime_error(reason);}
void Write(const std::filesystem::path& path,const std::string& text) {
    std::filesystem::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary);out<<text;
    Require(bool(out),"Fixture write failed");
}
void CrashChild(const std::filesystem::path& manifest,const std::filesystem::path& sdk,const wchar_t* mode,DWORD expected) {
    wchar_t executable[32768]{};Require(GetModuleFileNameW(nullptr,executable,32768)!=0,"Cannot locate crash test executable");
    using Engine::Services::ExportJob;
    auto command=ExportJob::QuoteArgument(executable)+L" "+mode+L" "+ExportJob::QuoteArgument(manifest.wstring())+L" "+ExportJob::QuoteArgument(sdk.wstring());
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    Require(CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)!=FALSE,"Cannot start owned crash process");
    CloseHandle(process.hThread);
    const auto waited=WaitForSingleObject(process.hProcess,30000);DWORD code=0;
    if(waited!=WAIT_OBJECT_0) {TerminateProcess(process.hProcess,99);WaitForSingleObject(process.hProcess,5000);}
    const bool exited=GetExitCodeProcess(process.hProcess,&code)!=FALSE;CloseHandle(process.hProcess);
    Require(waited==WAIT_OBJECT_0 && exited && code==expected,"Crash fixture did not exit at requested publication boundary");
}
int wmain(int argc,wchar_t** argv) {
    if(argc==4) {
        if(std::wstring(argv[1])==L"--crash-before") Engine::Services::NativeProjectCreate::BeforePublish=[](const auto&,const auto&) {TerminateProcess(GetCurrentProcess(),77);ExitProcess(77);};
        else if(std::wstring(argv[1])==L"--crash-after") Engine::Services::NativeProjectCreate::AfterPublish=[](const auto&) {TerminateProcess(GetCurrentProcess(),78);ExitProcess(78);};
        else return 1;
        Engine::Base::ProjectContext context;std::string error;
        Engine::Services::NativeProjectCreate::Create(argv[2],argv[3],context,error);return 2;
    }
    namespace fs=std::filesystem;
    const auto fixture=fs::temp_directory_path()/(L"kubo-native-create-日本語 & '$-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    const auto previous=fs::current_path();
    try {
        const auto sdk=fixture/L"開発 SDK",manifest=fixture/L"Game/日本語.kubo";
        Engine::Base::ProjectContext context;std::string error;
        Require(!Engine::Services::NativeProjectCreate::Create(manifest,sdk,context,error) && !fs::exists(manifest.parent_path()),"Missing SDK created output");
        Write(sdk/"sdk-package.json","{\"format\":\"kubo-runtime-sdk-1\"}");
        for(const auto file:{"KuboRuntime.props","tools/build_native_game.ps1","tools/package_project.ps1","examples/Starter/Starter.cpp","examples/Starter/Starter.vcxproj"}) Write(sdk/file,"trusted test template");
        fs::create_directories(fixture/"Unrelated");fs::current_path(fixture/"Unrelated");
        Require(Engine::Services::NativeProjectCreate::Create(manifest,sdk,context,error),error.c_str());
        Require(context.GetProjectFile()==manifest && fs::is_regular_file(context.ResolvePath(context.GetSettings().startupScene)),"Native creation omitted manifest/startup");
        Require(fs::is_regular_file(manifest.parent_path()/"Native/Starter.cpp") && fs::is_regular_file(manifest.parent_path()/"Native/Starter.vcxproj"),"Native templates missing");
        Engine::Editor::NativeBuildSettings settings;
        Require(Engine::Editor::NativeBuildSettingsFile::Load(manifest.parent_path()/"UserSettings/native-build.json",settings,error) && settings.native && !settings.debug && settings.project=="Native/Starter.vcxproj" && fs::path(std::u8string(settings.sdk.begin(),settings.sdk.end()))==sdk,"Created native settings incorrect");
        const auto createdLock=manifest.parent_path()/"UserSettings/native-build.json.lock";
        std::string createdIgnore;
        Require(fs::is_regular_file(createdLock) && fs::file_size(createdLock)==0 &&
            Engine::Editor::EditorSettingsFile::Read(manifest.parent_path()/".gitignore",{},createdIgnore,error) &&
            createdIgnore.find("UserSettings/native-build.json.lock\n")!=std::string::npos &&
            createdIgnore.find("UserSettings/ControllerLayouts/\n")!=std::string::npos,"Native template omitted empty lock or local settings Git ignores");
        const auto old=context.GetProjectFile();
        const auto noStages=[&] {
            for(const auto& entry:fs::directory_iterator(fixture))
                Require(!entry.path().filename().wstring().starts_with(L".kubo-native-stage-") && !entry.path().filename().wstring().starts_with(L".kubo-empty-backup-"),"Staged creation leaked own directory");
        };
        Require(!Engine::Services::NativeProjectCreate::Create(fixture/"InvalidName/CON.kubo",sdk,context,error) && !fs::exists(fixture/"InvalidName") && context.GetProjectFile()==old,"Late manifest validation left partial user output");
        fs::create_directory(fixture/"EmptyInvalid");
        Require(!Engine::Services::NativeProjectCreate::Create(fixture/"EmptyInvalid/CON.kubo",sdk,context,error) && fs::is_empty(fixture/"EmptyInvalid"),"Failed creation changed existing empty directory");
        fs::create_directory(fixture/"EmptySuccess");
        Require(Engine::Services::NativeProjectCreate::Create(fixture/"EmptySuccess/New.kubo",sdk,context,error) && context.GetRootPath()==fixture/"EmptySuccess", "Existing empty directory publication failed");
        fs::create_directory(fixture/"LockedOutput");
        const auto lock=CreateFileW((fixture/"LockedOutput").c_str(),FILE_LIST_DIRECTORY,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);
        Require(lock!=INVALID_HANDLE_VALUE,"Cannot hold empty output directory");
        const bool lockedCreated=Engine::Services::NativeProjectCreate::Create(fixture/"LockedOutput/Fail.kubo",sdk,context,error);CloseHandle(lock);
        Require(!lockedCreated && fs::is_empty(fixture/"LockedOutput"),"Failed directory publication left user output changed");
        noStages();
        using Engine::Services::NativeProjectCreate;
        fs::create_directory(fixture/"RollbackEmpty");
        NativeProjectCreate::BeforePublish=[](const fs::path&,const fs::path&) {throw std::runtime_error("Injected post-backup publication failure");};
        const bool rollbackCreated=NativeProjectCreate::Create(fixture/"RollbackEmpty/Fail.kubo",sdk,context,error);
        NativeProjectCreate::BeforePublish=nullptr;
        Require(!rollbackCreated && fs::is_directory(fixture/"RollbackEmpty") && fs::is_empty(fixture/"RollbackEmpty"),"Post-backup failure did not restore empty user folder");
        noStages();
        fs::create_directory(fixture/"ConcurrentOutput");
        NativeProjectCreate::BeforePublish=[](const fs::path& output,const fs::path&) {Write(output/"concurrent.txt","do not overwrite");};
        const bool concurrentCreated=NativeProjectCreate::Create(fixture/"ConcurrentOutput/Fail.kubo",sdk,context,error);
        NativeProjectCreate::BeforePublish=nullptr;
        Require(!concurrentCreated && fs::is_regular_file(fixture/"ConcurrentOutput/concurrent.txt") && !fs::exists(fixture/"ConcurrentOutput/Native") && error.find("original output retained at")!=std::string::npos,"Publication overwrote concurrent output or lost backup diagnostic");
        size_t retainedBackups=0;
        for(const auto& entry:fs::directory_iterator(fixture)) if(entry.path().filename().wstring().starts_with(L".kubo-empty-backup-")) {Require(fs::is_empty(entry.path()),"Original empty folder backup changed");++retainedBackups;}
        Require(retainedBackups==1,"Conflicted restore did not retain exactly its original folder");
        std::cout<<"PASS: staged new/empty-root publication, late-validation cleanup, locked-folder preservation, injected rollback and concurrent-output/backup protection\n";
        Require(context.Open(old,error),error.c_str());
        fs::create_directory(fixture/"LiveJournal");
        NativeProjectCreate::BeforePublish=[](const fs::path& output,const fs::path&) {
            Engine::Base::ProjectContext other;std::string error;
            Require(!NativeProjectCreate::Create(output/"Live.kubo",output.parent_path()/L"開発 SDK",other,error) && error.find("active or")!=std::string::npos,"Concurrent recovery did not respect live journal lock");
        };
        const bool liveCreated=NativeProjectCreate::Create(fixture/"LiveJournal/Live.kubo",sdk,context,error);
        NativeProjectCreate::BeforePublish=nullptr;Require(liveCreated,"Live journal lock guard broke owning creation");
        const auto beforeRoot=fixture/"CrashBefore",beforeManifest=beforeRoot/"Before.kubo";
        fs::create_directory(beforeRoot);CrashChild(beforeManifest,sdk,L"--crash-before",77);
        const auto beforeJournal=Engine::Services::NativeCreateJournal::Path(beforeRoot);
        std::string originalJournal;Require(Engine::Editor::EditorSettingsFile::Read(beforeJournal,{},originalJournal,error),error.c_str());
        Require(!fs::exists(beforeRoot),"Child did not stop after empty-folder retirement");
        auto record=crude_json::value::parse(originalJournal);record["stageId"]="999:999:999";
        Write(beforeJournal,record.dump());
        Require(!NativeProjectCreate::Create(beforeManifest,sdk,context,error) && !fs::exists(beforeRoot) && error.find("identity changed")!=std::string::npos,"Mismatched identity recovery moved output");
        record=crude_json::value::parse(originalJournal);record["stage"]="../outside";Write(beforeJournal,record.dump());
        Require(!NativeProjectCreate::Create(beforeManifest,sdk,context,error) && !fs::exists(beforeRoot),"Journal traversal path accepted");
        Write(beforeJournal,originalJournal);
        Require(!NativeProjectCreate::Create(beforeManifest,sdk,context,error) && fs::is_empty(beforeRoot) && !fs::exists(beforeJournal) && error.find("Restored original")!=std::string::npos,"Actual crash recovery did not restore and archive before retry");
        Require(NativeProjectCreate::Create(beforeManifest,sdk,context,error),error.c_str());
        const auto afterRoot=fixture/"CrashAfter",afterManifest=afterRoot/"After.kubo";
        fs::create_directory(afterRoot);CrashChild(afterManifest,sdk,L"--crash-after",78);
        Write(afterRoot/"user-after-crash.txt","preserved");
        Require(NativeProjectCreate::Create(afterManifest,sdk,context,error) && context.GetProjectFile()==afterManifest && error.find("already published")!=std::string::npos && fs::is_regular_file(afterRoot/"user-after-crash.txt") && !fs::exists(Engine::Services::NativeCreateJournal::Path(afterRoot)),"Published crash recovery regenerated or lost completed project");
        std::cout<<"PASS: actual child exits before/after publication, live-journal lock, identity/traversal refusal, empty-folder restoration/retry and completed-project adoption without deleting interrupted data\n";
        fs::create_directory(fixture/"MalformedJournal");
        const auto malformedJournal=Engine::Services::NativeCreateJournal::Path(fixture/"MalformedJournal");
        for(const auto& bad:{std::string(20,'[')+std::string(20,']'),std::string(16385,' '),std::string("{\"format\":1}")}) {
            Write(malformedJournal,bad);
            Require(!NativeProjectCreate::Create(fixture/"MalformedJournal/Bad.kubo",sdk,context,error) && fs::is_empty(fixture/"MalformedJournal"),"Malformed/large/deep journal modified output");
            std::string unchanged;Require(Engine::Editor::EditorSettingsFile::Read(malformedJournal,{},unchanged,error) && unchanged==bad,"Recovery changed rejected journal");
        }
        Require(context.Open(old,error),error.c_str());
        Require(!Engine::Services::NativeProjectCreate::Create(manifest,sdk,context,error) && context.GetProjectFile()==old,"Existing project creation changed context");
        Write(fixture/"Occupied/user.txt","preserve");
        Require(!Engine::Services::NativeProjectCreate::Create(fixture/"Occupied/New.kubo",sdk,context,error) && !fs::exists(fixture/"Occupied/Native"),"Nonempty project folder was modified");
        Require(!Engine::Services::NativeProjectCreate::Create(sdk/"New/Game.kubo",sdk,context,error) && !fs::exists(sdk/"New"),"Creation inside SDK accepted");
        Write(sdk/"sdk-package.json","{\"unrelated\":\"kubo-runtime-sdk-1\"}");
        Require(!Engine::Services::NativeProjectCreate::Create(fixture/"Invalid/Game.kubo",sdk,context,error) && !fs::exists(fixture/"Invalid"),"Wrong SDK format field accepted");
        Write(sdk/"sdk-package.json",std::string(40,'[')+std::string(40,']'));
        Require(!Engine::Services::NativeProjectCreate::Create(fixture/"Deep/Game.kubo",sdk,context,error) && !fs::exists(fixture/"Deep"),"Deep SDK JSON accepted");
        fs::current_path(previous);fs::remove_all(fixture);
        std::cout<<"PASS: native project creation Unicode/cwd isolation, template/settings/startup publication, missing/invalid SDK and occupied/SDK-root refusal, prior context preservation\n";return 0;
    } catch(const std::exception& e) {fs::current_path(previous);std::error_code ignored;fs::remove_all(fixture,ignored);std::cerr<<e.what()<<'\n';return 1;}
}
