#pragma once

#include "../math/types/Vector3.h"

namespace Engine::Physics {

struct ColliderComponent {
    Vector3 halfExtents{0.5f, 0.5f, 0.5f};
    bool isTrigger = false;
    // Surface material. Zero defaults retain legacy frictionless/inelastic contacts.
    float friction = 0.0f;
    float restitution = 0.0f;
};

struct RigidBodyComponent {
    Vector3 velocity{0.0f, 0.0f, 0.0f}; // World-space linear velocity, independent of the parent frame.
    float mass = 1.0f;
    bool dynamic = true;
    bool useGravity = true;
    bool grounded = false;
};

} // namespace Engine::Physics
