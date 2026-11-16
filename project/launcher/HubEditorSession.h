#pragma once

#include <Windows.h>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace KuboHub {

struct CloseWinHandle {
    void operator()(HANDLE handle) const noexcept { CloseHandle(handle); }
};
using UniqueWinHandle = std::unique_ptr<void, CloseWinHandle>;

struct EditorSession {
    UniqueWinHandle process;
    UniqueWinHandle ready;
    std::filesystem::path project;
    std::wstring name;
    ULONGLONG started = 0;
    bool confirmed = false;
    bool delayed = false;
    bool minimizedHub = false;
};

inline DWORD StartEditor(const std::filesystem::path& executable, bool visible, EditorSession& session)
{
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    UniqueWinHandle ready(CreateEventW(&security, TRUE, FALSE, nullptr));
    if (!ready) return GetLastError();

    SIZE_T bytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
    if (!bytes) return GetLastError();
    std::vector<unsigned char> storage(bytes);
    auto attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if (!InitializeProcThreadAttributeList(attributes, 1, 0, &bytes)) return GetLastError();
    const auto release = [](LPPROC_THREAD_ATTRIBUTE_LIST list) { DeleteProcThreadAttributeList(list); };
    const std::unique_ptr<std::remove_pointer_t<LPPROC_THREAD_ATTRIBUTE_LIST>, decltype(release)> ownedAttributes(attributes, release);
    HANDLE inherited = ready.get();
    if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
        &inherited, sizeof(inherited), nullptr, nullptr)) return GetLastError();

    std::wstring command = L"\"" + executable.wstring() + L"\" --project \"" +
        session.project.wstring() + L"\" --hub-ready-event " +
        std::to_wstring(reinterpret_cast<std::uintptr_t>(inherited));
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESHOWWINDOW;
    startup.StartupInfo.wShowWindow = visible ? SW_SHOWNORMAL : SW_HIDE;
    startup.lpAttributeList = attributes;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
        EXTENDED_STARTUPINFO_PRESENT, nullptr, executable.parent_path().c_str(),
        &startup.StartupInfo, &process)) return GetLastError();

    CloseHandle(process.hThread);
    SetHandleInformation(ready.get(),HANDLE_FLAG_INHERIT,0);
    session.process.reset(process.hProcess);
    session.ready = std::move(ready);
    session.started = GetTickCount64();
    if (visible) AllowSetForegroundWindow(process.dwProcessId);
    return ERROR_SUCCESS;
}

} // namespace KuboHub
