#pragma once
#include "../AssetPath.h"
#include <cwchar>

namespace Engine::Base {
inline std::string ResolveTextureAssetPath(const std::string& filePath,
    const std::filesystem::path& executableDirectory, const std::filesystem::path& projectRoot)
{
    if (filePath.empty()) return {};
    const auto requested = PathFromUtf8(filePath).lexically_normal();
    if (requested.is_absolute()) return PathToUtf8(requested);
    const auto candidate = (projectRoot / requested).lexically_normal();
    if (std::filesystem::exists(candidate)) return PathToUtf8(candidate);
    // Only legacy Resources paths may fall back to engine-bundled content.
    // Missing authored Assets/custom paths must not bind unrelated installation files.
    if (!executableDirectory.empty() && !requested.has_root_path() &&
        requested.begin()!=requested.end() && _wcsicmp(requested.begin()->c_str(),L"Resources")==0) {
        const auto bundled = (executableDirectory / requested).lexically_normal();
        if (std::filesystem::is_regular_file(bundled)) return PathToUtf8(bundled);
    }
    return PathToUtf8(candidate);
}
}
