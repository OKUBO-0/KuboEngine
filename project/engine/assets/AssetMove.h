#pragma once
#include "AssetRegistry.h"
#include "../base/AssetPath.h"
#include "externals/imgui_node_editor/crude_json.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <charconv>
#include <stdexcept>

namespace Engine::Assets {
struct AssetMoveRequest { std::string sourceRelative, destinationRelative, guid; };
struct AssetMoveResult { bool success = false, rollbackFailed = false; std::string error; };
namespace MoveDetail {
struct File {
    HANDLE handle = INVALID_HANDLE_VALUE;
    File() = default;
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    ~File() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
    bool Open(const std::filesystem::path& path) {
        handle = CreateFileW(path.c_str(), GENERIC_READ | DELETE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        return handle != INVALID_HANDLE_VALUE;
    }
};
inline bool Rename(HANDLE file, const std::filesystem::path& destination) noexcept {
    try {
    const auto name = destination.wstring();
    const auto bytes = offsetof(FILE_RENAME_INFO, FileName) + (name.size() + 1) * sizeof(wchar_t);
    std::vector<BYTE> storage(bytes, 0);
    auto* info = reinterpret_cast<FILE_RENAME_INFO*>(storage.data());
    info->ReplaceIfExists = FALSE;
    info->FileNameLength = static_cast<DWORD>(name.size() * sizeof(wchar_t));
    std::memcpy(info->FileName, name.data(), info->FileNameLength);
    return SetFileInformationByHandle(file, FileRenameInfo, info, static_cast<DWORD>(bytes)) != FALSE;
    } catch (...) { return false; }
}
inline std::filesystem::path Relative(const std::string& text) {
    if (text.empty() || text.find('\0') != std::string::npos) throw std::runtime_error("Asset path is empty or contains NUL");
    const auto path = Base::PathFromUtf8(text);
    if (path.has_root_name() || path.has_root_directory()) throw std::runtime_error("Asset move paths must be root-relative");
    for (const auto& component : path) {
        auto name = Base::PathToUtf8(component);
        if (name.empty() || name == "." || name == ".." || name.back() == '.' || name.back() == ' ')
            throw std::runtime_error("Invalid asset path component");
        for (unsigned char c : name) if (c < 32 || std::string("<>:\"|?*").find(static_cast<char>(c)) != std::string::npos)
            throw std::runtime_error("Invalid Windows asset filename");
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const auto base = name.substr(0, name.find('.'));
        if (base == "con" || base == "prn" || base == "aux" || base == "nul" ||
            (base.size() == 4 && (base.starts_with("com") || base.starts_with("lpt")) && base[3] >= '1' && base[3] <= '9') ||
            name.ends_with(".kubo-meta.json")) throw std::runtime_error("Reserved asset filename");
    }
    const auto first = Base::PathToUtf8(*path.begin());
    for (const auto* reserved : {".git", ".vs", ".kubo", "Library", "Build", "Builds", "generated", "ProjectSettings", "UserSettings"})
        if (_stricmp(first.c_str(), reserved) == 0) throw std::runtime_error("Reserved asset directory");
    return path;
}
inline void CheckParents(const std::filesystem::path& root, const std::filesystem::path& relative) {
    auto current = root;
    for (const auto& part : relative.parent_path()) {
        current /= part;
        const auto attributes = GetFileAttributesW(current.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY) || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            throw std::runtime_error("Move parent directory is missing or linked; create a normal folder first");
    }
}
}
// Pair transaction using held file handles, never overwrite a destination. Rollback
// is best-effort: a concurrent creator can prevent restoration, so report that state.
inline AssetMoveResult MoveAssetPair(const std::filesystem::path& assetRoot, const AssetMoveRequest& request,
    const std::function<bool()>& cancelled = {})
{
    AssetMoveResult result;
    MoveDetail::File sourceFile, metadataFile, journalFile;
    std::filesystem::path source, destination;
    bool sourceMoved = false;
    const auto clearJournal = [&]() {
        if (journalFile.handle == INVALID_HANDLE_VALUE) return true;
        FILE_DISPOSITION_INFO disposition{TRUE};
        return SetFileInformationByHandle(journalFile.handle, FileDispositionInfo, &disposition, sizeof(disposition)) != FALSE;
    };
    try {
        const auto check = [&]() { if (cancelled && cancelled()) throw std::runtime_error("Asset move cancelled"); };
        check();
        const auto root = std::filesystem::canonical(assetRoot);
        const auto from = MoveDetail::Relative(request.sourceRelative), to = MoveDetail::Relative(request.destinationRelative);
        MoveDetail::CheckParents(root, from); MoveDetail::CheckParents(root, to);
        if (from.extension() != to.extension()) throw std::runtime_error("Keep the asset file extension unchanged");
        source = root / from; destination = root / to;
        const auto metadata = AssetRegistry::MetadataPath(source), targetMetadata = AssetRegistry::MetadataPath(destination);
        if (std::filesystem::exists(destination) || std::filesystem::exists(targetMetadata)) throw std::runtime_error("Destination already exists (including case-only renames)");
        for (const auto& path : {source, metadata}) {
            const auto attributes = GetFileAttributesW(path.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
                throw std::runtime_error("Move requires normal source and persistent metadata files");
        }
        if (!sourceFile.Open(source) || !metadataFile.Open(metadata)) throw std::runtime_error("Source/metadata is locked or cannot be opened for rename");
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(metadataFile.handle, &size) || size.QuadPart < 0 || size.QuadPart > 16 * 1024) throw std::runtime_error("Invalid asset metadata size");
        std::string bytes(static_cast<size_t>(size.QuadPart), '\0'); DWORD read = 0;
        if (!ReadFile(metadataFile.handle, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) || read != bytes.size()) throw std::runtime_error("Cannot read asset metadata");
        const auto json = crude_json::value::parse(bytes);
        if (!json.is_object() || !json.contains("version") || !json["version"].is_number() || json["version"].get<double>() != 1 ||
            !json.contains("guid") || !json["guid"].is_string() || json["guid"].get<crude_json::string>() != request.guid ||
            (request.guid.size() != 16 && request.guid.size() != 32) ||
            !std::all_of(request.guid.begin(), request.guid.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }))
            throw std::runtime_error("Selected GUID no longer matches persistent metadata");
        const auto journalDirectory = root / ".kubo";
        std::filesystem::create_directory(journalDirectory);
        const auto directoryAttributes = GetFileAttributesW(journalDirectory.c_str());
        if (directoryAttributes == INVALID_FILE_ATTRIBUTES || (directoryAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
            throw std::runtime_error("Asset move journal directory is linked or inaccessible");
        journalFile.handle = CreateFileW((journalDirectory / "asset-move.pending.json").c_str(), GENERIC_WRITE | DELETE, FILE_SHARE_READ,
            nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (journalFile.handle == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot create move journal; recover any pending move first");
        crude_json::value journal;
        journal["version"] = 2.0; journal["guid"] = request.guid;
        journal["sourceRelative"] = request.sourceRelative; journal["destinationRelative"] = request.destinationRelative;
        BY_HANDLE_FILE_INFORMATION identity{};
        if (!GetFileInformationByHandle(sourceFile.handle, &identity)) throw std::runtime_error("Cannot inspect source identity for move journal");
        journal["volumeSerial"] = std::to_string(identity.dwVolumeSerialNumber);
        journal["fileIndexHigh"] = std::to_string(identity.nFileIndexHigh); journal["fileIndexLow"] = std::to_string(identity.nFileIndexLow);
        if (!GetFileInformationByHandle(metadataFile.handle, &identity)) throw std::runtime_error("Cannot inspect metadata identity for move journal");
        journal["metadataVolumeSerial"] = std::to_string(identity.dwVolumeSerialNumber);
        journal["metadataFileIndexHigh"] = std::to_string(identity.nFileIndexHigh);
        journal["metadataFileIndexLow"] = std::to_string(identity.nFileIndexLow);
        const auto journalBytes = journal.dump(2); DWORD written = 0;
        if (!WriteFile(journalFile.handle, journalBytes.data(), static_cast<DWORD>(journalBytes.size()), &written, nullptr) ||
            written != journalBytes.size() || !FlushFileBuffers(journalFile.handle)) throw std::runtime_error("Cannot persist asset move journal");
        check();
        if (!MoveDetail::Rename(sourceFile.handle, destination)) throw std::runtime_error("Cannot rename source without overwriting a destination");
        sourceMoved = true;
        check();
        if (!MoveDetail::Rename(metadataFile.handle, targetMetadata)) throw std::runtime_error("Cannot rename asset metadata");
        result.success = true;
        if (!clearJournal()) result.error = "Pair moved but pending journal could not be cleared; recovery required";
    } catch (const std::exception& exception) {
        result.error = exception.what();
        if (sourceMoved && !MoveDetail::Rename(sourceFile.handle, source)) {
            result.rollbackFailed = true;
            result.error += "; rollback failed: source remains at " + Base::PathToUtf8(destination) + ", metadata remains at " + Base::PathToUtf8(AssetRegistry::MetadataPath(source));
        }
        if (!result.rollbackFailed && !clearJournal()) result.error += "; pending journal could not be cleared";
    }
    return result;
}
// Explicit recovery only. Ambiguous/legacy journals and competing paths stay blocked.
// Every mutation uses a held, identity-verified handle and never overwrites a file.
inline AssetMoveResult RecoverAssetMove(const std::filesystem::path& assetRoot,
    const std::function<bool()>& cancelled = {})
{
    AssetMoveResult result;
    MoveDetail::File journalFile, files[4];
    try {
        const auto check = [&]() { if (cancelled && cancelled()) throw std::runtime_error("Asset recovery cancelled"); };
        check();
        const auto root = std::filesystem::canonical(assetRoot);
        MoveDetail::CheckParents(root, ".kubo/asset-move.pending.json");
        const auto readJson = [](HANDLE file) {
            BY_HANDLE_FILE_INFORMATION info{};
            if (!GetFileInformationByHandle(file, &info) || (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
                throw std::runtime_error("Recovery requires ordinary files");
            LARGE_INTEGER size{};
            if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > 16 * 1024) throw std::runtime_error("Invalid recovery metadata size");
            std::string bytes(static_cast<size_t>(size.QuadPart), '\0'); DWORD count = 0;
            if (!ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr) || count != bytes.size())
                throw std::runtime_error("Cannot read recovery metadata");
            const auto json = crude_json::value::parse(bytes);
            if (!json.is_object()) throw std::runtime_error("Invalid recovery JSON");
            return json;
        };
        if (!journalFile.Open(root / ".kubo/asset-move.pending.json")) throw std::runtime_error("No accessible pending journal, or another operation is active");
        const auto journal = readJson(journalFile.handle);
        if (!journal.contains("version") || !journal["version"].is_number() || journal["version"].get<double>() != 2)
            throw std::runtime_error("Recovery requires version 2 file identities; legacy journals require manual inspection");
        const auto string = [&](const char* key) {
            if (!journal.contains(key) || !journal[key].is_string()) throw std::runtime_error("Missing recovery field");
            return journal[key].get<crude_json::string>();
        };
        const auto number = [&](const char* key) {
            const auto value = string(key); DWORD parsed = 0;
            const auto conversion = std::from_chars(value.data(), value.data() + value.size(), parsed);
            if (value.empty() || conversion.ec != std::errc{} || conversion.ptr != value.data() + value.size())
                throw std::runtime_error("Invalid recovery file identity");
            return parsed;
        };
        const auto guid = string("guid");
        if ((guid.size() != 16 && guid.size() != 32) || !std::all_of(guid.begin(), guid.end(), [](char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); })) throw std::runtime_error("Invalid recovery GUID");
        const auto from = MoveDetail::Relative(string("sourceRelative")), to = MoveDetail::Relative(string("destinationRelative"));
        if (from.extension() != to.extension()) throw std::runtime_error("Recovery extension mismatch");
        MoveDetail::CheckParents(root, from); MoveDetail::CheckParents(root, to);
        const DWORD volume = number("volumeSerial"), high = number("fileIndexHigh"), low = number("fileIndexLow");
        const DWORD metaVolume = number("metadataVolumeSerial"), metaHigh = number("metadataFileIndexHigh"), metaLow = number("metadataFileIndexLow");
        const std::filesystem::path paths[4] = {root / from, root / to, AssetRegistry::MetadataPath(root / from), AssetRegistry::MetadataPath(root / to)};
        int sourceAt = -1, metadataAt = -1;
        for (int i = 0; i < 4; ++i) {
            const auto attributes = GetFileAttributesW(paths[i].c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES) {
                const auto error = GetLastError();
                if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) continue;
                throw std::runtime_error("Cannot inspect recovery path");
            }
            if ((attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) || !files[i].Open(paths[i]))
                throw std::runtime_error("Recovery path is linked, not a file, or locked");
            BY_HANDLE_FILE_INFORMATION info{};
            if (!GetFileInformationByHandle(files[i].handle, &info) || (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
                throw std::runtime_error("Cannot inspect held recovery file");
            const bool metadata = i >= 2;
            if (info.dwVolumeSerialNumber != (metadata ? metaVolume : volume) || info.nFileIndexHigh != (metadata ? metaHigh : high) ||
                info.nFileIndexLow != (metadata ? metaLow : low)) throw std::runtime_error("Competing or replaced recovery file; preserve it and inspect manually");
            auto& at = metadata ? metadataAt : sourceAt;
            if (at != -1) throw std::runtime_error("Ambiguous recovery file identity");
            at = i;
        }
        if (sourceAt < 0 || metadataAt < 0) throw std::runtime_error("Original recovery pair is missing");
        const auto metadata = readJson(files[metadataAt].handle);
        if (!metadata.contains("version") || !metadata["version"].is_number() || metadata["version"].get<double>() != 1 ||
            !metadata.contains("guid") || !metadata["guid"].is_string() || metadata["guid"].get<crude_json::string>() != guid)
            throw std::runtime_error("Recovery sidecar no longer matches GUID");
        check();
        // A split pair always returns to its original location. Already-complete
        // source or destination pairs only acknowledge the journal.
        if (sourceAt == 1 && metadataAt == 2) {
            if (!MoveDetail::Rename(files[sourceAt].handle, paths[0])) throw std::runtime_error("Recovery rollback blocked; journal retained");
        } else if (!((sourceAt == 0 && metadataAt == 2) || (sourceAt == 1 && metadataAt == 3))) {
            throw std::runtime_error("Unsupported recovery arrangement; journal retained");
        }
        FILE_DISPOSITION_INFO disposition{TRUE};
        if (!SetFileInformationByHandle(journalFile.handle, FileDispositionInfo, &disposition, sizeof(disposition)))
            throw std::runtime_error("Pair verified but journal could not be cleared; retry recovery");
        result.success = true;
    } catch (const std::exception& exception) { result.error = exception.what(); }
    return result;
}
}
