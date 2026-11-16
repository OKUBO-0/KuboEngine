#include "engine/services/McpPipeConnection.h"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace Engine::Services::McpPipeConnection;
static void Need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
struct Fake {
    ULONGLONG clock=0;DWORD error=0,waitError=0;unsigned opens=0,waits=0;bool waitOk=true;DWORD step=1;
    std::vector<DWORD> failures;
    ULONGLONG Now()const{return clock;}
    DWORD Error()const{return error;}
    HANDLE Open(const wchar_t*){error=opens<failures.size()?failures[opens]:0;++opens;return error?INVALID_HANDLE_VALUE:reinterpret_cast<HANDLE>(static_cast<uintptr_t>(1));}
    bool Wait(const wchar_t*,DWORD remaining){Need(remaining>0,"Unbounded zero wait used");++waits;clock+=step;if(!waitOk)error=waitError;return waitOk;}
};
struct Handle {HANDLE value=INVALID_HANDLE_VALUE;~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}};
int main(){try{
    Fake direct;auto r=ConnectWith(direct,L"fixture",1000);Need(r.phase==Phase::Connected && r.attempts==1 && direct.waits==0,"Direct connection changed");
    Fake raced;raced.failures={ERROR_PIPE_BUSY,ERROR_PIPE_BUSY};r=ConnectWith(raced,L"fixture",1000);Need(r.phase==Phase::Connected && r.attempts==3 && raced.waits==2,"Post-wait race was not handled");
    Fake denied;denied.failures={ERROR_ACCESS_DENIED};r=ConnectWith(denied,L"fixture",1000);Need(r.error==ERROR_ACCESS_DENIED && r.phase==Phase::Open && denied.waits==0,"Non-busy error retried");
    Fake timed;timed.failures={ERROR_PIPE_BUSY};timed.waitOk=false;timed.waitError=ERROR_SEM_TIMEOUT;r=ConnectWith(timed,L"fixture",1000);Need(r.phase==Phase::Wait && r.error==ERROR_SEM_TIMEOUT && timed.opens==1,"Failed wait discarded diagnostic or reopened");
    Fake deadline;deadline.failures={ERROR_PIPE_BUSY};deadline.step=1000;r=ConnectWith(deadline,L"fixture",1000);Need(r.phase==Phase::Deadline && deadline.opens==1,"Deadline allowed another open");
    Fake bounded;bounded.failures.assign(64,ERROR_PIPE_BUSY);bounded.step=0;r=ConnectWith(bounded,L"fixture",1000);Need(r.phase==Phase::Attempts && bounded.opens==64 && r.error==ERROR_PIPE_BUSY,"Immediate race spinning unbounded");
    Fake zero;zero.failures={ERROR_PIPE_BUSY};r=ConnectWith(zero,L"fixture",0);Need(r.phase==Phase::Deadline && zero.waits==0,"Zero timeout waited");
    Fake slowBefore;slowBefore.failures={ERROR_PIPE_BUSY};slowBefore.step=1500;
    r=ConnectWith(slowBefore,L"fixture",1000);Need(r.phase==Phase::Deadline && slowBefore.opens==1,"Old one-second boundary fixture did not refuse late availability");
    Fake slowNow;slowNow.failures={ERROR_PIPE_BUSY};slowNow.step=1500;
    r=ConnectWith(slowNow,L"fixture",DefaultTimeoutMs);Need(r.phase==Phase::Connected && slowNow.opens==2 && slowNow.waits==1,"Late before-send availability was not admitted within default budget");
    Fake exhausted;exhausted.failures={ERROR_PIPE_BUSY};exhausted.step=DefaultTimeoutMs;
    r=ConnectWith(exhausted,L"fixture",DefaultTimeoutMs);Need(r.phase==Phase::Deadline && exhausted.opens==1,"Default deadline allowed late open");
    const auto name=L"\\\\.\\pipe\\KuboConnectCheck-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
    Handle server,blocker;
    server.value=CreateNamedPipeW(name.c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,PIPE_TYPE_MESSAGE|PIPE_READMODE_MESSAGE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,4096,4096,0,nullptr);
    Need(server.value!=INVALID_HANDLE_VALUE,"Native fixture pipe failed");
    blocker.value=CreateFileW(name.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);Need(blocker.value!=INVALID_HANDLE_VALUE,"Native blocker failed");
    r=Connect(name.c_str(),25);Need(r.handle==INVALID_HANDLE_VALUE && r.phase==Phase::Wait && r.error==ERROR_SEM_TIMEOUT,"Real occupied pipe did not preserve wait timeout");
    CloseHandle(blocker.value);blocker.value=INVALID_HANDLE_VALUE;Need(DisconnectNamedPipe(server.value)!=FALSE,"Native fixture disconnect failed");
    Handle event;event.value=CreateEventW(nullptr,TRUE,FALSE,nullptr);Need(event.value!=nullptr,"Native fixture event failed");
    OVERLAPPED listening{};listening.hEvent=event.value;
    Need(!ConnectNamedPipe(server.value,&listening) && GetLastError()==ERROR_IO_PENDING,"Native fixture did not rearm listener");
    r=Connect(name.c_str(),1000);Handle admitted;admitted.value=r.handle;DWORD connected=0;
    if(r.handle==INVALID_HANDLE_VALUE){CancelIoEx(server.value,&listening);GetOverlappedResult(server.value,&listening,&connected,TRUE);}
    Need(r.phase==Phase::Connected && r.handle!=INVALID_HANDLE_VALUE,"Native connection recovery failed");
    Need(GetOverlappedResult(server.value,&listening,&connected,TRUE)!=FALSE,"Native listener completion failed");
    DWORD available=99;Need(PeekNamedPipe(server.value,nullptr,0,nullptr,&available,nullptr)!=FALSE && available==0,"Connector wrote request bytes");
    std::cout<<"PASS: MCP before-send pipe direct/busy availability race/strict deadline+attempt cap/nonbusy+wait diagnostics/native occupied timeout+recovery/no request bytes; late1500ms availability admitted within bounded default5000ms, former1000ms refuses, exhausted default refuses\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
