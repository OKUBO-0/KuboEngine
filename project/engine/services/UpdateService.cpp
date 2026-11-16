#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "UpdateService.h"

#include "externals/imgui_node_editor/crude_json.h"

#include <Windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <charconv>
#include <cctype>
#include <array>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace Engine::Services {
namespace {

std::vector<int> ParseVersion(const std::string& value)
{
    if (value.empty() || value.size() > 128 || value.back() == '.') return {};
    std::vector<int> result;
    std::stringstream stream(value);
    std::string part;
    while (std::getline(stream, part, '.')) {
        int number = 0;
        const auto parsed = std::from_chars(part.data(), part.data() + part.size(), number);
        if (part.empty() || parsed.ec != std::errc{} || parsed.ptr != part.data() + part.size() || number < 0 ||
            !std::all_of(part.begin(), part.end(), [](unsigned char ch) { return ch >= '0' && ch <= '9'; })) return {};
        result.push_back(number);
    }
    return result;
}

bool ValidHash(const std::string& value)
{
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
    });
}

struct FileHandle {
    FileHandle() = default;
    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;
    HANDLE value = INVALID_HANDLE_VALUE;
    ~FileHandle() { Close(); }
    void Close() { if (value != INVALID_HANDLE_VALUE) { CloseHandle(value); value = INVALID_HANDLE_VALUE; } }
};

struct PendingFile {
    std::filesystem::path path;
    FileHandle file;
    ~PendingFile() { file.Close(); if (!path.empty()) DeleteFileW(path.c_str()); }
};

struct HashState {
    std::vector<unsigned char> object;
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    ~HashState() {
        // The caller-owned object buffer must outlive BCryptDestroyHash, even
        // when reading/allocating throws. Members remain alive in this body.
        if (hash) BCryptDestroyHash(hash);
        if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    }
};

std::string HashFile(HANDLE file)
{
    HashState state;
    if (BCryptOpenAlgorithmProvider(&state.algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) throw std::runtime_error("Cannot open SHA-256 provider");
    DWORD objectLength = 0, resultLength = 0;
    if (BCryptGetProperty(state.algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength), &resultLength, 0) != 0) throw std::runtime_error("Cannot query SHA-256 object length");
    state.object.resize(objectLength);
    if (BCryptCreateHash(state.algorithm, &state.hash, state.object.data(), objectLength, nullptr, 0, 0) != 0) throw std::runtime_error("Cannot create SHA-256 hash");
    LARGE_INTEGER beginning{};
    if (!SetFilePointerEx(file, beginning, nullptr, FILE_BEGIN)) throw std::runtime_error("Cannot seek update artifact");
    std::array<unsigned char, 64 * 1024> buffer;
    DWORD read = 0;
    for (;;) {
        if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr)) throw std::runtime_error("Cannot read update artifact");
        if (!read) break;
        if (BCryptHashData(state.hash, buffer.data(), read, 0) != 0) throw std::runtime_error("Cannot hash update artifact");
    }
    std::array<unsigned char, 32> digest;
    if (BCryptFinishHash(state.hash, digest.data(), static_cast<ULONG>(digest.size()), 0) != 0) throw std::runtime_error("Cannot finish SHA-256 hash");
    std::string result;
    static constexpr char digits[] = "0123456789abcdef";
    for (const auto byte : digest) { result += digits[byte >> 4]; result += digits[byte & 0x0f]; }
    return result;
}

} // namespace

int UpdateService::CompareVersions(const std::string& left, const std::string& right)
{
    const auto lhs = ParseVersion(left);
    const auto rhs = ParseVersion(right);
    if (lhs.empty() || rhs.empty()) return 0;
    for (size_t index = 0; index < std::max(lhs.size(), rhs.size()); ++index) {
        const int a = index < lhs.size() ? lhs[index] : 0;
        const int b = index < rhs.size() ? rhs[index] : 0;
        if (a != b) return a < b ? -1 : 1;
    }
    return 0;
}

