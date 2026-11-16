#include "../engine/assets/AssetMove.h"
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
namespace fs = std::filesystem;
using namespace Engine::Assets;
bool ownedChildrenJoined = true;
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void Write(const fs::path& path, const std::string& bytes) { std::ofstream file(path, std::ios::binary); file << bytes; Require(bool(file), "Fixture write failed"); }
std::string Read(const fs::path& path) { std::ifstream file(path, std::ios::binary); Require(bool(file), "Fixture read failed"); return {std::istreambuf_iterator<char>(file), {}}; }
struct Handle {
    HANDLE value = nullptr;
    ~Handle() { if (value) CloseHandle(value); }
};
struct Child {
    PROCESS_INFORMATION process{};
    HANDLE job = nullptr;
    ~Child() {
        if (process.hProcess) {
            if (WaitForSingleObject(process.hProcess, 0) != WAIT_OBJECT_0) {
                TerminateProcess(process.hProcess, 91);
                if (WaitForSingleObject(process.hProcess, 10000) != WAIT_OBJECT_0) ownedChildrenJoined = false;
            }
            CloseHandle(process.hProcess);
        }
        if (process.hThread) CloseHandle(process.hThread);
        if (job) CloseHandle(job);
    }
    void Start(const fs::path& exe, std::wstring arguments) {
        std::wstring command = L"\"" + exe.wstring() + L"\" " + arguments;
        job = CreateJobObjectW(nullptr, nullptr);
        Require(job != nullptr, "Owned child job creation failed");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        Require(SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) != FALSE,
            "Owned child job lifetime configuration failed");
        STARTUPINFOW startup{}; startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESHOWWINDOW; startup.wShowWindow = SW_HIDE;
        Require(CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_SUSPENDED,
            nullptr, nullptr, &startup, &process) != FALSE, "Owned child could not start");
        Require(AssignProcessToJobObject(job, process.hProcess) != FALSE, "Owned child job assignment failed");
        Require(ResumeThread(process.hThread) != static_cast<DWORD>(-1), "Owned child could not resume");
    }
    void Kill() {
        Require(TerminateProcess(process.hProcess, 90) != FALSE, "Owned crash child could not be terminated");
        Require(WaitForSingleObject(process.hProcess, 10000) == WAIT_OBJECT_0, "Owned child did not exit after termination");
        DWORD code = 0;
        Require(GetExitCodeProcess(process.hProcess, &code) && code == 90, "Child did not exit through forced termination");
    }
};
int ChildMain(int argc, wchar_t** argv) {
    if (argc == 3 && std::wstring(argv[1]) == L"--recover") {
        const auto recovered = RecoverAssetMove(fs::path(argv[2]));
        if (!recovered.success) std::cerr << recovered.error << '\n';
        return recovered.success ? 0 : 1;
    }
    Require(argc == 5 && std::wstring(argv[1]) == L"--crash-child", "Invalid child arguments");
    const fs::path root(argv[2]);
    const int checkpoint = std::wstring(argv[3]) == L"before" ? 2 : 3;
    Handle ready; ready.value = OpenEventW(EVENT_MODIFY_STATE, FALSE, argv[4]);
    Require(ready.value != nullptr, "Owned ready event missing");
    AssetRegistry registry; std::string error;
    Require(registry.Scan(root, error, AssetRegistry::MetadataPolicy::ReadOnly), error.c_str());
    const auto* record = registry.FindByPath(root / L"元モデル.obj");
    Require(record != nullptr, "Child source record missing");
    int checks = 0;
    const auto moved = MoveAssetPair(root, {"元モデル.obj", "移動先/改名.obj", record->guid}, [&] {
        if (++checks == checkpoint) {
            Require(SetEvent(ready.value) != FALSE, "Child ready signal failed");
            // Parent terminates this owned child without unwinding held handles.
            Sleep(INFINITE);
        }
        return false;
    });
    if (!moved.success) std::cerr << moved.error << '\n';
    return 2; // Reaching normal completion is a failed crash synchronization.
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc > 1) {
        try { return ChildMain(argc, argv); }
        catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    }
    const auto fixture = fs::temp_directory_path() / (L"kubo-move-crash-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    try {
        wchar_t buffer[32768]{};
        const DWORD length = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
        Require(length > 0 && length < std::size(buffer), "Test executable path unavailable");
        const fs::path executable(buffer);
        for (const auto* stage : {L"before", L"split"}) {
            const auto root = fixture / stage / L"日本語 project 🚀";
            fs::create_directories(root / L"移動先");
            const auto source = root / L"元モデル.obj", destination = root / L"移動先/改名.obj";
            const auto sidecar = AssetRegistry::MetadataPath(source), journal = root / ".kubo/asset-move.pending.json";
            Write(source, "original payload\n");
            AssetRegistry initial; std::string error;
            Require(initial.Scan(root, error), error.c_str());
            const auto guid = initial.FindByPath(source)->guid, metadata = Read(sidecar);
            const auto eventName = L"Local\\KuboMoveCrash-" + std::to_wstring(GetCurrentProcessId()) + L"-" + stage + L"-" + std::to_wstring(GetTickCount64());
            Handle ready; ready.value = CreateEventW(nullptr, TRUE, FALSE, eventName.c_str());
            Require(ready.value != nullptr, "Ready event creation failed");
            {
                Child child;
                child.Start(executable, L"--crash-child \"" + root.wstring() + L"\" " + stage + L" \"" + eventName + L"\"");
                const HANDLE waits[] = {ready.value, child.process.hProcess};
                Require(WaitForMultipleObjects(2, waits, FALSE, 15000) == WAIT_OBJECT_0, "Child failed to reach persisted move checkpoint");
                Require(fs::exists(journal), "Child checkpoint lacked persisted journal");
                child.Kill();
            }
            Require(Read(sidecar) == metadata && (std::wstring(stage) == L"split" ? (!fs::exists(source) && Read(destination) == "original payload\n")
                : (Read(source) == "original payload\n" && !fs::exists(destination))), "Killed worker did not leave expected on-disk arrangement");
            AssetRegistry restarted;
            Require(!restarted.Scan(root, error, AssetRegistry::MetadataPolicy::ReadOnly), "Restart scan ignored pending crash journal");
            // A fresh process, not the crashed process or a retained C++ registry,
            // verifies persisted source/sidecar identities and performs recovery.
            {
                Child recovery;
                recovery.Start(executable, L"--recover \"" + root.wstring() + L"\"");
                Require(WaitForSingleObject(recovery.process.hProcess, 15000) == WAIT_OBJECT_0, "Recovery process timeout");
                DWORD code = 0;
                Require(GetExitCodeProcess(recovery.process.hProcess, &code) && code == 0, "Fresh-process recovery failed");
            }
            Require(!fs::exists(journal) && !fs::exists(destination) && Read(source) == "original payload\n" && Read(sidecar) == metadata,
                "Crash recovery lost original pair or changed bytes");
            Require(restarted.Scan(root, error, AssetRegistry::MetadataPolicy::ReadOnly) && restarted.ResolveGuid(guid) == source,
                "Recovered identity did not survive fresh read-only scan");
        }
        Require(ownedChildrenJoined, "Child exit unconfirmed; fixture retained");
        fs::remove_all(fixture);
        std::cout << "PASS: real owned-process termination before/between asset renames, restart guard and fresh-process identity recovery\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        // Never remove even an owned fixture while child exit is unconfirmed.
        if (ownedChildrenJoined) { std::error_code ignored; fs::remove_all(fixture, ignored); }
        else std::cerr << "Retained fixture: " << Engine::Base::PathToUtf8(fixture) << '\n';
        return 1;
    }
}
