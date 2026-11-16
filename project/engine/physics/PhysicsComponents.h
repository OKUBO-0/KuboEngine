#pragma once

#include "../math/types/Vector3.h"
#include <cmath>

namespace Engine::Physics {
enum class ColliderShape { Box, Sphere, Capsule };

struct ColliderComponent {
    Vector3 halfExtents{0.5f, 0.5f, 0.5f};
    bool isTrigger = false;
    // Surface material. Zero defaults retain legacy frictionless/inelastic contacts.
    float friction = 0.0f;
    float restitution = 0.0f;
    ColliderShape shape = ColliderShape::Box;
    float radius = 0.5f;
    // Capsule centerline half length, excluding the hemispheres, along local Y.
    float halfSegment = 0.5f;

    bool HasValidShape() const noexcept {
        const auto nonnegative = [](float value) {return std::isfinite(value) && value>=0;};
        if (!nonnegative(halfExtents.x) || !nonnegative(halfExtents.y) || !nonnegative(halfExtents.z) ||
            !nonnegative(halfSegment) || !std::isfinite(radius) || radius<=0) return false;
        return shape==ColliderShape::Box || shape==ColliderShape::Sphere || shape==ColliderShape::Capsule;
    }
};

struct RigidBodyComponent {
    Vector3 velocity{0.0f, 0.0f, 0.0f}; // World-space linear velocity, independent of the parent frame.
    float mass = 1.0f;
    bool dynamic = true;
    bool useGravity = true;
    bool grounded = false;
};

} // namespace Engine::Physics
