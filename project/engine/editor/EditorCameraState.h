#pragma once
#include "Vector3.h"
#include "ScenePicking.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace Engine::Editor {
struct EditorCameraMotion {
    float forward = 0, right = 0, up = 0;
    float mouseX = 0, mouseY = 0, wheel = 0;
    bool navigate = false, accelerate = false;
};
// Transient editor state, never part of an authored scene or playback snapshot.
struct EditorCameraState {
    Vector3 position{0, 2, -10}, rotation{};
    bool orthographic = false;
    float height = 10;
    float farClip = 1000;
    float speed = 5;
    Vector3 Forward() const {
        return {std::sin(rotation.y) * std::cos(rotation.x), -std::sin(rotation.x), std::cos(rotation.y) * std::cos(rotation.x)};
    }
    void Reset() { *this = {}; }
    bool Focus(Vector3 center, float distance = 10) {
        if (!std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(center.z) ||
            !std::isfinite(distance) || distance <= 0) return false;
        const auto direction = Forward();
        const Vector3 next{center.x - direction.x * distance, center.y - direction.y * distance, center.z - direction.z * distance};
        if (!std::isfinite(next.x) || !std::isfinite(next.y) || !std::isfinite(next.z)) return false;
        position = next; return true;
    }
    bool FrameBounds(const SceneBounds& bounds, float fov = 0.45f, float aspect = 16.0f/9.0f) {
        if (!ValidBounds(bounds) || !std::isfinite(fov) || fov<=0 || fov>=std::numbers::pi_v<float> || !std::isfinite(aspect) || aspect<=0) return false;
        const double x=(double(bounds.max.x)-bounds.min.x)*0.5, y=(double(bounds.max.y)-bounds.min.y)*0.5, z=(double(bounds.max.z)-bounds.min.z)*0.5;
        const double radius=(std::max)(0.05,std::sqrt(x*x+y*y+z*z));
        const double angle=(std::min)(double(fov)*0.5,std::atan(std::tan(double(fov)*0.5)*aspect));
        const float distance=float(radius*1.2/std::sin(angle));
        const float span=float(radius*2.4*(std::max)(1.0,1.0/aspect));
        const float clip=float((std::max)(1000.0,double(distance)+radius*2));
        if (!std::isfinite(distance) || !std::isfinite(span) || !std::isfinite(clip)) return false;
        const Vector3 center{float((double(bounds.min.x)+bounds.max.x)*0.5),float((double(bounds.min.y)+bounds.max.y)*0.5),float((double(bounds.min.z)+bounds.max.z)*0.5)};
        if (!Focus(center,distance)) return false;
        if (orthographic) height=span;
        farClip=clip; return true;
    }
    void Update(const EditorCameraMotion& motion, float delta, bool allowed) {
        if (!allowed || !std::isfinite(delta) || delta <= 0 ||
            !std::isfinite(motion.forward) || !std::isfinite(motion.right) || !std::isfinite(motion.up) ||
            !std::isfinite(motion.mouseX) || !std::isfinite(motion.mouseY) || !std::isfinite(motion.wheel)) return;
        delta = std::clamp(delta, 0.0f, 0.1f);
        if (motion.navigate) {
            rotation.x = std::clamp(rotation.x + std::clamp(motion.mouseY, -1000.0f, 1000.0f) * 0.003f, -1.5f, 1.5f);
            rotation.y = std::remainder(rotation.y + std::clamp(motion.mouseX, -1000.0f, 1000.0f) * 0.003f, 2 * std::numbers::pi_v<float>);
            const float f = std::clamp(motion.forward, -1.0f, 1.0f), r = std::clamp(motion.right, -1.0f, 1.0f), u = std::clamp(motion.up, -1.0f, 1.0f);
            const float length = std::sqrt(f*f + r*r + u*u);
            const float baseSpeed = std::isfinite(speed) ? std::clamp(speed,0.1f,100.0f) : 5.0f;
            const float step = delta * baseSpeed * (motion.accelerate ? 3.0f : 1.0f) / (std::max)(1.0f, length);
            const auto forward = Forward();
            position.x += (forward.x*f + std::cos(rotation.y)*r) * step;
            position.y += (forward.y*f + u) * step;
            position.z += (forward.z*f - std::sin(rotation.y)*r) * step;
        }
        const float wheel = std::clamp(motion.wheel, -10.0f, 10.0f);
        if (orthographic) { if (wheel != 0) height = std::clamp(height * std::exp(-wheel * 0.15f), 0.01f, 100000.0f); }
        else { const auto direction = Forward(); position.x += direction.x*wheel; position.y += direction.y*wheel; position.z += direction.z*wheel; }
    }
};
}
