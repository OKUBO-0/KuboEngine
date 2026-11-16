#pragma once

#include <Windows.h>
#include <filesystem>

namespace Engine::Base {

inline constexpr unsigned ProjectPreviewWidth = 640;
inline constexpr unsigned ProjectPreviewHeight = 360;
inline constexpr unsigned ProjectPreviewMaxBytes = 1024 * 1024;

inline std::filesystem::path ProjectPreviewPath(const std::filesystem::path& root)
{
    return root / L"UserSettings/Hub/thumbnail.png";
}

// Check parents from the drive outward before traversing the next directory.
// A preview is optional, including for projects on a network or redirected drive.
inline bool PlainLocalDirectories(const std::filesystem::path& root, bool allowMissing)
{
    if (!root.is_absolute() || GetDriveTypeW(root.root_path().c_str()) != DRIVE_FIXED) return false;
    auto path = root.root_path();
    for (const auto& component : root.relative_path()) {
        if (component == L"." || component == L"..") return false;
        path /= component;
        const DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            const DWORD error = GetLastError();
            if (allowMissing && (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)) continue;
            return false;
        }
        if (!(attributes & FILE_ATTRIBUTE_DIRECTORY) || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
    }
    return true;
}

inline bool PreviewDirectoriesAllowed(const std::filesystem::path& root, bool allowMissing)
{
    return root.is_absolute() && PlainLocalDirectories(root / L"UserSettings/Hub", allowMissing);
}

} // namespace Engine::Base
