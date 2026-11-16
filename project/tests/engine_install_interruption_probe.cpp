#define NOMINMAX
#include <Windows.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void Need(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
struct Handle {HANDLE value=nullptr;~Handle(){if(value && value!=INVALID_HANDLE_VALUE)CloseHandle(value);}};
struct Child {
    Handle process,thread;bool suspended=false;
    ~Child(){if(process.value){DWORD exit=0;if(GetExitCodeProcess(process.value,&exit) && exit==STILL_ACTIVE){TerminateProcess(process.value,0xE0510002);WaitForSingleObject(process.value,10000);}}}
};
std::wstring Quote(const std::wstring& text){
    std::wstring result=L"\"";size_t slash=0;
    for(auto c:text){if(c==L'\\'){++slash;continue;}result.append(slash*(c==L'"'?2:1),L'\\');slash=0;if(c==L'"')result+=L'\\';result+=c;}
    result.append(slash*2,L'\\');result+=L'"';return result;
}
bool Hex(const std::wstring& value,size_t count){if(value.size()!=count)return false;for(auto c:value)if(!((c>=L'0' && c<=L'9') || (c>=L'a' && c<=L'f')))return false;return true;}
std::wstring Candidate(const std::filesystem::path& store){
    WIN32_FIND_DATAW data{};const auto find=FindFirstFileW((store/L".pending-*").c_str(),&data);if(find==INVALID_HANDLE_VALUE)return {};
    std::wstring token;
    do{const std::wstring name=data.cFileName;if((data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) && !(data.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT) && name.starts_with(L".pending-") && Hex(name.substr(9),32)){token=name.substr(9);break;}}while(FindNextFileW(find,&data));
    FindClose(find);return token;
}
}
int wmain(int argc,wchar_t** argv){try{
    Need(argc==5,"Use probe <trusted KuboLauncher.exe> <fixture manifest> <fixture store> <expected manifest SHA-256>");
    const auto launcher=std::filesystem::absolute(argv[1]).lexically_normal(),manifest=std::filesystem::absolute(argv[2]).lexically_normal(),store=std::filesystem::absolute(argv[3]).lexically_normal();const std::wstring hash=argv[4];
    Need(launcher.filename()==L"KuboLauncher.exe" && Hex(hash,64),"Invalid owned-probe inputs");
    // Only this fresh test namespace is admitted; no arbitrary existing install is killed.
    Need(store.filename().wstring().starts_with(L"InterruptedStore-") && store.parent_path().filename().wstring().starts_with(L"kubo-install-interrupt-"),"Interruption requires the owned fixture namespace");
    wchar_t temporary[32768]{};const auto length=GetTempPathW(32768,temporary);Need(length && length<32768,"Cannot resolve test temporary directory");
    const auto fixture=store.parent_path();const auto temp=std::filesystem::path(temporary).lexically_normal().native();const auto path=fixture.native();
    Need(path.size()>temp.size() && CompareStringOrdinal(path.data(),static_cast<int>(temp.size()),temp.data(),static_cast<int>(temp.size()),TRUE)==CSTR_EQUAL,"Fixture is not under the system temp directory");
    Need(SetEnvironmentVariableW(L"APPDATA",(fixture/L"AppData").c_str()) && SetEnvironmentVariableW(L"LOCALAPPDATA",(fixture/L"LocalAppData").c_str()),"Cannot isolate child profile");
    std::wstring line=Quote(launcher.native())+L" --install-package "+Quote(manifest.native())+L" --store "+Quote(store.native())+L" --accept-local-package --no-launch";
    STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;PROCESS_INFORMATION info{};
    Child child;
    Need(CreateProcessW(launcher.c_str(),line.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED|CREATE_NO_WINDOW,nullptr,fixture.c_str(),&startup,&info),"Cannot create owned installer child");
    child.process.value=info.hProcess;child.thread.value=info.hThread;
    std::cout<<"INTERRUPTION_CHILD_STARTED_PID="<<info.dwProcessId<<std::endl;
    Need(ResumeThread(child.thread.value)!=DWORD(-1),"Cannot run owned child");
    const auto deadline=GetTickCount64()+30000;
    while(GetTickCount64()<deadline){
        DWORD exit=0;Need(GetExitCodeProcess(child.process.value,&exit),"Cannot inspect owned child");
        if(exit!=STILL_ACTIVE){std::cout<<"INTERRUPTION_NOT_OBSERVED CHILD_PID="<<info.dwProcessId<<" CHILD_EXIT="<<exit<<'\n';return exit==0?3:1;}
        const auto token=Candidate(store);
        if(!token.empty()){
            // This is the known primary thread returned by our CreateProcess,
            // not a thread inferred from a title/PID inventory. CLI install runs
            // synchronously there. Suspend only to inspect a stable phase, then
            // deliberately terminate this one owned test child.
            const auto previous= SuspendThread(child.thread.value);
            if(previous==DWORD(-1)){Sleep(1);continue;}child.suspended=true;
            const auto pending=store/(L".pending-"+token),final=store/(L"package-"+hash),journal=store/(L"pending-"+token+L".json");
            if(GetFileAttributesW(pending.c_str())==INVALID_FILE_ATTRIBUTES || GetFileAttributesW(final.c_str())!=INVALID_FILE_ATTRIBUTES){
                Need(ResumeThread(child.thread.value)!=DWORD(-1),"Cannot retire late suspension");child.suspended=false;
                Need(WaitForSingleObject(child.process.value,30000)==WAIT_OBJECT_0,"Late child did not finish");Need(GetExitCodeProcess(child.process.value,&exit),"Missing late child exit");
                std::cout<<"INTERRUPTION_NOT_OBSERVED CHILD_PID="<<info.dwProcessId<<" CHILD_EXIT="<<exit<<'\n';return exit==0?3:1;
            }
            Need(GetFileAttributesW(journal.c_str())!=INVALID_FILE_ATTRIBUTES,"Candidate lacks durable journal");
            Need(TerminateProcess(child.process.value,0xE0510001),"Cannot terminate owned interruption child");
            Need(WaitForSingleObject(child.process.value,10000)==WAIT_OBJECT_0 && GetExitCodeProcess(child.process.value,&exit) && exit==0xE0510001,"Missing expected held child terminal exit");
            std::wcout<<L"INTERRUPTED_CANDIDATE_TOKEN="<<token<<L" CHILD_PID="<<info.dwProcessId<<L" CHILD_EXIT="<<exit<<L" PRIMARY_THREAD_ID="<<info.dwThreadId<<L" PREVIOUS_SUSPEND_COUNT="<<previous<<L"\n";return 0;
        }
        Sleep(1);
    }
    throw std::runtime_error("Live owned installer observation deadline expired; do not restart it as if it completed");
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
