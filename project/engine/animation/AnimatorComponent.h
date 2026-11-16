#pragma once
// Authored data only; playback position/state belong to the runtime owner.
#include "../script/ScriptProperty.h"
#include "../../externals/imgui_node_editor/crude_json.h"
#include <stdexcept>
namespace Engine::AnimationSystem {
struct AnimatorComponent {
    std::string clip = "default";
    std::string controllerGuid; // Nonempty selects Controller; clip remains legacy authored data.
    float speed = 1;
    bool loop = true, playOnAwake = true;
    bool IsValid() const {
        return Engine::Scripting::ValidScriptText(clip, 256, true) && ValidControllerGuid(controllerGuid) &&
            std::isfinite(speed) && speed >= 0;
    }
    static bool ValidControllerGuid(const std::string& guid) noexcept {
        if(guid.empty())return true;
        if(guid.size()!=16 && guid.size()!=32)return false;
        for(char c:guid)if(!((c>='0' && c<='9') || (c>='a' && c<='f')))return false;
        return true;
    }
    void Swap(AnimatorComponent& other) noexcept {
        clip.swap(other.clip);
        controllerGuid.swap(other.controllerGuid);
        std::swap(speed, other.speed); std::swap(loop, other.loop);
        std::swap(playOnAwake, other.playOnAwake);
    }
};
inline crude_json::value EncodeAnimator(const AnimatorComponent& component) {
    if (!component.IsValid()) throw std::invalid_argument("Invalid authored Animator");
    crude_json::value doc;
    doc["clip"] = component.clip; doc["speed"] = double(component.speed);
    doc["loop"] = component.loop; doc["playOnAwake"] = component.playOnAwake;
    if(!component.controllerGuid.empty())doc["controllerGuid"]=component.controllerGuid;
    return doc;
}
inline bool DecodeAnimator(const crude_json::value& doc, AnimatorComponent& output) noexcept {
    try {
        if (!doc.is_object() || doc.get<crude_json::object>().size() != (doc.contains("controllerGuid") ? 5 : 4) ||
            !doc.contains("clip") || !doc["clip"].is_string() ||
            !doc.contains("speed") || !doc["speed"].is_number() ||
            !doc.contains("loop") || !doc["loop"].is_boolean() ||
            !doc.contains("playOnAwake") || !doc["playOnAwake"].is_boolean()) return false;
        const auto speed = doc["speed"].get<crude_json::number>();
        const auto& clip = doc["clip"].get<crude_json::string>();
        if (!std::isfinite(speed) || speed < 0 || speed > (std::numeric_limits<float>::max)() ||
            !Engine::Scripting::ValidScriptText(clip, 256, true)) return false;
        AnimatorComponent candidate;
        if(doc.contains("controllerGuid")) {
            if(!doc["controllerGuid"].is_string())return false;
            candidate.controllerGuid=doc["controllerGuid"].get<crude_json::string>();
            if(!AnimatorComponent::ValidControllerGuid(candidate.controllerGuid))return false;
        }
        candidate.clip = clip; candidate.speed = static_cast<float>(speed);
        candidate.loop = doc["loop"].get<crude_json::boolean>();
        candidate.playOnAwake = doc["playOnAwake"].get<crude_json::boolean>();
        output.Swap(candidate); return true;
    } catch (...) { return false; }
}
}
