#pragma once
// Preserve proven stale metadata; unknown or active owners are never displaced.
#include "McpProcessIdentity.h"
#include "McpDescriptorValidation.h"
#include "../../externals/imgui_node_editor/crude_json.h"
#include <winternl.h>
#include <filesystem>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <string>
#include <cstddef>
#include <cstdio>

namespace Engine::Services::McpStaleArchive {
enum class State { Archived, Absent, Refused };
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    ~Handle() { if (value != INVALID_HANDLE_VALUE && value) CloseHandle(value); }
};
using CreateNative = NTSTATUS (NTAPI*)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES,
    PIO_STATUS_BLOCK, PLARGE_INTEGER, ULONG, ULONG, ULONG, ULONG, PVOID, ULONG);
using SetNative = NTSTATUS (NTAPI*)(HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, FILE_INFORMATION_CLASS);
using BeforeRename = void (*)(const wchar_t* leaf, unsigned attempt);
inline bool Integer(const crude_json::value& doc, const char* key, DWORD& out) {
    if (!doc.contains(key) || !doc[key].is_number()) return false;
    const auto value = doc[key].get<crude_json::number>();
    if (!std::isfinite(value) || value < 0 || value > 4294967295.0 || std::floor(value) != value) return false;
    out = static_cast<DWORD>(value); return true;
}
inline State Archive(const std::filesystem::path& root, std::wstring& archivedLeaf, BeforeRename before = nullptr) noexcept {
    try {
        const auto parent = root / "Library/Mcp";
        Handle directory{CreateFileW(parent.c_str(), FILE_READ_ATTRIBUTES | FILE_TRAVERSE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
        if (directory.value == INVALID_HANDLE_VALUE) return State::Refused;
        FILE_ATTRIBUTE_TAG_INFO attributes{};
        if (!GetFileInformationByHandleEx(directory.value, FileAttributeTagInfo, &attributes, sizeof(attributes)) ||
            !(attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) return State::Refused;
        const auto module = GetModuleHandleW(L"ntdll.dll");
        const auto create = module ? reinterpret_cast<CreateNative>(GetProcAddress(module, "NtCreateFile")) : nullptr;
        const auto set = module ? reinterpret_cast<SetNative>(GetProcAddress(module, "NtSetInformationFile")) : nullptr;
        if (!create || !set) return State::Refused;
        wchar_t source[] = L"editor-session.json";
        UNICODE_STRING name{}; name.Buffer = source;
        name.Length = static_cast<USHORT>(sizeof(source) - sizeof(wchar_t)); name.MaximumLength = sizeof(source);
        OBJECT_ATTRIBUTES object{}; object.Length = sizeof(object); object.RootDirectory = directory.value;
        object.ObjectName = &name; object.Attributes = 0x40;
        IO_STATUS_BLOCK io{}; Handle file;
        const auto opened = create(&file.value, FILE_READ_DATA | FILE_READ_ATTRIBUTES | DELETE | SYNCHRONIZE,
            &object, &io, nullptr, 0, FILE_SHARE_READ, 1, 0x40 | 0x20 | 0x200000, nullptr, 0);
        if (opened == static_cast<NTSTATUS>(0xC0000034L)) return State::Absent;
        if (opened < 0) return State::Refused;
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(file.value, &info) || info.nNumberOfLinks != 1 ||
            (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_READONLY))) return State::Refused;
        char bytes[4097]{}; DWORD size = 0;
        if (!ReadFile(file.value, bytes, sizeof(bytes), &size, nullptr) || !McpDescriptorValidation::Preflight(bytes, size)) return State::Refused;
        const auto doc = crude_json::value::parse(std::string(bytes, size));
        if (!doc.is_object() || !doc.contains("format") || !doc["format"].is_string() ||
            !doc.contains("pipe") || !doc["pipe"].is_string()) return State::Refused;
        const auto format = doc["format"].get<crude_json::string>();
        if (format != "kubo-editor-mcp-2" && format != "kubo-editor-mcp-1") return State::Refused;
        const auto pipe = doc["pipe"].get<crude_json::string>();
        constexpr const char* prefix = "\\\\.\\pipe\\KuboEditor-";
        constexpr size_t prefixSize = sizeof("\\\\.\\pipe\\KuboEditor-") - 1;
        if (pipe.size() != prefixSize + 32 || pipe.compare(0, prefixSize, prefix) != 0) return State::Refused;
        for (size_t i = prefixSize; i < pipe.size(); ++i) if (!((pipe[i] >= '0' && pipe[i] <= '9') || (pipe[i] >= 'a' && pipe[i] <= 'f'))) return State::Refused;
        Engine::Services::McpProcessIdentity identity;
        if (!Integer(doc, "pid", identity.pid) || !identity.pid) return State::Refused;
        Engine::Services::McpProcessLease ownerLease;
        Handle legacyOwner;
        if (format == "kubo-editor-mcp-2") {
            if (!Integer(doc, "process_creation_high", identity.creation.dwHighDateTime) ||
                !Integer(doc, "process_creation_low", identity.creation.dwLowDateTime) ||
                (!identity.creation.dwHighDateTime && !identity.creation.dwLowDateTime)) return State::Refused;
            const auto state = ownerLease.Acquire(identity);
            if (state != Engine::Services::McpProcessLease::State::Exited && state != Engine::Services::McpProcessLease::State::DifferentProcess) return State::Refused;
        } else {
            // Legacy records have no generation identity: a live/reused PID is
            // inconclusive and must never be archived automatically.
            legacyOwner.value = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, identity.pid);
            if (!legacyOwner.value) { if (GetLastError() != ERROR_INVALID_PARAMETER) return State::Refused; }
            else if (WaitForSingleObject(legacyOwner.value, 0) != WAIT_OBJECT_0) return State::Refused;
        }
        static std::atomic<unsigned long long> sequence{0};
        for (unsigned attempt = 0; attempt < 16; ++attempt) {
            wchar_t leaf[128]{};
            const auto length = _snwprintf_s(leaf, _countof(leaf), _TRUNCATE,
                L"editor-session.stale-%lu-%lu-%llu-%llu.json", identity.pid, GetCurrentProcessId(), GetTickCount64(), sequence.fetch_add(1));
            if (length <= 0) return State::Refused;
            std::wstring prepared(leaf, static_cast<size_t>(length)); // Allocate before mutation.
            if (before) before(leaf, attempt);
            alignas(FILE_RENAME_INFO) std::array<unsigned char, offsetof(FILE_RENAME_INFO, FileName) + sizeof(leaf)> storage{};
            auto* rename = reinterpret_cast<FILE_RENAME_INFO*>(storage.data());
            rename->ReplaceIfExists = FALSE; rename->RootDirectory = directory.value;
            rename->FileNameLength = static_cast<DWORD>(length * sizeof(wchar_t));
            std::memcpy(rename->FileName, leaf, rename->FileNameLength);
            IO_STATUS_BLOCK renamed{};
            const auto result = set(file.value, &renamed, rename,
                static_cast<ULONG>(offsetof(FILE_RENAME_INFO, FileName) + rename->FileNameLength + sizeof(wchar_t)),
                static_cast<FILE_INFORMATION_CLASS>(10)); // FileRenameInformation; native root-relative target.
            if (result == 0) {
                archivedLeaf.swap(prepared); return State::Archived;
            }
            if (result != static_cast<NTSTATUS>(0xC0000035L)) {
                std::fprintf(stderr, "MCP_ARCHIVE_RENAME_STATUS=0x%08lX attempt=%u\n", static_cast<ULONG>(result), attempt);
                return State::Refused;
            }
        }
        return State::Refused;
    } catch (...) { return State::Refused; }
}
}
