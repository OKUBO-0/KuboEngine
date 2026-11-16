#include "../engine/services/ExportJob.h"
#include <iostream>
#include <vector>

namespace Engine::Services {
struct ExportJobChecks {
    static HANDLE FindLiveCompiler(ExportJob& job) {
        if(!job.job_) return nullptr;
        std::vector<ULONG_PTR> storage(4096);
        auto* list=reinterpret_cast<JOBOBJECT_BASIC_PROCESS_ID_LIST*>(storage.data());
        if(!QueryInformationJobObject(job.job_,JobObjectBasicProcessIdList,list,static_cast<DWORD>(storage.size()*sizeof(ULONG_PTR)),nullptr)) return nullptr;
        for(DWORD i=0;i<list->NumberOfProcessIdsInList;++i) {
            auto process=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,static_cast<DWORD>(list->ProcessIdList[i]));
            if(!process) continue;
            BOOL member=FALSE;wchar_t path[32768]{};DWORD size=32768;
            if(IsProcessInJob(process,job.job_,&member) && member && QueryFullProcessImageNameW(process,0,path,&size) &&
                _wcsicmp(std::filesystem::path(path).filename().c_str(),L"cl.exe")==0 && WaitForSingleObject(process,0)==WAIT_TIMEOUT) return process;
            CloseHandle(process);
        }
        return nullptr;
    }
};
}

