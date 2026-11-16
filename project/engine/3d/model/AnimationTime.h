#pragma once
#include <algorithm>
#include <cmath>

namespace Engine::Graphics3D {

// Controller exit intervals follow the rendered clip clock. A stationary
// zero-duration clip completes once on a positive playback step, even if looped;
// it has no elapsed duration from which to manufacture additional cycles.
inline double AdvanceControllerNormalizedTime(double previous, float currentSeconds,
    float duration, float deltaTime, float speed, bool loop)
{
    if (!std::isfinite(previous) || previous < 0 || previous > 1e9 ||
        !std::isfinite(duration) || duration < 0 ||
        !std::isfinite(deltaTime) || deltaTime <= 0 ||
        !std::isfinite(speed) || speed <= 0) return previous;
    if (duration == 0) return (std::max)(previous, 1.0);
    if (loop) return (std::min)(1e9, previous + double(deltaTime) * speed / duration);
    if (!std::isfinite(currentSeconds) || currentSeconds < 0) return previous;
    return (std::min)(1.0, double(currentSeconds) / duration);
}

inline float AdvanceAnimationSeconds(float current, float duration, float deltaTime, float speed, bool loop)
{
    if (!std::isfinite(duration) || duration <= 0.0f) {
        return 0.0f;
    }
    const double time = std::isfinite(current) ? (std::max)(0.0, static_cast<double>(current)) : 0.0;
    const double delta = std::isfinite(deltaTime) ? (std::max)(0.0, static_cast<double>(deltaTime)) : 0.0;
    const double rate = std::isfinite(speed) ? (std::max)(0.0, static_cast<double>(speed)) : 0.0;
    const double next = time + delta * rate;
    return static_cast<float>(loop ? std::fmod(next, duration) : (std::min)(next, static_cast<double>(duration)));
}

}
