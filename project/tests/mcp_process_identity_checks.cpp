#include "../engine/services/McpProcessIdentity.h"
#include "../engine/services/EditorMcpSession.h"
#include <iostream>
#include <string>
#include <stdexcept>

static void Require(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
struct Handle {
    HANDLE value = nullptr;
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
struct Child {
    PROCESS_INFORMATION process{};
    ~Child() {
        if (process.hProcess) {
            if (WaitForSingleObject(process.hProcess, 0) != WAIT_OBJECT_0) {
                TerminateProcess(process.hProcess, 77); WaitForSingleObject(process.hProcess, 5000);
            }
            CloseHandle(process.hProcess);
        }
        if (process.hThread) CloseHandle(process.hThread);
    }
};
struct PendingConnect {
    HANDLE server;
    Handle event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    OVERLAPPED operation{};
    bool pending = false;
    explicit PendingConnect(HANDLE pipe) : server(pipe) {
        Require(event.value != nullptr, "Cannot create native accept event");
        operation.hEvent = event.value;
        if (!ConnectNamedPipe(server, &operation)) {
            const auto error = GetLastError(); pending = error == ERROR_IO_PENDING;
            Require(pending || error == ERROR_PIPE_CONNECTED, "Cannot rearm native accept");
        }
    }
    ~PendingConnect() {
        if (pending) { CancelIoEx(server, &operation); DWORD count = 0; GetOverlappedResult(server, &operation, &count, TRUE); }
    }
};
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc == 4 && std::wstring(argv[1]) == L"--child") {
            Handle ready{OpenEventW(EVENT_MODIFY_STATE, FALSE, argv[2])};
            Handle stop{OpenEventW(SYNCHRONIZE, FALSE, argv[3])};
            if (!ready.value || !stop.value || !SetEvent(ready.value)) return 2;
            return WaitForSingleObject(stop.value, 10000) == WAIT_OBJECT_0 ? 0 : 3;
        }
        Require(argc == 1, "No other arguments accepted");
        using namespace Engine::Services;
        McpProcessIdentity self;
        Require(McpProcessIdentity::Current(self), "Cannot capture current creation time");
        McpProcessLease own;
        Require(own.Acquire(self) == McpProcessLease::State::Alive && own.IsAlive(), "Same live process refused");
        auto mismatch = self; mismatch.creation.dwLowDateTime ^= 1;
        McpProcessLease different;
        Require(different.Acquire(mismatch) == McpProcessLease::State::DifferentProcess && !different.IsAlive(), "Creation mismatch accepted");
        Require(different.Acquire({}) == McpProcessLease::State::Unavailable, "PID0 misclassified as stale");
        const auto suffix = std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
        const auto readyName = L"Local\\KuboMcpIdentityReady-" + suffix;
        const auto stopName = L"Local\\KuboMcpIdentityStop-" + suffix;
        Handle ready{CreateEventW(nullptr, TRUE, FALSE, readyName.c_str())};
        Handle stop{CreateEventW(nullptr, TRUE, FALSE, stopName.c_str())};
        Require(ready.value && stop.value, "Child events failed");
        wchar_t executable[32768]{};
        Require(GetModuleFileNameW(nullptr, executable, _countof(executable)) != 0, "Cannot locate owned executable");
        auto command = L"\"" + std::wstring(executable) + L"\" --child " + readyName + L" " + stopName;
        STARTUPINFOW startup{}; startup.cb = sizeof(startup); Child child;
        Require(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
            nullptr, nullptr, &startup, &child.process), "Cannot start owned child");
        Require(WaitForSingleObject(ready.value, 5000) == WAIT_OBJECT_0, "Owned child not ready");
        McpProcessIdentity childIdentity; childIdentity.pid = child.process.dwProcessId;
        FILETIME exit{}, kernel{}, user{};
        Require(GetProcessTimes(child.process.hProcess, &childIdentity.creation, &exit, &kernel, &user), "Cannot capture child creation");
        McpProcessLease childLease;
        Require(childLease.Acquire(childIdentity) == McpProcessLease::State::Alive, "Live child refused");
        wchar_t pipeName[128]{};
        _snwprintf_s(pipeName, _countof(pipeName), _TRUNCATE, L"\\\\.\\pipe\\KuboEditor-%016llx%016llx",
            static_cast<unsigned long long>(GetCurrentProcessId()), GetTickCount64());
        Handle server{CreateNamedPipeW(pipeName, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_REJECT_REMOTE_CLIENTS, 1, 4096, 4096, 0, nullptr)};
        Require(server.value != INVALID_HANDLE_VALUE, "Real native pipe creation failed");
        Handle client{CreateFileW(pipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr)};
        Require(client.value != INVALID_HANDLE_VALUE, "Real native pipe open failed");
        Require(own.OwnsPipe(client.value), "Kernel reported wrong current server owner");
        Require(!childLease.OwnsPipe(client.value), "Different live process accepted as pipe owner");
        CloseHandle(client.value); client.value = nullptr;
        Require(DisconnectNamedPipe(server.value), "Cannot reset native pipe fixture");
        PendingConnect accepting(server.value);
        const auto fakeRoot = std::filesystem::temp_directory_path() / (L"kubo-mcp-owner-binding-" + suffix);
        Require(!std::filesystem::exists(fakeRoot), "Fresh owner-binding fixture required");
        const auto descriptor = EditorMcpSession::Descriptor(fakeRoot);
        std::filesystem::create_directories(descriptor.parent_path());
        EditorMcpSession::Value metadata;
        metadata["format"] = "kubo-editor-mcp-2";
        std::string asciiPipe; for (const auto character : std::wstring(pipeName)) asciiPipe += static_cast<char>(character);
        metadata["pipe"] = asciiPipe; metadata["pid"] = double(childIdentity.pid);
        metadata["process_creation_high"] = double(childIdentity.creation.dwHighDateTime);
        metadata["process_creation_low"] = double(childIdentity.creation.dwLowDateTime);
        { std::ofstream output(descriptor, std::ios::binary); output << metadata.dump(); Require(bool(output), "Cannot publish owned mismatch fixture"); }
        bool endpointRefused = false;
        try { EditorMcpSession::Call(fakeRoot, EditorMcpSession::Value::parse(R"({"id":1,"method":"scene.create_entity","params":{"name":"Must not send"}})")); }
        catch (const std::exception& error) {
            std::cout << "MCP_CALL_OWNER_DIAGNOSTIC=" << error.what() << '\n';
            endpointRefused = std::string(error.what()) == "Editor MCP endpoint is not owned by the published live process";
        }
        Require(endpointRefused && childLease.IsAlive(), "Actual Call did not refuse the different live pipe server");
        std::cout << "MCP_CALL_OWNER_REFUSAL actual_kernel_owner=1 different_live_publisher=1 refused_before_transfer=1 retained_fixture=" << fakeRoot << '\n';
        Require(SetEvent(stop.value) && WaitForSingleObject(child.process.hProcess, 5000) == WAIT_OBJECT_0, "Owned child did not stop");
        DWORD childExit = 1; Require(GetExitCodeProcess(child.process.hProcess, &childExit) && childExit == 0, "Owned child failed");
        Require(!childLease.IsAlive() && !childLease.OwnsPipe(client.value), "Exited process accepted");
        McpProcessLease dead;
        Require(dead.Acquire(childIdentity) == McpProcessLease::State::Exited, "Known exited process not classified");
        std::cout << "PASS: actual process creation identity, live/changed/exited/PID0 classification, real kernel pipe-owner match/mismatch, held process lease and owned child exit0 (not forced PID reuse)\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
