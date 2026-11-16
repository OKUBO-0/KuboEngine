#pragma once

#include "AssetMove.h"
#include <atomic>

namespace Engine::Assets {
namespace IntakeDetail {
inline DWORD CALLBACK CopyProgress(LARGE_INTEGER, LARGE_INTEGER, LARGE_INTEGER, LARGE_INTEGER,
    DWORD, DWORD, HANDLE, HANDLE, LPVOID context)
{
    return static_cast<const std::atomic<bool>*>(context)->load() ? PROGRESS_CANCEL : PROGRESS_CONTINUE;
}
}

// Stage the complete selection before publishing files. If publication is
// interrupted, report the committed prefix and leave it available for refresh.
inline void CopyAssetFiles(const std::filesystem::path& assetRoot,
    const std::string& folder, const std::vector<std::filesystem::path>& sources,
    std::atomic<bool>& cancelled, size_t& copied, std::atomic<size_t>& staged)
{
    namespace fs = std::filesystem;
    const auto checkCancellation = [&] {
        if (cancelled.load()) throw std::runtime_error("Asset import cancelled");
    };
    const auto exists = [](const fs::path& path) {
        const auto attributes = GetFileAttributesW(path.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES) return true;
        const auto error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return false;
        throw std::runtime_error("Cannot inspect import destination: " + Base::PathToUtf8(path));
    };
    const auto root = fs::canonical(assetRoot);
    if (exists(root / ".kubo/asset-move.pending.json"))
        throw std::runtime_error("Recover the pending asset move before importing files");
    if (sources.size() > 1024) throw std::runtime_error("Select at most 1024 files per import");
    std::vector<fs::path> destinations;
    destinations.reserve(sources.size());
    for (const auto& source : sources) {
        checkCancellation();
        const auto attributes = GetFileAttributesW(source.c_str());
        if (!source.is_absolute() || attributes == INVALID_FILE_ATTRIBUTES ||
            (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
            throw std::runtime_error("Import requires an existing regular file: " + Base::PathToUtf8(source));
        const auto name = Base::PathToUtf8(source.filename());
        const auto relative = MoveDetail::Relative(folder.empty() ? name : folder + "/" + name);
        MoveDetail::CheckParents(root,relative);
        const auto destination = root / relative;
        const bool duplicate = std::any_of(destinations.begin(),destinations.end(),[&](const auto& other) {
            return CompareStringOrdinal(other.c_str(),-1,destination.c_str(),-1,TRUE) == CSTR_EQUAL;
        });
        if (duplicate || exists(destination) || exists(AssetRegistry::MetadataPath(destination)))
            throw std::runtime_error("Import destination already exists: " + Base::PathToUtf8(relative));
        destinations.push_back(destination);
    }

    const auto stagingParent = root / ".kubo";
    fs::create_directory(stagingParent);
    const auto stagingAttributes = GetFileAttributesW(stagingParent.c_str());
    if (stagingAttributes == INVALID_FILE_ATTRIBUTES || !(stagingAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (stagingAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
        throw std::runtime_error("Import staging directory is inaccessible or linked");
    static std::atomic<unsigned long long> sequence{0};
    const auto staging = stagingParent / (L"file-import-" + std::to_wstring(GetCurrentProcessId()) +
        L"-" + std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(++sequence));
    if (!fs::create_directory(staging)) throw std::runtime_error("Cannot create exclusive import staging directory");
    struct Cleanup {
        const fs::path& directory;
        ~Cleanup() { std::error_code error; fs::remove_all(directory,error); }
    } cleanup{staging};
    for (size_t i = 0; i < sources.size(); ++i) {
        checkCancellation();
        const auto temporary = staging / std::to_wstring(i);
        // The private staging directory permits fail-if-exists copying without
        // touching an existing asset, even when a source file is read-only.
        if (!CopyFileExW(sources[i].c_str(),temporary.c_str(),IntakeDetail::CopyProgress,&cancelled,
            nullptr,COPY_FILE_FAIL_IF_EXISTS))
            throw std::runtime_error("Cannot read or stage import file: " + Base::PathToUtf8(sources[i]));
        if (!SetFileAttributesW(temporary.c_str(),FILE_ATTRIBUTE_NORMAL))
            throw std::runtime_error("Cannot prepare staged import file");
        staged.fetch_add(1);
    }
    for (size_t i = 0; i < destinations.size(); ++i) {
        checkCancellation();
        MoveDetail::CheckParents(root,destinations[i].lexically_relative(root));
        if (exists(AssetRegistry::MetadataPath(destinations[i])) ||
            !MoveFileExW((staging / std::to_wstring(i)).c_str(),destinations[i].c_str(),MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot publish import file without overwriting: " + Base::PathToUtf8(destinations[i]));
        ++copied;
    }
}

} // namespace Engine::Assets
