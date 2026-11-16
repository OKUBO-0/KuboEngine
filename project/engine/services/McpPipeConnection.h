#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace Engine::Services::McpPipeConnection {
// Re-arming the main-thread Editor endpoint may wait for an in-flight frame.
// Match the separate transfer budget, without replaying any sent request.
inline constexpr DWORD DefaultTimeoutMs=5000;
enum class Phase {Open,Wait,Deadline,Attempts,Connected};
struct Result {HANDLE handle=INVALID_HANDLE_VALUE;DWORD error=0;unsigned attempts=0;Phase phase=Phase::Open;};
inline const char* PhaseName(Phase phase) noexcept {
    switch(phase){case Phase::Wait:return "wait";case Phase::Deadline:return "deadline";case Phase::Attempts:return "attempt-cap";case Phase::Connected:return "connected";default:return "open";}
}
// Before-send only: no request bytes, no reconnect after WriteFile, no replay.
// Generic operations permit deterministic deadline/contention tests.
template<class Operations> Result ConnectWith(Operations& ops,const wchar_t* name,DWORD timeoutMs) {
    Result result;const auto started=ops.Now();
    for(unsigned attempt=0;attempt<64;++attempt) {
        if(attempt && ops.Now()-started>=timeoutMs){result.error=ERROR_SEM_TIMEOUT;result.phase=Phase::Deadline;return result;}
        ++result.attempts;result.handle=ops.Open(name);
        if(result.handle!=INVALID_HANDLE_VALUE){result.phase=Phase::Connected;result.error=0;return result;}
        result.error=ops.Error();result.phase=Phase::Open;
        if(result.error!=ERROR_PIPE_BUSY)return result;
        const auto elapsed=ops.Now()-started;
        if(elapsed>=timeoutMs){result.error=ERROR_SEM_TIMEOUT;result.phase=Phase::Deadline;return result;}
        if(!ops.Wait(name,static_cast<DWORD>(timeoutMs-elapsed))) {
            result.error=ops.Error();result.phase=Phase::Wait;return result;
        }
        // Another client can win the race after Wait reports availability.
    }
    result.error=ERROR_PIPE_BUSY;result.phase=Phase::Attempts;return result;
}
struct NativeOperations {
    ULONGLONG Now() const noexcept{return GetTickCount64();}
    HANDLE Open(const wchar_t* name) const noexcept{return CreateFileW(name,GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);}
    bool Wait(const wchar_t* name,DWORD timeout) const noexcept{return WaitNamedPipeW(name,timeout)!=FALSE;}
    DWORD Error() const noexcept{return GetLastError();}
};
inline Result Connect(const wchar_t* name,DWORD timeoutMs=DefaultTimeoutMs) {
    NativeOperations ops;return ConnectWith(ops,name,timeoutMs);
}
}