UpdateInfo UpdateService::ReadManifest(const std::filesystem::path& manifestPath, const std::string& currentVersion)
{
    UpdateInfo result;
    try {
        const auto size = std::filesystem::file_size(manifestPath);
        if (size > 1024 * 1024) throw std::runtime_error("Update manifest exceeds 1 MiB");
        std::ifstream input(manifestPath, std::ios::binary);
        if (!input) throw std::runtime_error("Cannot open update manifest");
        std::string text(static_cast<size_t>(size), '\0');
        if (!input.read(text.data(), static_cast<std::streamsize>(text.size()))) throw std::runtime_error("Cannot read update manifest");
        const auto document = crude_json::value::parse(text);
        if (!document.is_object() || !document.contains("version") || !document["version"].is_string()) {
            throw std::runtime_error("Update manifest requires a string version");
        }
        result.version = document["version"].get<crude_json::string>();
        if (document.contains("url") && document["url"].is_string()) result.url = document["url"].get<crude_json::string>();
        if (document.contains("sha256") && document["sha256"].is_string()) result.sha256 = document["sha256"].get<crude_json::string>();
        if (document.contains("artifact") && document["artifact"].is_string()) result.artifact = document["artifact"].get<crude_json::string>();
        if (document.contains("sha256") && (!document["sha256"].is_string() || !ValidHash(result.sha256))) throw std::runtime_error("Invalid update SHA-256 digest");
        result.valid = !ParseVersion(result.version).empty() && !ParseVersion(currentVersion).empty();
        result.updateAvailable = CompareVersions(result.version, currentVersion) > 0;
        if (!result.valid) {
            result.error = "Invalid dotted numeric version in update manifest";
            result.updateAvailable = false;
        }
    } catch (const std::exception& exception) {
        result.error = exception.what();
    }
    return result;
}

bool UpdateService::ComputeSha256(const std::filesystem::path& file, std::string& hash, std::string& error)
{
    try {
        FileHandle input;
        input.value = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (input.value == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot open update artifact for stable reading");
        hash = HashFile(input.value);
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        hash.clear();
        error = exception.what();
        return false;
    }
}

bool UpdateService::StageVerifiedArtifact(const std::filesystem::path& source,
    const std::filesystem::path& destination, const std::string& expectedSha256, std::string& error)
{
    try {
        if (!ValidHash(expectedSha256)) throw std::runtime_error("A 64-character hexadecimal SHA-256 hash is required");
        FileHandle input;
        input.value = CreateFileW(source.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (input.value == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot open update artifact for stable copying");
        const auto target = std::filesystem::absolute(destination);
        std::filesystem::create_directories(target.parent_path());
        PendingFile pending;
        for (int attempt = 0; attempt < 8; ++attempt) {
            std::array<unsigned char, 16> random;
            if (BCryptGenRandom(nullptr, random.data(), static_cast<ULONG>(random.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) throw std::runtime_error("Cannot generate staging filename");
            std::wstring suffix;
            static constexpr wchar_t digits[] = L"0123456789abcdef";
            for (const auto byte : random) { suffix += digits[byte >> 4]; suffix += digits[byte & 0x0f]; }
            const auto candidate = target.parent_path() / (target.filename().wstring() + L".pending-" + suffix + L".tmp");
            pending.file.value = CreateFileW(candidate.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (pending.file.value != INVALID_HANDLE_VALUE) { pending.path = candidate; break; }
            if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS) throw std::runtime_error("Cannot create staging file");
        }
        if (pending.file.value == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot allocate unique staging file");
        std::array<unsigned char, 64 * 1024> buffer;
        DWORD read = 0;
        for (;;) {
            if (!ReadFile(input.value, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr)) throw std::runtime_error("Cannot copy update artifact");
            if (!read) break;
            DWORD written = 0;
            if (!WriteFile(pending.file.value, buffer.data(), read, &written, nullptr) || written != read) throw std::runtime_error("Cannot write complete staging file");
        }
        if (!FlushFileBuffers(pending.file.value)) throw std::runtime_error("Cannot flush staging file");
        const auto actual = HashFile(pending.file.value);
        std::string expected = expectedSha256;
        std::transform(expected.begin(), expected.end(), expected.begin(), [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
        if (actual != expected) throw std::runtime_error("Update artifact SHA-256 mismatch");
        input.Close();
        // Publish through the still-exclusive handle. Closing it before a
        // path-based rename would reopen a verified-bytes substitution window.
        const auto targetName = target.wstring();
        if (targetName.size() > 32767) throw std::runtime_error("Staging destination path is too long");
        const auto nameBytes = targetName.size() * sizeof(wchar_t);
        std::vector<unsigned char> renameBuffer(sizeof(FILE_RENAME_INFO) + nameBytes, 0);
        auto* rename = reinterpret_cast<FILE_RENAME_INFO*>(renameBuffer.data());
        rename->ReplaceIfExists = TRUE;
        rename->FileNameLength = static_cast<DWORD>(nameBytes);
        std::memcpy(rename->FileName, targetName.data(), nameBytes);
        if (!SetFileInformationByHandle(pending.file.value, FileRenameInfo, rename, static_cast<DWORD>(renameBuffer.size()))) {
            throw std::runtime_error("Cannot stage update artifact (Windows error " + std::to_string(GetLastError()) + ")");
        }
        pending.path.clear();
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

} // namespace Engine::Services
