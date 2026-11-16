#pragma once
#include "AnimatorComponent.h"

namespace Engine::AnimationSystem {
// Runtime intent only. Object3D owns the sole playback clock and sampled pose.
enum class AnimatorCommand { Play, Pause, Stop, Restart };
struct AnimatorBinding {
    bool authored = false, playing = true;
    AnimatorComponent settings{};
    void Swap(AnimatorBinding& other) noexcept {
        settings.Swap(other.settings);
        std::swap(authored, other.authored); std::swap(playing, other.playing);
    }
    bool Matches(const AnimatorComponent* requested) const noexcept {
        if (!requested) return !authored;
        return authored && settings.clip == requested->clip && settings.controllerGuid==requested->controllerGuid && settings.speed == requested->speed &&
            settings.loop == requested->loop && settings.playOnAwake == requested->playOnAwake;
    }
    bool NeedsNewOwner(const AnimatorComponent* requested) const noexcept {
        return authored != (requested != nullptr) || (requested && (settings.clip != requested->clip || settings.controllerGuid!=requested->controllerGuid));
    }
    // Caller must query its actual model. Prepare privately before mutating GPU owner.
    bool Prepare(const AnimatorComponent* requested, bool clipAvailable, bool newModel,
        AnimatorBinding& output) const noexcept {
        if (requested && (!requested->IsValid() || !clipAvailable)) return false;
        try {
            AnimatorBinding candidate;
            if (requested) {
                candidate.settings = *requested; candidate.authored = true;
                candidate.playing = newModel || NeedsNewOwner(requested) ? requested->playOnAwake : playing;
            }
            output.Swap(candidate); return true;
        } catch (...) { return false; }
    }
    bool Apply(AnimatorCommand command) noexcept {
        if (!authored) return false;
        switch (command) {
        case AnimatorCommand::Play: case AnimatorCommand::Restart: playing = true; return true;
        case AnimatorCommand::Pause: case AnimatorCommand::Stop: playing = false; return true;
        default: return false;
        }
    }
    float EffectiveSpeed() const noexcept { return playing ? settings.speed : 0.0f; }
    // World settings may have changed since the last rendering sync (Stop/Lua).
    // Same admitted clip uses latest playOnAwake; never thaw an unbound draft.
    void ResetIntent(const AnimatorComponent* requested) noexcept {
        if (authored) playing = requested && requested->IsValid() && settings.clip == requested->clip && settings.controllerGuid==requested->controllerGuid
            ? requested->playOnAwake : settings.playOnAwake;
        else playing = requested == nullptr;
    }
};
}
