#pragma once
// Private schema prototype; not EntityWorld/Inspector/Lua integration.
#include "../engine/script/ScriptProperty.h"
#include "../externals/imgui_node_editor/crude_json.h"
#include <stdexcept>
namespace AnimatorComponentCandidate {
struct Component {
    std::string clip = "default";
    float speed = 1;
    bool loop = true, playOnAwake = true;
    bool IsValid() const {
        return Engine::Scripting::ValidScriptText(clip, 256, true) &&
            std::isfinite(speed) && speed >= 0;
    }
    void Swap(Component& other) noexcept {
        clip.swap(other.clip);
        std::swap(speed, other.speed); std::swap(loop, other.loop);
        std::swap(playOnAwake, other.playOnAwake);
    }
};
inline crude_json::value Encode(const Component& component) {
    if (!component.IsValid()) throw std::invalid_argument("Invalid authored Animator");
    crude_json::value doc;
    doc["clip"] = component.clip; doc["speed"] = double(component.speed);
    doc["loop"] = component.loop; doc["playOnAwake"] = component.playOnAwake;
    return doc;
}
inline bool Decode(const crude_json::value& doc, Component& output) noexcept {
    try {
        if (!doc.is_object() || doc.get<crude_json::object>().size() != 4 ||
            !doc.contains("clip") || !doc["clip"].is_string() ||
            !doc.contains("speed") || !doc["speed"].is_number() ||
            !doc.contains("loop") || !doc["loop"].is_boolean() ||
            !doc.contains("playOnAwake") || !doc["playOnAwake"].is_boolean()) return false;
        const auto speed = doc["speed"].get<crude_json::number>();
        const auto& clip = doc["clip"].get<crude_json::string>();
        if (!std::isfinite(speed) || speed < 0 || speed > (std::numeric_limits<float>::max)() ||
            !Engine::Scripting::ValidScriptText(clip, 256, true)) return false;
        Component candidate;
        candidate.clip = clip; candidate.speed = static_cast<float>(speed);
        candidate.loop = doc["loop"].get<crude_json::boolean>();
        candidate.playOnAwake = doc["playOnAwake"].get<crude_json::boolean>();
        output.Swap(candidate); return true;
    } catch (...) { return false; }
}
}
