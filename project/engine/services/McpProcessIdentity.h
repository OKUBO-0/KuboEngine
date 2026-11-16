#pragma once
#include <Windows.h>

namespace Engine::Services {
struct McpProcessIdentity {
    DWORD pid = 0;
    FILETIME creation{};
    static bool Current(McpProcessIdentity& identity) noexcept {
        FILETIME exit{}, kernel{}, user{};
        identity.pid = GetCurrentProcessId();
        return GetProcessTimes(GetCurrentProcess(), &identity.creation, &exit, &kernel, &user) != FALSE;
    }
};
// Keeps the queried process object alive while connecting. An inaccessible
// process is unknown, not proof of a stale session; PID alone is never identity.
class McpProcessLease final {
public:
    enum class State { Unavailable, Alive, Exited, DifferentProcess };
    McpProcessLease() = default;
    ~McpProcessLease() { Reset(); }
    McpProcessLease(const McpProcessLease&) = delete;
    McpProcessLease& operator=(const McpProcessLease&) = delete;
    void Reset() noexcept {
        if (process_) CloseHandle(process_);
        process_ = nullptr; identity_ = {}; state_ = State::Unavailable;
    }
    State Acquire(const McpProcessIdentity& identity) noexcept {
        Reset(); identity_ = identity;
        if (!identity.pid) return state_;
        process_ = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, identity.pid);
        if (!process_) {
            if (GetLastError() == ERROR_INVALID_PARAMETER) state_ = State::Exited;
            return state_;
        }
        FILETIME creation{}, exit{}, kernel{}, user{};
        if (!GetProcessTimes(process_, &creation, &exit, &kernel, &user)) return state_;
        if (creation.dwHighDateTime != identity.creation.dwHighDateTime ||
            creation.dwLowDateTime != identity.creation.dwLowDateTime) {
            state_ = State::DifferentProcess; return state_;
        }
        const auto wait = WaitForSingleObject(process_, 0);
        state_ = wait == WAIT_TIMEOUT ? State::Alive : wait == WAIT_OBJECT_0 ? State::Exited : State::Unavailable;
        return state_;
    }
    bool IsAlive() const noexcept {
        return state_ == State::Alive && process_ && WaitForSingleObject(process_, 0) == WAIT_TIMEOUT;
    }
    bool OwnsPipe(HANDLE pipe) const noexcept {
        ULONG actual = 0;
        return IsAlive() && GetNamedPipeServerProcessId(pipe, &actual) && actual == identity_.pid && IsAlive();
    }
private:
    HANDLE process_ = nullptr;
    McpProcessIdentity identity_{};
    State state_ = State::Unavailable;
};
}
