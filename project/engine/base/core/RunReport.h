#pragma once
#include <Windows.h>
#include <winternl.h>
#include <atomic>
#include <cstdio>
#include <exception>
#include <filesystem>

namespace Engine::Base {
// Best-effort diagnostics, independent of Logger and the current directory.
// The directory handle pins the destination even if its path is renamed.
class RunReport final {
    using CreateFileNative = NTSTATUS (NTAPI*)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES,
        PIO_STATUS_BLOCK, PLARGE_INTEGER, ULONG, ULONG, ULONG, ULONG, PVOID, ULONG);
    HANDLE directory_ = INVALID_HANDLE_VALUE;
    CreateFileNative create_ = nullptr;
    struct Buffer {
        char data[12288]{};
        DWORD size = 0;
        void Text(const char* text) noexcept {
            while (*text && size < sizeof(data)) data[size++] = *text++;
        }
        void Error(const std::exception_ptr& error) noexcept {
            if (!error) { Text("none\n"); return; }
            try { std::rethrow_exception(error); }
            catch (const std::exception& e) { Escaped(e.what()); }
            catch (...) { Text("non-standard C++ exception\n"); }
        }
        void Escaped(const char* message) noexcept {
            if (!message) { Text("null exception message\n"); return; }
            constexpr char hex[] = "0123456789ABCDEF";
            size_t i = 0;
            for (; i < 1024 && message[i]; ++i) {
                const auto c = static_cast<unsigned char>(message[i]);
                if (c >= 32 && c < 127 && c != '\\') {
                    const char one[] = {static_cast<char>(c), 0}; Text(one);
                } else {
                    const char escaped[] = {'\\', 'x', hex[c >> 4], hex[c & 15], 0}; Text(escaped);
                }
            }
            if (i == 1024) Text(" [truncated after 1024 bytes]");
            Text("\n");
        }
    };
public:
    explicit RunReport(const std::filesystem::path& root) noexcept {
        directory_ = CreateFileW(root.c_str(), FILE_READ_ATTRIBUTES | FILE_TRAVERSE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        FILE_ATTRIBUTE_TAG_INFO attributes{};
        if (directory_ != INVALID_HANDLE_VALUE &&
            (!GetFileInformationByHandleEx(directory_, FileAttributeTagInfo, &attributes, sizeof(attributes)) ||
             !(attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
             (attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))) {
            CloseHandle(directory_); directory_ = INVALID_HANDLE_VALUE;
        }
        const auto module = GetModuleHandleW(L"ntdll.dll");
        if (module) create_ = reinterpret_cast<CreateFileNative>(GetProcAddress(module, "NtCreateFile"));
    }
    ~RunReport() { if (directory_ != INVALID_HANDLE_VALUE) CloseHandle(directory_); }
    RunReport(const RunReport&) = delete;
    RunReport& operator=(const RunReport&) = delete;

    bool Save(const std::exception_ptr& operation, const std::exception_ptr& cleanup,
              bool shutdownComplete, unsigned attempts) noexcept {
        if (directory_ == INVALID_HANDLE_VALUE || !create_) return false;
        Buffer buffer;
        char header[256]{};
        std::snprintf(header, sizeof(header),
            "KuboEngine run report v1\npid=%lu\nshutdown_complete=%u\nshutdown_attempts=%u\n[operation error]\n",
            GetCurrentProcessId(), shutdownComplete ? 1u : 0u, attempts);
        buffer.Text(header); buffer.Error(operation);
        buffer.Text("[first shutdown error]\n"); buffer.Error(cleanup);
        buffer.Text("END KuboEngine run report\n");
        static std::atomic<unsigned long long> sequence{0};
        for (unsigned attempt = 0; attempt < 16; ++attempt) {
            wchar_t leaf[128]{};
            const int count = _snwprintf_s(leaf, _countof(leaf), _TRUNCATE,
                L".kubo-run-error-%lu-%llu-%llu.log", GetCurrentProcessId(),
                GetTickCount64(), sequence.fetch_add(1));
            if (count <= 0) return false;
            UNICODE_STRING name{};
            name.Buffer = leaf; name.Length = static_cast<USHORT>(count * sizeof(wchar_t));
            name.MaximumLength = static_cast<USHORT>(sizeof(leaf));
            OBJECT_ATTRIBUTES object{};
            object.Length = sizeof(object); object.RootDirectory = directory_;
            object.ObjectName = &name; object.Attributes = 0x40; // OBJ_CASE_INSENSITIVE
            IO_STATUS_BLOCK status{};
            HANDLE file = INVALID_HANDLE_VALUE;
            const auto result = create_(&file, FILE_WRITE_DATA | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                &object, &status, nullptr, FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ,
                2, 0x40 | 0x20 | 0x2 | 0x200000, nullptr, 0);
            // FILE_CREATE; NON_DIRECTORY, SYNCHRONOUS_IO_NONALERT,
            // WRITE_THROUGH, OPEN_REPARSE_POINT. Never open an existing leaf.
            if (result < 0) {
                if (result == static_cast<NTSTATUS>(0xC0000035L)) continue; // name collision
                return false;
            }
            BY_HANDLE_FILE_INFORMATION info{};
            DWORD written = 0;
            const bool valid = GetFileInformationByHandle(file, &info) && info.nNumberOfLinks == 1 &&
                !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT));
            const bool saved = valid && WriteFile(file, buffer.data, buffer.size, &written, nullptr) &&
                written == buffer.size && FlushFileBuffers(file);
            CloseHandle(file);
            // Failed writes may leave a new partial report; never remove files here.
            if (saved) {
                OutputDebugStringW(L"KuboEngine report saved in captured project directory: ");
                OutputDebugStringW(leaf); OutputDebugStringW(L"\n");
            }
            return saved;
        }
        return false;
    }
};
}
