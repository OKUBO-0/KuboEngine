#pragma once
#include <filesystem>

namespace Engine::Base {
// Snapshot the owning root once; later cwd changes must not redirect settings.
class ProjectPaths final {
public:
    explicit ProjectPaths(const std::filesystem::path& root = {})
        : root_(std::filesystem::absolute(root.empty() ? std::filesystem::current_path() : root).lexically_normal()) {}
    const std::filesystem::path& Root() const { return root_; }
    std::filesystem::path InputBindings() const { return root_ / "UserSettings/input_bindings.json"; }
    std::filesystem::path EditorLayout() const { return root_ / "UserSettings/editor-layout.ini"; }
    std::filesystem::path EditorWindows() const { return root_ / "UserSettings/editor-windows.ini"; }
    std::filesystem::path NativeBuildSettings() const { return root_ / "UserSettings/native-build.json"; }
    std::filesystem::path LegacyScene() const { return root_ / "UserScenes/gameplay.json"; }
private:
    std::filesystem::path root_;
};
}
