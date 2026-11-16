#pragma once
#include <Windows.h>
#include <winternl.h>
#include <filesystem>

namespace Engine::Services {
// Pin the physical parent, but keep the published descriptor's original access
// and sharing contract so existing ifstream/MCP clients remain compatible.
class McpDescriptorRetirement final {
    using CreateNative = NTSTATUS (NTAPI*)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES,
        PIO_STATUS_BLOCK, PLARGE_INTEGER, ULONG, ULONG, ULONG, ULONG, PVOID, ULONG);
    HANDLE directory_ = INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION identity_{};
    CreateNative create_ = nullptr;
public:
    McpDescriptorRetirement() = default;
    ~McpDescriptorRetirement() { Reset(); }
    McpDescriptorRetirement(const McpDescriptorRetirement&) = delete;
    McpDescriptorRetirement& operator=(const McpDescriptorRetirement&) = delete;
    void Reset() noexcept {
        if (directory_ != INVALID_HANDLE_VALUE) CloseHandle(directory_);
        directory_ = INVALID_HANDLE_VALUE; create_ = nullptr; identity_ = {};
    }
    bool Capture(const std::filesystem::path& parent, HANDLE file) noexcept {
        Reset();
        if (!GetFileInformationByHandle(file, &identity_) || identity_.nNumberOfLinks != 1 ||
            (identity_.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) return false;
        directory_ = CreateFileW(parent.c_str(), FILE_READ_ATTRIBUTES | FILE_TRAVERSE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        FILE_ATTRIBUTE_TAG_INFO attributes{};
        if (directory_ == INVALID_HANDLE_VALUE ||
            !GetFileInformationByHandleEx(directory_, FileAttributeTagInfo, &attributes, sizeof(attributes)) ||
            !(attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) { Reset(); return false; }
        const auto module = GetModuleHandleW(L"ntdll.dll");
        if (module) create_ = reinterpret_cast<CreateNative>(GetProcAddress(module, "NtCreateFile"));
        if (!create_) { Reset(); return false; }
        return true;
    }
    // Invoke only after closing the original write lease. Open relative to the
    // captured physical parent; delete only after verifying the original file.
    bool Retire() noexcept {
        if (directory_ == INVALID_HANDLE_VALUE || !create_) return false;
        wchar_t leaf[] = L"editor-session.json";
        UNICODE_STRING name{};
        name.Buffer = leaf; name.Length = static_cast<USHORT>(sizeof(leaf) - sizeof(wchar_t));
        name.MaximumLength = static_cast<USHORT>(sizeof(leaf));
        OBJECT_ATTRIBUTES object{};
        object.Length = sizeof(object); object.RootDirectory = directory_;
        object.ObjectName = &name; object.Attributes = 0x40;
        IO_STATUS_BLOCK status{};
        HANDLE file = INVALID_HANDLE_VALUE;
        const auto result = create_(&file, DELETE | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
            &object, &status, nullptr, 0, FILE_SHARE_READ, 1,
            0x40 | 0x20 | 0x200000, nullptr, 0);
        // FILE_OPEN (never create), NON_DIRECTORY, SYNCHRONOUS, OPEN_REPARSE_POINT.
        if (result < 0) return false;
        BY_HANDLE_FILE_INFORMATION actual{};
        const bool same = GetFileInformationByHandle(file, &actual) && actual.nNumberOfLinks == 1 &&
            !(actual.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) &&
            actual.dwVolumeSerialNumber == identity_.dwVolumeSerialNumber &&
            actual.nFileIndexHigh == identity_.nFileIndexHigh && actual.nFileIndexLow == identity_.nFileIndexLow &&
            actual.ftCreationTime.dwHighDateTime == identity_.ftCreationTime.dwHighDateTime &&
            actual.ftCreationTime.dwLowDateTime == identity_.ftCreationTime.dwLowDateTime;
        FILE_DISPOSITION_INFO disposition{TRUE};
        const bool retired = same && SetFileInformationByHandle(file, FileDispositionInfo, &disposition, sizeof(disposition));
        CloseHandle(file);
        return retired;
    }
};
}
