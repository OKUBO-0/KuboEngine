#pragma once
// Authored data only; playback position/state belong to the runtime owner.
#include "../script/ScriptProperty.h"
#include "../../externals/imgui_node_editor/crude_json.h"
#include <stdexcept>
#include <vector>
namespace Engine::AnimationSystem {
struct ParticleAnimationEvent {
    std::string clip;
    double position=0.5;
    unsigned count=1;
    bool IsValid() const {
        return Scripting::ValidScriptText(clip,256,true) && position>0 && position<=1 && count>0 && count<=4096;
    }
};
struct AnimatorComponent {
    std::string clip = "default";
    std::string controllerGuid; // Nonempty selects Controller; clip remains legacy authored data.
    float speed = 1;
    bool loop = true, playOnAwake = true;
    static constexpr size_t MaximumEvents=32;
    std::vector<ParticleAnimationEvent> particleEvents;
    bool IsValid() const {
        if(!Scripting::ValidScriptText(clip,256,true) || !ValidControllerGuid(controllerGuid) ||
            !std::isfinite(speed) || speed<0 || particleEvents.size()>MaximumEvents)return false;
        for(const auto& event:particleEvents)if(!event.IsValid())return false;
        return true;
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
        particleEvents.swap(other.particleEvents);
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
    if(!component.particleEvents.empty()){
        crude_json::array events;
        for(const auto& event:component.particleEvents)
            events.emplace_back(crude_json::array{event.clip,event.position,double(event.count)});
        doc["particleEvents"]=std::move(events);
    }
    return doc;
}
inline bool DecodeAnimator(const crude_json::value& doc, AnimatorComponent& output) noexcept {
    try {
        if (!doc.is_object() || doc.get<crude_json::object>().size() !=
                4u+size_t(doc.contains("controllerGuid"))+size_t(doc.contains("particleEvents")) ||
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
        if(doc.contains("particleEvents")){
            const auto& events=doc["particleEvents"];
            if(!events.is_array() || events.get<crude_json::array>().size()>AnimatorComponent::MaximumEvents)return false;
            for(const auto& entry:events.get<crude_json::array>()){
                if(!entry.is_array() || entry.get<crude_json::array>().size()!=3 || !entry[0].is_string() ||
                    !entry[1].is_number() || !entry[2].is_number())return false;
                const auto count=entry[2].get<double>();
                if(!(count>=1 && count<=4096) || std::floor(count)!=count)return false;
                ParticleAnimationEvent event{entry[0].get<std::string>(),entry[1].get<double>(),static_cast<unsigned>(count)};
                if(!event.IsValid())return false;
                candidate.particleEvents.push_back(std::move(event));
            }
        }
        output.Swap(candidate); return true;
    } catch (...) { return false; }
}
inline unsigned CountAnimationParticles(const AnimatorComponent& animator,std::string_view clip,
    double from,double to,bool loop,unsigned limit) noexcept {
    if(!std::isfinite(from) || !std::isfinite(to) || from<0 || to<=from)return 0;
    unsigned total=0;
    for(const auto& event:animator.particleEvents){
        if(event.clip!=clip || !(event.position>0 && event.position<=1))continue;
        const double phase=event.position;
        const double crossings=loop?std::floor(to-phase)-std::floor(from-phase):double(from<phase && phase<=to);
        const double burst=crossings*event.count;
        if(burst>=limit-total)return limit;
        total+=static_cast<unsigned>(burst);
    }
    return total;
}
}
