#pragma once
#include "ScriptAudioRequests.h"
#include "../animation/AnimatorBinding.h"
#include <array>
#include <cstring>
#include <span>
#include <type_traits>
namespace Engine::Scripting {
enum class ScriptAnimatorRequestKind {Playback,Float,Bool,Trigger,LayerWeight,ForceState};
struct ScriptAnimatorRequest {
    uint64_t entity=0;
    AnimationSystem::AnimatorCommand command=AnimationSystem::AnimatorCommand::Play;
    std::array<char,257> clip{};
    uint16_t clipBytes=0;
    std::array<char,32> controllerGuid{};
    uint8_t controllerGuidBytes=0;
    float speed=1;
    bool loop=true,playOnAwake=true;
    ScriptAnimatorRequestKind kind=ScriptAnimatorRequestKind::Playback;
    std::array<char,128> parameter{};
    uint16_t parameterBytes=0;
    double number=0;bool flag=false;
    uint8_t layer=0;
    bool ValidControllerPayload() const noexcept {
        if(kind==ScriptAnimatorRequestKind::ForceState)return layer<4 && parameterBytes>0 && parameterBytes<=parameter.size() &&
            ValidScriptText(std::string_view(parameter.data(),parameterBytes),128,true) && std::isfinite(number) && number>=0 && number<=60;
        if(kind==ScriptAnimatorRequestKind::LayerWeight)return parameterBytes==0 && layer<4 && std::isfinite(number) && number>=0 && number<=1;
        if(kind==ScriptAnimatorRequestKind::Playback || static_cast<unsigned>(kind)>3 ||
            !parameterBytes || parameterBytes>parameter.size() ||
            !ValidScriptText(std::string_view(parameter.data(),parameterBytes),128,true))return false;
        return kind!=ScriptAnimatorRequestKind::Float || (std::isfinite(number) && std::abs(number)<=(std::numeric_limits<float>::max)());
    }
    bool Matches(const AnimationSystem::AnimatorComponent& settings) const noexcept {
        if(!clipBytes || clipBytes>256 || controllerGuidBytes>controllerGuid.size() || !settings.IsValid())return false;
        return clipBytes==settings.clip.size() && std::memcmp(clip.data(),settings.clip.data(),clipBytes)==0 &&
            controllerGuidBytes==settings.controllerGuid.size() && std::memcmp(controllerGuid.data(),settings.controllerGuid.data(),controllerGuidBytes)==0 &&
            speed==settings.speed && loop==settings.loop && playOnAwake==settings.playOnAwake;
    }
};
static_assert(std::is_trivially_copyable_v<ScriptAnimatorRequest>);
class ScriptAnimatorBatch final {
public:
    static constexpr size_t Maximum=64;
    bool Append(uint64_t entity,AnimationSystem::AnimatorCommand command,const AnimationSystem::AnimatorComponent& settings) noexcept {
        if(!entity || entity>9007199254740991ULL || static_cast<unsigned>(command)>3 || !settings.IsValid() || count_==Maximum)return false;
        ScriptAnimatorRequest value;value.entity=entity;value.command=command;value.clipBytes=static_cast<uint16_t>(settings.clip.size());
        std::memcpy(value.clip.data(),settings.clip.data(),value.clipBytes);value.speed=settings.speed;value.loop=settings.loop;value.playOnAwake=settings.playOnAwake;
        value.controllerGuidBytes=static_cast<uint8_t>(settings.controllerGuid.size());
        std::memcpy(value.controllerGuid.data(),settings.controllerGuid.data(),value.controllerGuidBytes);
        values_[count_++]=value;return true;
    }
    std::span<const ScriptAnimatorRequest> Values() const noexcept {return {values_.data(),count_};}
    bool AppendController(uint64_t entity,ScriptAnimatorRequestKind kind,std::string_view name,double number,bool flag,
        const AnimationSystem::AnimatorComponent& settings) noexcept {
        if(kind==ScriptAnimatorRequestKind::Playback || static_cast<unsigned>(kind)>3 || settings.controllerGuid.empty() ||
            !ValidScriptText(name,128,true) || (kind==ScriptAnimatorRequestKind::Float &&
            (!std::isfinite(number) || std::abs(number)>(std::numeric_limits<float>::max)())))return false;
        if(!Append(entity,AnimationSystem::AnimatorCommand::Play,settings))return false;
        auto& value=values_[count_-1];value.kind=kind;value.parameterBytes=static_cast<uint16_t>(name.size());
        std::memcpy(value.parameter.data(),name.data(),name.size());value.number=number;value.flag=flag;return true;
    }
    void Clear() noexcept {count_=0;}
    bool AppendForceState(uint64_t entity,size_t layer,std::string_view name,double duration,const AnimationSystem::AnimatorComponent& settings) noexcept {
        if(layer>=4 || !ValidScriptText(name,128,true) || !std::isfinite(duration) || duration<0 || duration>60 || settings.controllerGuid.empty())return false;
        if(!Append(entity,AnimationSystem::AnimatorCommand::Play,settings))return false;
        auto& value=values_[count_-1];value.kind=ScriptAnimatorRequestKind::ForceState;value.layer=static_cast<uint8_t>(layer);
        value.parameterBytes=static_cast<uint16_t>(name.size());std::memcpy(value.parameter.data(),name.data(),name.size());value.number=duration;return true;
    }
    bool AppendLayerWeight(uint64_t entity,size_t layer,double weight,const AnimationSystem::AnimatorComponent& settings) noexcept {
        if(layer>=4 || !std::isfinite(weight) || weight<0 || weight>1 || settings.controllerGuid.empty())return false;
        if(!Append(entity,AnimationSystem::AnimatorCommand::Play,settings))return false;
        auto& value=values_[count_-1];value.kind=ScriptAnimatorRequestKind::LayerWeight;value.layer=static_cast<uint8_t>(layer);value.number=weight;return true;
    }
private:
    std::array<ScriptAnimatorRequest,Maximum> values_{};size_t count_=0;
};
// Main-thread only. No callbacks/reentry/concurrent mutation during admission.
// Requests address final live Entity IDs and reject changed settings/missing
// Entities. A same-ID remove/re-add with identical settings is the same target.
class ScriptAnimatorRequests final {
public:
    static constexpr size_t MaximumPending=256;
    struct Pending {std::array<ScriptAnimatorRequest,MaximumPending> values{};size_t count=0;};
    void SetEnabled(bool enabled) noexcept {enabled_=enabled;if(!enabled)Clear();}
    bool Enabled() const noexcept {return enabled_;}
    bool CanPublish(const ScriptAnimatorBatch& batch) const noexcept {return batch.Values().empty() || (enabled_ && batch.Values().size()<=MaximumPending-pending_.count);}
    // Audio copy-and-swap can fail; Animator admission is checked BEFORE it.
    // Following Audio success, fixed trivial copies cannot allocate/throw.
    bool PublishTogether(const ScriptAnimatorBatch& batch,ScriptAudioRequests* audio,const std::vector<ScriptAudioRequest>& sounds) {
        if(!CanPublish(batch))return false;
        if(!sounds.empty() && (!audio || !audio->Publish(sounds)))return false;
        for(const auto& request:batch.Values())pending_.values[pending_.count++]=request;
        return true;
    }
    Pending Take() noexcept {auto result=pending_;pending_.count=0;return result;}
    void Clear() noexcept {pending_.count=0;}
private:
    bool enabled_=false;Pending pending_{};
};
}
