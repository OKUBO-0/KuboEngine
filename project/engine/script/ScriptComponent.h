#pragma once

#include <string>
#include "ScriptProperty.h"

namespace Engine::Scripting {

/// @brief Serializable reference to a script asset attached to an entity.
/// @details `started` is runtime state and is intentionally not persisted.
struct ScriptComponent {
    std::string assetPath;
    std::string assetGuid;
    bool enabled = true;
    bool started = false;
    ScriptProperties properties; // Authored overrides; runtime Lua locals are not persisted.
    std::string RegistrationKey() const { return assetGuid.empty() ? assetPath : "guid:" + assetGuid; }
};

} // namespace Engine::Scripting