void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void Write(const std::filesystem::path& path, const char* text)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output << text;
    if (!output) throw std::runtime_error("Cannot write test fixture");
}
void Wait(Engine::Services::ExportJob& job)
{
    for (int i = 0; i < 600 && job.GetState() == Engine::Services::ExportJob::State::Running; ++i) {
        Sleep(100);
        job.Poll();
    }
    Require(job.GetState() != Engine::Services::ExportJob::State::Running, "Export process exceeded 60 second test limit");
}
struct OwnedHandle {
    HANDLE value=nullptr;
    ~OwnedHandle() {if(value) CloseHandle(value);}
};
struct ControlProcess {
    HANDLE value=nullptr;
    ~ControlProcess() {if(value) {if(WaitForSingleObject(value,0)==WAIT_TIMEOUT) {TerminateProcess(value,42);WaitForSingleObject(value,5000);}CloseHandle(value);}}
};
int wmain(int argc, wchar_t** argv)
{
    using Engine::Services::ExportJob;
    if(argc==2 && std::wstring(argv[1])==L"--control-wait") {Sleep(120000);return 0;}
    if(argc==7 && (std::wstring(argv[1])==L"--cancel-native" || std::wstring(argv[1])==L"--cancel-native-release")) {
        ExportJob nativeJob;
        try {
            const bool debug=std::wstring(argv[1])==L"--cancel-native";
            std::string error;Require(nativeJob.StartNative(argv[4],argv[5],argv[2],argv[3],debug,error),error.c_str());
            OwnedHandle compiler;
            for(int i=0;i<6000 && nativeJob.GetState()==ExportJob::State::Running && !compiler.value;++i) {
                compiler.value=Engine::Services::ExportJobChecks::FindLiveCompiler(nativeJob);
                if(!compiler.value) {Sleep(10);nativeJob.Poll();}
            }
            if(!compiler.value) {if(nativeJob.GetState()==ExportJob::State::Running) {nativeJob.Cancel(error);Wait(nativeJob);}throw std::runtime_error("No live job-owned MSVC compiler observed; this is not a compiler-cancellation pass");}
            std::cout<<"OBSERVED_OWNED_COMPILER_PID="<<GetProcessId(compiler.value)<<'\n';
            Require(nativeJob.Cancel(error),error.c_str());Wait(nativeJob);
            const auto compilerWait=WaitForSingleObject(compiler.value,0);
            Require(nativeJob.GetState()==ExportJob::State::Cancelled && nativeJob.GetExitCode()==ERROR_CANCELLED && compilerWait==WAIT_OBJECT_0,"Actual compiler did not exit before cancellation completion");
            Require(!std::filesystem::exists(std::filesystem::path(argv[5])/"package.json"),"Interrupted compile produced a runtime package");
            std::cout<<"RETAINED_EXPORT_LOG_IN_PROJECT_LIBRARY_LOGS\n";
            std::cout<<"PASS: observed live job-owned MSVC compiler, production native cancellation, compiler handle exit and no published game\n";
            const auto retryOutput=std::filesystem::path(std::wstring(argv[5])+L"-retry");
            Require(nativeJob.StartNative(argv[4],retryOutput,argv[2],argv[6],debug,error),error.c_str());Wait(nativeJob);
            Require(nativeJob.GetState()==ExportJob::State::Succeeded && !nativeJob.IsCancellationRequested() &&
                std::filesystem::is_regular_file(retryOutput/"native-build.json") && std::filesystem::is_regular_file(retryOutput/"package.json"),"Same service could not build/export a valid SDK game after actual compiler cancellation");
            std::cout<<"PASS: same ExportJob restarts a real SDK-native build and publishes a game after compiler cancellation\n";
            return 0;
        } catch(const std::exception& e) {
            std::cerr<<e.what()<<'\n'<<nativeJob.ReadLog();
            if(nativeJob.GetState()==ExportJob::State::Running) {std::string error;if(nativeJob.Cancel(error)) Wait(nativeJob);}
            return 1;
        }
    }
    if (argc == 6) {
        try {
            ExportJob nativeJob; std::string error;
            Require(nativeJob.StartNative(argv[3], argv[4], argv[1], argv[2], std::wstring(argv[5]) == L"Debug", error), error.c_str());
            Require(!nativeJob.StartNative(argv[3], argv[4], argv[1], argv[2], false, error), "Concurrent native job accepted");
            Wait(nativeJob);
            std::cout << nativeJob.ReadLog();
            Require(nativeJob.GetState() == ExportJob::State::Succeeded, "Real native export job failed");
            return 0;
        } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
    }
    const auto fixture = std::filesystem::temp_directory_path() /
        (L"kubo-export-job-日本語 & '$-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    ExportJob job;
    try {
        const auto project = fixture / "Project/Sample.kubo";
        const auto runtime = fixture / "Engine/KuboPlayer.exe";
        const auto script = std::filesystem::absolute("tools/package_project.ps1");
        const auto output = fixture / "Export";
        Write(project, "{\"name\":\"Sample\",\"engineVersion\":\"0.1.0\",\"startupScene\":\"entry.scene\",\"assetDirectory\":\"Content\"}");
        Write(project.parent_path() / "entry.scene", "{\"version\":1,\"entities\":[]}");
        Write(project.parent_path() / "Content/asset.txt", "source asset");
        Write(runtime, "not an executable: packaging test fixture");
        Write(runtime.parent_path() / "dxcompiler.dll", "fixture");
        Write(runtime.parent_path() / "dxil.dll", "fixture");
        Write(runtime.parent_path() / "Resources/Shaders/test.hlsl", "fixture");
        Write(runtime.parent_path() / "Resources/textures/skybox/test.dds", "fixture");
        Write(runtime.parent_path() / "Resources/textures/common/white.png", "fixture");
        Write(runtime.parent_path() / "ThirdPartyLicenses/Lua-5.4.9.txt", "fixture");
        std::string error;
        Require(!job.Start(project, output, fixture / "missing.ps1", runtime, error), "Missing script accepted");
        Require(!error.empty() && job.GetState() == ExportJob::State::Idle, "Start failure changed job state");
        Require(job.Start(project, output, script, runtime, error), error.c_str());
        Require(job.GetState() == ExportJob::State::Running, "Export was not asynchronous");
        Require(!job.Start(project, output, script, runtime, error), "Concurrent export was accepted");
        Wait(job);
        Require(job.GetState() == ExportJob::State::Succeeded && job.GetExitCode() == 0, job.ReadLog().c_str());
        Require(std::filesystem::is_regular_file(output / "KuboPlayer.exe") &&
            std::filesystem::is_regular_file(output / "Content/asset.txt") &&
            std::filesystem::is_regular_file(output / "package.json"), "Async export omitted package files");
        Require(job.ReadLog().find("Kubo runtime package created:") != std::string::npos, "Child output was not captured");
        Require(job.Start(project, output, script, runtime, error), error.c_str());
        Wait(job);
        Require(job.GetState() == ExportJob::State::Failed && job.GetExitCode() != 0,
            "Nonempty-output failure was not reported");
        Require(job.ReadLog().find("OutputDirectory must be empty") != std::string::npos,
            "Failure log did not capture the packager error");
        std::ifstream asset(output / "Content/asset.txt");
        std::string text;
        std::getline(asset, text);
        Require(text == "source asset", "Rejected export overwrote existing files");
        asset.close();
        const auto sdk = fixture / "SDK & '$";
        const auto consumer = fixture / "Native & '$/Game.vcxproj";
        Write(sdk / "sdk-package.json", "{}");
        Write(consumer, "test-only native input");
        Require(!job.StartNative(project, fixture / "NativeExport", sdk, consumer, false, error), "Missing native builder accepted");
        Write(sdk / "tools/build_native_game.ps1",
            "param($ProjectFile,$OutputDirectory,$Configuration,$NativeProject,$SdkDirectory)\n"
            "$ErrorActionPreference='Stop'\n"
            "if (!(Test-Path -LiteralPath $ProjectFile) -or !(Test-Path -LiteralPath $NativeProject) -or !(Test-Path -LiteralPath (Join-Path $SdkDirectory 'sdk-package.json'))) {throw 'argument transport failed'}\n"
            "if ($Configuration -eq 'Release') {[Console]::Error.WriteLine('intentional native build failure');exit 7}\n"
            "New-Item -ItemType Directory -Path $OutputDirectory | Out-Null\n"
            "Write-Output 'native Debug argument transport verified'\n");
        Require(job.StartNative(project, fixture / "NativeExport", sdk, consumer, true, error), error.c_str());
        Require(!job.Start(project, output, script, runtime, error), "Concurrent prebuilt export accepted during native build");
        Wait(job);
        Require(job.GetState() == ExportJob::State::Succeeded && job.ReadLog().find("native Debug argument transport verified") != std::string::npos,
            "Native async argument transport/log capture failed");
        Require(job.StartNative(project, fixture / "FailedNative", sdk, consumer, false, error), error.c_str());
        Wait(job);
        Require(job.GetState() == ExportJob::State::Failed && job.GetExitCode() == 7 && !std::filesystem::exists(fixture / "FailedNative"),
            "Native failure isolation/exit propagation failed");
        const auto waitingBuilder=sdk/"tools/build_native_game.ps1";
        Write(waitingBuilder,R"PS(param($ProjectFile,$OutputDirectory,$Configuration,$NativeProject,$SdkDirectory,$RuntimeExecutable)
$ErrorActionPreference='Stop'
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
[IO.File]::WriteAllText((Join-Path $OutputDirectory 'partial.txt'),'retained partial output')
$child=Start-Process -FilePath (Join-Path $PSHOME 'powershell.exe') -ArgumentList @('-NoProfile','-File',('"'+(Join-Path $PSScriptRoot 'child.ps1')+'"'),'-Directory',('"'+$OutputDirectory+'"')) -WindowStyle Hidden -PassThru
[IO.File]::WriteAllText((Join-Path $OutputDirectory 'child.pid'),[string]$child.Id)
Write-Output 'waiting for owned child and grandchild'
while(!$child.HasExited) {Start-Sleep -Milliseconds 100}
)PS");
        Write(sdk/"tools/child.ps1",R"PS(param($Directory)
$ErrorActionPreference='Stop'
$grand=Start-Process -FilePath (Join-Path $PSHOME 'powershell.exe') -ArgumentList @('-NoProfile','-Command','Start-Sleep -Seconds 120') -WindowStyle Hidden -PassThru
[IO.File]::WriteAllText((Join-Path $Directory 'grand.pid'),[string]$grand.Id)
while(!$grand.HasExited) {Start-Sleep -Milliseconds 100}
)PS");
        wchar_t executable[32768]{};Require(GetModuleFileNameW(nullptr,executable,32768)!=0,"Cannot locate control process executable");
        auto controlCommand=ExportJob::QuoteArgument(executable)+L" --control-wait";
        STARTUPINFOW controlStartup{};controlStartup.cb=sizeof(controlStartup);PROCESS_INFORMATION controlInfo{};
        Require(CreateProcessW(executable,controlCommand.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&controlStartup,&controlInfo)!=FALSE,"Cannot create owned unrelated control process");
        CloseHandle(controlInfo.hThread);ControlProcess control;control.value=controlInfo.hProcess;
        for(bool native:{true,false}) {
            const auto cancelledOutput=fixture/(native ? "CancelledNative" : "CancelledPrebuilt");
            const bool started=native ? job.StartNative(project,cancelledOutput,sdk,consumer,false,error) : job.Start(project,cancelledOutput,waitingBuilder,runtime,error);
            Require(started,error.c_str());
            for(int i=0;i<150 && !std::filesystem::is_regular_file(cancelledOutput/"grand.pid");++i) {Sleep(100);job.Poll();}
            Require(std::filesystem::is_regular_file(cancelledOutput/"grand.pid"),"Waiting exporter did not start grandchild");
            DWORD childId=0,grandId=0;
            {std::ifstream input(cancelledOutput/"child.pid");input>>childId;Require(bool(input),"Missing child PID");}
            {std::ifstream input(cancelledOutput/"grand.pid");input>>grandId;Require(bool(input),"Missing grandchild PID");}
            OwnedHandle child,grand;
            child.value=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,childId);
            grand.value=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,grandId);
            Require(child.value && grand.value && WaitForSingleObject(child.value,0)==WAIT_TIMEOUT && WaitForSingleObject(grand.value,0)==WAIT_TIMEOUT,"Owned descendants not live before cancel");
            Require(job.Cancel(error),error.c_str());Wait(job);
            const auto childWait=WaitForSingleObject(child.value,0),grandWait=WaitForSingleObject(grand.value,0);
            if(job.GetState()!=ExportJob::State::Cancelled || job.GetExitCode()!=ERROR_CANCELLED || childWait!=WAIT_OBJECT_0 || grandWait!=WAIT_OBJECT_0)
                throw std::runtime_error("Cancel completion mismatch: state="+std::to_string(static_cast<int>(job.GetState()))+
                    " exit="+std::to_string(job.GetExitCode())+" childWait="+std::to_string(childWait)+" grandWait="+std::to_string(grandWait));
            Require(WaitForSingleObject(control.value,0)==WAIT_TIMEOUT,"Cancel stopped unrelated control process");
            Require(std::filesystem::is_regular_file(cancelledOutput/"partial.txt"),"Cancellation deleted partial user output");
            Require(!job.Cancel(error),"Completed cancellation accepted as a new running cancellation");
        }
        Require(job.Start(project,fixture/"AfterCancel",script,runtime,error),error.c_str());Wait(job);
        Require(job.GetState()==ExportJob::State::Succeeded && !job.IsCancellationRequested(),"Successful restart after cancellation failed");
        std::cout<<"PASS: native/prebuilt cancel waits for child/grandchild job exit, preserves unrelated process/partial output and permits clean restart\n";
        std::filesystem::remove_all(fixture);
        std::cout << "PASS: async export, Unicode/metacharacter paths, log capture, concurrent rejection and failure isolation\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        if (job.GetState() != ExportJob::State::Running) std::filesystem::remove_all(fixture);
        return 1;
    }
}
