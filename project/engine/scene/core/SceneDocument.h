#pragma once

#include "externals/imgui_node_editor/crude_json.h"
#include <filesystem>
#include <string>

namespace Engine::Scene {

using SceneValue = crude_json::value;

// The scene supplies its schema; parsing and file replacement never mutate live objects.
class SceneDocument {
public:
    static constexpr size_t MaxBytes = 4 * 1024 * 1024;
    // Shared native bounded read before scene-specific candidate validation.
    static bool Read(const std::filesystem::path& path, SceneValue& result, std::string& error);
    // Bounded UTF-8 JSON publication after the caller's scene-specific semantic
    // validation. Like Read, this does not select a scene format/version.
    static bool Write(const std::filesystem::path& path, const SceneValue& value,
        std::string& error, bool replaceExisting=true);
    static std::filesystem::path AutosavePath(std::filesystem::path path) { path += L".autosave"; return path; }
    static bool Validate(const SceneValue& value, const SceneValue& schema, std::string& error);
    static bool Load(const std::filesystem::path& path, const SceneValue& schema,
        SceneValue& result, std::string& error);
    static bool Save(const std::filesystem::path& path, const SceneValue& value,
        const SceneValue& schema, std::string& error, bool replaceExisting=true);
};

}
