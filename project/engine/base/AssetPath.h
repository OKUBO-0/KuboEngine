#pragma once

#include <filesystem>
#include <string>

namespace Engine::Base {

inline std::filesystem::path PathFromUtf8(const std::string& text)
{
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}
inline std::string PathToUtf8(const std::filesystem::path& path)
{
    const auto text = path.generic_u8string();
    return {text.begin(), text.end()};
}

// File identity only: memory-backed asset names must not pass through this helper.
inline std::string NormalizeAssetPath(const std::filesystem::path& path)
{
    if (path.empty()) {
        return {};
    }
    std::error_code error;
    const auto absolute = std::filesystem::absolute(path, error);
    if (error) {
        return PathToUtf8(path.lexically_normal());
    }
    const auto canonical = std::filesystem::weakly_canonical(absolute, error);
    return PathToUtf8(error ? absolute.lexically_normal() : canonical);
}
// Engine/UI narrow asset strings have an explicit UTF-8 contract, never ACP.
inline std::string NormalizeAssetPath(const std::string& path) { return NormalizeAssetPath(PathFromUtf8(path)); }
inline std::string NormalizeAssetPath(const char* path) { return NormalizeAssetPath(std::string(path)); }

}
