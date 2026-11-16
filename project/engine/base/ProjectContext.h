#pragma once

#include <filesystem>
#include <string>
#include "AssetPath.h"
#include "render/ViewExtent.h"
#include "../script/LuaResourceBudget.h"

namespace Engine::Base {

/// @brief KuboEngine project manifest and root-relative path resolver.
/// @details The editor and runtime use this context instead of relying on the
///          process working directory. Existing projects may keep Resources/;
///          new projects use Assets/.
class ProjectContext final {
public:
    struct Settings {
        std::string name;
        std::string engineVersion = "0.1.0";
        std::string startupScene = "Assets/Scenes/Main.scene";
        std::string assetDirectory = "Assets";
        ViewExtent gameOutput{};
        Scripting::LuaResourceLimits luaLimits{};
    };

    bool Open(const std::filesystem::path& projectFile, std::string& error);
    bool Create(const std::filesystem::path& projectRoot, const std::string& name,
        std::string& error);
    // Saves only Game output, preserving other JSON fields. Requires unchanged
    // manifest bytes since Open/last save; not a cross-process compare-and-swap.
    bool SaveGameOutput(ViewExtent extent, std::string& error);
    bool SaveLaunchSettings(ViewExtent extent, const std::string& startupScene, std::string& error);
    bool SaveLaunchSettings(ViewExtent extent, const std::string& startupScene,
        Scripting::LuaResourceLimits luaLimits, std::string& error);
    bool SaveLuaLimits(Scripting::LuaResourceLimits limits, std::string& error);
    static bool ValidLuaLimits(Scripting::LuaResourceLimits limits) noexcept {
        return limits.memoryBytes>0 && limits.memoryBytes<=1024ULL*1024*1024 &&
            limits.states>0 && limits.states<=4096 && limits.frameInstructions>0 && limits.frameInstructions<=100000000;
    }

    bool IsOpen() const { return !projectFile_.empty(); }
    const std::filesystem::path& GetProjectFile() const { return projectFile_; }
    const std::filesystem::path& GetRootPath() const { return rootPath_; }
    const Settings& GetSettings() const { return settings_; }

    /// @brief Resolve a project-relative asset or settings path.
    /// @details Absolute paths are returned unchanged. The result is lexical;
    ///          callers can check existence without changing the process cwd.
    std::filesystem::path ResolvePath(const std::filesystem::path& path) const;
    std::filesystem::path ResolveAssetPath(const std::filesystem::path& path) const;
    // Engine narrow paths are UTF-8, including manifest strings.
    std::filesystem::path ResolvePath(const std::string& path) const { return ResolvePath(PathFromUtf8(path)); }
    std::filesystem::path ResolvePath(const char* path) const { return ResolvePath(std::string(path)); }
    std::filesystem::path ResolveAssetPath(const std::string& path) const { return ResolveAssetPath(PathFromUtf8(path)); }
    std::filesystem::path ResolveAssetPath(const char* path) const { return ResolveAssetPath(std::string(path)); }

    static std::filesystem::path FindManifest(const std::filesystem::path& path);
    static std::filesystem::path FindManifestUpwards(const std::filesystem::path& start);

private:
    bool LoadManifest(const std::filesystem::path& projectFile, std::string& error);
    bool SaveSettings(const ViewExtent* extent, const std::string* startupScene,
        const Scripting::LuaResourceLimits* luaLimits, std::string& error);

    std::filesystem::path projectFile_;
    std::filesystem::path rootPath_;
    Settings settings_{};
    std::string manifestBytes_;
};

} // namespace Engine::Base
