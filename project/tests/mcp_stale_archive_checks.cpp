#include "../engine/services/McpStaleArchive.h"
#include "../engine/services/EditorMcpSession.h"
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <thread>

static void Require(bool condition, const char* why) { if (!condition) throw std::runtime_error(why); }
static std::string Read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
static void Write(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream output(path, std::ios::binary); output << bytes;
    Require(bool(output), "Owned fixture write failed");
}
struct Event {
    HANDLE handle = nullptr;
    ~Event() { if (handle) CloseHandle(handle); }
};
struct Child {
    PROCESS_INFORMATION process{};
    ~Child() {
        if (process.hProcess) {
            if (WaitForSingleObject(process.hProcess, 0) != WAIT_OBJECT_0) { TerminateProcess(process.hProcess, 77); WaitForSingleObject(process.hProcess, 5000); }
            CloseHandle(process.hProcess);
        }
        if (process.hThread) CloseHandle(process.hThread);
    }
};
static std::filesystem::path collisionParent, occupied;
static void OccupyFirst(const wchar_t* leaf, unsigned attempt) {
    if (attempt) return;
    occupied = collisionParent / leaf;
    const auto file = CreateFileW(occupied.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    Require(file != INVALID_HANDLE_VALUE, "Fresh collision fixture already occupied");
    constexpr char text[] = "occupied archive evidence"; DWORD written = 0;
    const bool saved = WriteFile(file, text, sizeof(text) - 1, &written, nullptr) && written == sizeof(text) - 1;
    CloseHandle(file); Require(saved, "Cannot create collision evidence");
}
int wmain(int argc, wchar_t** argv) {
    using Engine::Services::EditorMcpSession;
    using namespace Engine::Services::McpStaleArchive;
    try {
        if (argc == 4 && std::wstring(argv[1]) == L"--publish") {
            EditorMcpSession session; std::string error;
            Require(session.Start(argv[2], error), error.c_str());
            Event ready{OpenEventW(EVENT_MODIFY_STATE, FALSE, argv[3])};
            Require(ready.handle && SetEvent(ready.handle), "Cannot announce owned publication");
            std::_Exit(70); // Actual published descriptor; intentionally no Stop/destructor.
        }
        Require(argc == 1, "No other arguments accepted");
        const auto suffix = std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
        const auto root = std::filesystem::temp_directory_path() / (L"kubo-mcp-stale-\u65E5\u672C-" + suffix);
        Require(!std::filesystem::exists(root), "Fresh owned root required");
        std::filesystem::create_directories(root);
        std::cout << "Retained stale archive fixture: " << root << '\n';
        const auto readyName = L"Local\\KuboMcpStale-" + suffix;
        Event ready{CreateEventW(nullptr, TRUE, FALSE, readyName.c_str())}; Require(ready.handle != nullptr, "Cannot create ready event");
        wchar_t executable[32768]{}; Require(GetModuleFileNameW(nullptr, executable, _countof(executable)), "Cannot locate owned helper");
        auto command = L"\"" + std::wstring(executable) + L"\" --publish \"" + root.wstring() + L"\" " + readyName;
        STARTUPINFOW startup{}; startup.cb = sizeof(startup); Child child;
        Require(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child.process), "Owned publisher launch failed");
        Require(WaitForSingleObject(ready.handle, 5000) == WAIT_OBJECT_0 && WaitForSingleObject(child.process.hProcess, 5000) == WAIT_OBJECT_0, "Publisher did not terminate");
        DWORD exit = 0; Require(GetExitCodeProcess(child.process.hProcess, &exit) && exit == 70, "Expected abrupt publisher exit70");
        const auto path = EditorMcpSession::Descriptor(root);
        const auto initial = Read(path); Require(!initial.empty(), "Abnormal owner lost descriptor");
        collisionParent = path.parent_path(); std::wstring archive = L"unchanged";
        Require(Archive(root, archive, OccupyFirst) == State::Archived, "Ended real owner was not archived");
        Require(!std::filesystem::exists(path) && Read(path.parent_path() / archive) == initial &&
            Read(occupied) == "occupied archive evidence", "Archive bytes/collision preservation failed");
        const auto firstArchive = archive;
        EditorMcpSession current; std::string error;
        Require(current.Start(root, error), "Archive did not permit actual Start");
        const auto live = Read(path); archive = L"unchanged";
        Require(Archive(root, archive) == State::Refused && archive == L"unchanged" && Read(path) == live && current.IsRunning(), "Live write lease changed");
        current.Stop();
        Require(Archive(root, archive) == State::Absent && archive == L"unchanged", "Absent source changed output");
        Write(path, initial);
        Require(current.Start(root, error), "Production Start did not recover actual exited publisher metadata");
        bool recoveredBytes = false;
        for (const auto& entry : std::filesystem::directory_iterator(path.parent_path())) {
            if (entry.path().filename().wstring().find(L"editor-session.stale-") == 0 &&
                entry.path().filename().wstring() != firstArchive && entry.path() != occupied && Read(entry.path()) == initial)
                recoveredBytes = true;
        }
        Require(recoveredBytes && current.IsRunning() && Read(path) != initial, "Production recovery lost stale evidence or failed new publication");
        current.Stop();
        Write(path, initial);
        EditorMcpSession contenderA, contenderB;
        Event startRace{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
        Require(startRace.handle != nullptr, "Cannot create race event");
        bool wonA = false, wonB = false; std::string errorA, errorB;
        std::thread first([&] { WaitForSingleObject(startRace.handle, INFINITE); wonA = contenderA.Start(root, errorA); });
        std::thread second;
        try { second = std::thread([&] { WaitForSingleObject(startRace.handle, INFINITE); wonB = contenderB.Start(root, errorB); }); }
        catch (...) { SetEvent(startRace.handle); first.join(); throw; }
        SetEvent(startRace.handle); first.join(); second.join();
        Require(wonA != wonB && contenderA.IsRunning() == wonA && contenderB.IsRunning() == wonB,
            "Competing production Starts did not preserve one exclusive owner");
        const auto winnerBytes = Read(path);
        Require(!winnerBytes.empty() && winnerBytes != initial, "Winner did not publish fresh descriptor");
        if (wonA) contenderB.Stop(); else contenderA.Stop();
        Require(Read(path) == winnerBytes, "Failed contender Stop changed winner descriptor");
        contenderA.Stop(); contenderB.Stop();
        Require(!std::filesystem::exists(path), "Winning session could not retire its descriptor");
        auto legacy = crude_json::value::parse(initial); legacy["format"] = "kubo-editor-mcp-1";
        Write(path, legacy.dump()); const auto legacyBytes = Read(path);
        Require(Archive(root, archive) == State::Archived && Read(path.parent_path() / archive) == legacyBytes, "Ended legacy owner not preserved");
        Engine::Services::McpProcessIdentity self; Require(Engine::Services::McpProcessIdentity::Current(self), "Cannot capture current identity");
        legacy["pid"] = double(self.pid); Write(path, legacy.dump()); const auto liveLegacy = Read(path); archive = L"unchanged";
        Require(Archive(root, archive) == State::Refused && Read(path) == liveLegacy && archive == L"unchanged", "Legacy live/reused PID archived");
        Require(!current.Start(root, error) && Read(path) == liveLegacy && !current.IsRunning(), "Production Start displaced live legacy metadata");
        std::filesystem::rename(path, path.parent_path() / "legacy-live-retained.json");
        auto v2 = crude_json::value::parse(initial);
        v2["pid"] = double(self.pid);
        v2["process_creation_high"] = double(self.creation.dwHighDateTime);
        v2["process_creation_low"] = double(self.creation.dwLowDateTime);
        Write(path, v2.dump()); const auto aliveBytes = Read(path);
        Require(Archive(root, archive) == State::Refused && Read(path) == aliveBytes && archive == L"unchanged", "Live v2 without writer lease archived");
        std::filesystem::rename(path, path.parent_path() / "v2-live-retained.json");
        v2["process_creation_low"] = double(self.creation.dwLowDateTime ^ 1u);
        Write(path, v2.dump()); const auto mismatchBytes = Read(path);
        Require(Archive(root, archive) == State::Archived && Read(path.parent_path() / archive) == mismatchBytes, "Different recorded generation not archived");
        // Controlled generation mismatch, not forced physical PID reuse.
        Write(path, initial); archive = L"unchanged";
        Require(SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_READONLY), "Cannot stage owned readonly file");
        Require(Archive(root, archive) == State::Refused && Read(path) == initial && archive == L"unchanged", "Readonly evidence changed");
        Require(SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL), "Cannot restore owned fixture attributes");
        std::filesystem::rename(path, path.parent_path() / "readonly-retained.json");
        Write(path, "{malformed"); archive = L"unchanged";
        Require(Archive(root, archive) == State::Refused && Read(path) == "{malformed" && archive == L"unchanged", "Unknown metadata changed");
        Require(!current.Start(root, error) && Read(path) == "{malformed" && !current.IsRunning(), "Production Start changed unknown metadata");
        std::filesystem::rename(path, path.parent_path() / "malformed-retained.json");
        Write(path, std::string(4097, 'x'));
        Require(Archive(root, archive) == State::Refused && Read(path).size() == 4097, "Oversized metadata changed");
        std::filesystem::rename(path, path.parent_path() / "oversized-retained.json");
        Write(path, initial); const auto alias = path.parent_path() / "hardlink-retained.json";
        Require(CreateHardLinkW(alias.c_str(), path.c_str(), nullptr), "Owned hardlink fixture failed");
        Require(Archive(root, archive) == State::Refused && Read(path) == initial && Read(alias) == initial, "Hardlinked metadata changed");
        Require(Read(path.parent_path() / firstArchive) == initial && Read(occupied) == "occupied archive evidence", "Earlier evidence changed");
        std::cout << "PASS: production stale archive and actual Start recovery; actual published owner exit70, Unicode root, no-replace collision/raw bytes, controlled generation mismatch, live lease/live v2/legacy live/readonly/unknown/oversize/hardlink refusal and absent-output isolation (not forced PID reuse)\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
