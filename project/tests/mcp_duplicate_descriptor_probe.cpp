// Registered regression: ambiguous JSON identity must remain evidence.
#ifdef MCP_UNIQUE_KEYS_CANDIDATE
#include "mcp_stale_archive_unique_keys.h"
#else
#include "../engine/services/McpStaleArchive.h"
#endif
#include <fstream>
#include <iostream>
int main() {
    using namespace Engine::Services;
    McpProcessIdentity self;
    if (!McpProcessIdentity::Current(self)) return 2;
    const auto root = std::filesystem::temp_directory_path() /
        (L"kubo-mcp-duplicate-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    if (std::filesystem::exists(root)) return 2;
    const auto parent = root / "Library/Mcp";
    std::filesystem::create_directories(parent);
    const auto path = parent / "editor-session.json";
    // Both low fields are syntactically valid; the second names this live owner.
    const std::string bytes = "{\"format\":\"kubo-editor-mcp-2\",\"pipe\":\"\\\\\\\\.\\\\pipe\\\\KuboEditor-00000000000000000000000000000000\",\"pid\":" +
        std::to_string(self.pid) + ",\"process_creation_high\":" + std::to_string(self.creation.dwHighDateTime) +
        ",\"process_creation_low\":" + std::to_string(self.creation.dwLowDateTime ^ 1u) +
        ",\"process_creation_low\":" + std::to_string(self.creation.dwLowDateTime) + "}";
    { std::ofstream output(path, std::ios::binary); output << bytes; if (!output) return 2; }
    std::wstring archived = L"unchanged";
    const auto state = McpStaleArchive::Archive(root, archived);
    std::cout << "DUPLICATE_DESCRIPTOR_STATE=" << static_cast<int>(state) << " RETAINED=" << root << '\n';
    if (state == McpStaleArchive::State::Refused && std::filesystem::exists(path)) {
        std::ifstream input(path, std::ios::binary);
        const std::string after{std::istreambuf_iterator<char>(input), {}};
        if (after == bytes && archived == L"unchanged") {
            input.close();
            // The first-member parser produces a unique, mismatched-generation
            // positive control. Refusing all metadata is not a valid repair.
            const auto uniqueBytes = crude_json::value::parse(bytes).dump();
            { std::ofstream output(path, std::ios::binary); output << uniqueBytes; if (!output) return 2; }
            if (McpStaleArchive::Archive(root, archived) != McpStaleArchive::State::Archived ||
                std::filesystem::exists(path) || archived == L"unchanged") return 1;
            std::ifstream saved(parent / archived, std::ios::binary);
            const std::string retained{std::istreambuf_iterator<char>(saved), {}};
            if (retained != uniqueBytes) return 1;
            std::cout << "PASS: ambiguous duplicate identity preserved; unique stale generation still archives exact bytes (not physical PID reuse)\n";
            return 0;
        }
    }
    std::cerr << "FAIL: ambiguous duplicate identity was accepted for archival; fixture retained\n";
    return 1;
}
