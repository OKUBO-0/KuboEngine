#include <Windows.h>
static BOOL WINAPI OwnershipCloseHandle(HANDLE handle);
// Inject a concurrent path replacement at the precise close/delete boundary.
// The production Start/Stop bodies are included unchanged; no private copy.
#define CloseHandle OwnershipCloseHandle
#ifdef KUBO_MCP_OWNERSHIP_CANDIDATE
#include "candidate_EditorMcpSession.h"
#else
#include "../engine/services/EditorMcpSession.h"
#endif
#undef CloseHandle
#include <iostream>

static std::filesystem::path originalRoot, movedRoot;
static BY_HANDLE_FILE_INFORMATION descriptorIdentity{};
static bool swapArmed = false, swapSucceeded = false, renameBlocked = false, leafOnly = false;
static unsigned swapCount = 0;
static BOOL WINAPI OwnershipCloseHandle(HANDLE handle) {
    BY_HANDLE_FILE_INFORMATION info{};
    const bool owned = swapArmed && GetFileInformationByHandle(handle, &info) &&
        info.dwVolumeSerialNumber == descriptorIdentity.dwVolumeSerialNumber &&
        info.nFileIndexHigh == descriptorIdentity.nFileIndexHigh && info.nFileIndexLow == descriptorIdentity.nFileIndexLow;
    const auto closed = ::CloseHandle(handle);
    if (owned && closed) {
        swapArmed = false; ++swapCount;
        try {
            if (leafOnly) std::filesystem::rename(Engine::Services::EditorMcpSession::Descriptor(originalRoot),
                Engine::Services::EditorMcpSession::Descriptor(originalRoot).parent_path() / "owned-backup.json");
            else std::filesystem::rename(originalRoot, movedRoot);
            const auto replacement = Engine::Services::EditorMcpSession::Descriptor(originalRoot);
            std::filesystem::create_directories(replacement.parent_path());
            std::ofstream foreign(replacement, std::ios::binary); foreign << "foreign replacement descriptor";
            foreign.flush(); swapSucceeded = bool(foreign);
        } catch (const std::filesystem::filesystem_error& error) {
            renameBlocked = !leafOnly && error.code().value() == ERROR_ACCESS_DENIED;
            std::cerr << "Replacement injection refused: " << error.what() << '\n'; swapSucceeded = false;
        }
        catch (const std::exception& error) { std::cerr << "Replacement injection failed: " << error.what() << '\n'; swapSucceeded = false; }
        catch (...) { swapSucceeded = false; }
    }
    return closed;
}

static std::string Read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
int main() {
    using Engine::Services::EditorMcpSession;
    const auto fixture = std::filesystem::temp_directory_path() /
        (L"kubo-mcp-descriptor-owner-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    std::cout << "Retained descriptor ownership fixture: " << fixture << '\n';
    try {
        if (std::filesystem::exists(fixture)) throw std::runtime_error("Fresh owned fixture required");
        const auto original = fixture / "original";
        const auto moved = fixture / "moved";
        std::filesystem::create_directories(original);
        EditorMcpSession session;
        std::string error;
        if (!session.Start(original, error)) throw std::runtime_error(error);
        const auto initial = Read(EditorMcpSession::Descriptor(original));
        if (initial.empty()) throw std::runtime_error("Original descriptor not published");
        // The live handle denies directory rename. Exercise the race only once
        // Stop closes it; do not weaken the live owner's sharing flags.
        const auto query = CreateFileW(EditorMcpSession::Descriptor(original).c_str(), FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (query == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot query owned descriptor identity");
        const bool identified = GetFileInformationByHandle(query, &descriptorIdentity) != FALSE;
        ::CloseHandle(query);
        if (!identified) throw std::runtime_error("Cannot identify owned descriptor");
        originalRoot = original; movedRoot = moved; swapArmed = true;
        const auto replacement = EditorMcpSession::Descriptor(original);
        session.Stop();
        std::cout << "MCP_REPLACEMENT_INJECTION count=" << swapCount << " succeeded=" << swapSucceeded << " blocked=" << renameBlocked << '\n';
        if ((!swapSucceeded && !renameBlocked) || swapCount != 1) throw std::runtime_error("Close-boundary replacement was not exercised");
        if (renameBlocked) {
            if (std::filesystem::exists(EditorMcpSession::Descriptor(original))) throw std::runtime_error("Pinned-parent Stop retained owned descriptor");
            std::filesystem::rename(original, moved);
            std::filesystem::create_directories(replacement.parent_path());
            std::ofstream foreign(replacement, std::ios::binary); foreign << "foreign replacement descriptor";
            if (!foreign) throw std::runtime_error("Cannot create post-Stop replacement");
        }
        const bool foreignPreserved = Read(replacement) == "foreign replacement descriptor";
        const bool ownedRemoved = !std::filesystem::exists(EditorMcpSession::Descriptor(moved));
        std::cout << "MCP_DESCRIPTOR_OWNERSHIP foreign_preserved=" << foreignPreserved
                  << " original_handle_removed=" << ownedRemoved << " stopped=" << !session.IsRunning() << '\n';
        if (!foreignPreserved || !ownedRemoved || session.IsRunning()) return 1;
        // Idempotent Stop must not revisit the replaced pathname.
        session.Stop();
        if (Read(replacement) != "foreign replacement descriptor") return 1;
        if (!session.Start(moved, error)) throw std::runtime_error("Reenable in moved project failed: " + error);
        session.Stop();
        if (std::filesystem::exists(EditorMcpSession::Descriptor(moved))) return 1;
        if (!session.Start(moved, error)) throw std::runtime_error(error);
        const auto beforeSwap = Read(EditorMcpSession::Descriptor(moved));
        const auto leafQuery = CreateFileW(EditorMcpSession::Descriptor(moved).c_str(), FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (leafQuery == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot query leaf replacement identity");
        const bool leafIdentified = GetFileInformationByHandle(leafQuery, &descriptorIdentity) != FALSE;
        ::CloseHandle(leafQuery);
        if (!leafIdentified) throw std::runtime_error("Cannot identify leaf replacement");
        originalRoot = moved; leafOnly = true; swapCount = 0; swapSucceeded = false; renameBlocked = false; swapArmed = true;
        session.Stop();
        const auto backup = EditorMcpSession::Descriptor(moved).parent_path() / "owned-backup.json";
        if (!swapSucceeded || swapCount != 1 || Read(EditorMcpSession::Descriptor(moved)) != "foreign replacement descriptor" ||
            Read(backup) != beforeSwap) throw std::runtime_error("Leaf replacement was deleted or moved owned evidence changed");
        session.Stop();
        if (session.Start(moved, error) || Read(EditorMcpSession::Descriptor(moved)) != "foreign replacement descriptor")
            throw std::runtime_error("Foreign descriptor was taken over");
        std::cout << "MCP_LEAF_REPLACEMENT foreign_preserved=1 moved_owned_evidence_retained=1 duplicate_refused=1\n";
        std::cout << "PASS: MCP exact owned descriptor retirement across root rename/replacement, idempotent Stop and reenable\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
