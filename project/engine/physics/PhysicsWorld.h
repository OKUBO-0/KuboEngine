#pragma once

#include "PhysicsComponents.h"
#include "../scene/entity/EntityWorld.h"

#include <cstdint>
#include <cstddef>
#include <functional>
#include <vector>
#include <string>
#include <span>
#include <utility>

namespace Engine::Physics {

struct CollisionEvent {
    Scene::EntityId first = 0;
    Scene::EntityId second = 0;
    Vector3 normal{0.0f, 0.0f, 0.0f};
    float penetration = 0.0f;
    bool trigger = false;
};

struct PhysicsAdvanceUsage {
    size_t steps=0,discardedEvents=0;
    double consumedSeconds=0,droppedSeconds=0,remainingSeconds=0;
};

struct TriggerEvent {
    Scene::EntityId first = 0;
    Scene::EntityId second = 0;
    bool entered = false;
};

struct PhysicsQueryOverride {Scene::EntityId id=0;Scene::TransformComponent transform{};bool active=true;};
struct PhysicsQueryFilter {Scene::EntityId ignore=0;bool includeTriggers=true;};
struct PhysicsRayHit {Scene::EntityId entity=0;float distance=0;Vector3 position{},normal{};bool trigger=false;};
enum class PhysicsQueryResult {Success,Invalid,Limit,AllocationFailure};
// Logical work accounting, not an OS deadline/RSS sandbox. Shared per Lua hook;
// native callers may use a fresh budget per query or retain one for a batch.
struct PhysicsQueryBudget {
    static constexpr size_t MaxQueries=64,MaxEntities=10000,MaxWork=262144,MaxHits=1024,MaxOverrides=65;
    size_t queries=0,work=0;bool exhausted=false;
};

/// @brief Lightweight deterministic box, sphere and capsule physics for entity worlds.
class PhysicsWorld final {
public:
    // Current-pose shapes; boxes use their world AABB. Inclusive
    // touching; nearest hit then lowest ID. Inside/on-boundary ray: t=0,n=0.
    // Success miss: hit.entity=0. Failures leave caller output unchanged.
    static PhysicsQueryResult Raycast(const Scene::EntityWorld& world,Vector3 origin,Vector3 direction,float maximumDistance,
        PhysicsRayHit& hit,PhysicsQueryFilter filter={},std::span<const PhysicsQueryOverride> overrides={},PhysicsQueryBudget* budget=nullptr) noexcept;
    static PhysicsQueryResult OverlapBox(const Scene::EntityWorld& world,Vector3 center,Vector3 halfExtents,
        std::vector<Scene::EntityId>& hits,PhysicsQueryFilter filter={},std::span<const PhysicsQueryOverride> overrides={},PhysicsQueryBudget* budget=nullptr) noexcept;
    // Avoid low-speed bounce jitter at supporting contacts (world units/second).
    static constexpr double RestitutionVelocityThreshold = 0.5;
    void SetGravity(Vector3 gravity) { gravity_ = gravity; }
    Vector3 GetGravity() const { return gravity_; }

    void Step(Scene::EntityWorld& world, float deltaSeconds);
    // Host frame scheduler; Step remains the explicit single-step API.
    static constexpr double FixedStepSeconds=1.0/60.0;
    static constexpr size_t MaximumFrameSteps=8,MaximumFrameEvents=65536;
    // Trigger edges are dispatched after each successful fixed step, before
    // the next input sample is consumed. A zero-step frame preserves contacts.
    void Advance(Scene::EntityWorld& world,float frameSeconds,const std::function<void(float)>& beforeStep={},
        const std::function<void(std::span<const TriggerEvent>)>& onTriggers={});
    const PhysicsAdvanceUsage& GetAdvanceUsage() const noexcept {return advanceUsage_;}
    void ResetSimulation() {ClearEvents();activeTriggers_.clear();accumulator_=0;advanceUsage_={};}

    const std::vector<CollisionEvent>& GetEvents() const { return events_; }
    void ClearEvents() { events_.clear(); diagnostic_.clear(); }
    const std::string& GetDiagnostic() const {return diagnostic_;}

private:
    using TriggerPair = std::pair<Scene::EntityId,Scene::EntityId>;
    bool StepInternal(Scene::EntityWorld& world,float deltaSeconds,std::vector<TriggerPair>* triggers);
    std::vector<TriggerPair> activeTriggers_;
    Vector3 gravity_{0.0f, -9.8f, 0.0f};
    std::vector<CollisionEvent> events_;
    std::string diagnostic_;
    double accumulator_=0;
    PhysicsAdvanceUsage advanceUsage_;
};

} // namespace Engine::Physics
