#pragma once
// Headless policy prototype. Object3D remains the production clock owner.
#include "animator_component_candidate.h"
#include "../engine/3d/model/AnimationTime.h"
namespace AnimatorPlaybackCandidate {
enum class Command { Play, Pause, Stop, Restart };
struct State {
    AnimatorComponentCandidate::Component authored;
    float time = 0;
    bool bound = false, playing = false;
    void Swap(State& other) noexcept {
        authored.Swap(other.authored); std::swap(time, other.time);
        std::swap(bound, other.bound); std::swap(playing, other.playing);
    }
    bool Configure(const AnimatorComponentCandidate::Component& next, bool clipAvailable) noexcept {
        if (!next.IsValid() || !clipAvailable) return false;
        try {
            State candidate;
            candidate.authored = next;
            candidate.bound = true;
            const bool reset = !bound || authored.clip != next.clip;
            candidate.time = reset ? 0 : time;
            candidate.playing = reset ? next.playOnAwake : playing;
            Swap(candidate); return true;
        } catch (...) { return false; }
    }
    bool Apply(Command command) noexcept {
        if (!bound) return false;
        switch (command) {
        case Command::Play: playing = true; break;
        case Command::Pause: playing = false; break;
        case Command::Stop: playing = false; time = 0; break;
        case Command::Restart: playing = true; time = 0; break;
        default: return false;
        }
        return true;
    }
    bool Tick(float duration, float delta) noexcept {
        if (!bound || !std::isfinite(duration) || duration <= 0 || !std::isfinite(delta) || delta < 0) return false;
        if (playing) time = Engine::Graphics3D::AdvanceAnimationSeconds(time, duration, delta, authored.speed, authored.loop);
        return true;
    }
};
}
