#pragma once
#include "EntityWorld.h"
#include "../../script/ScriptRuntime.h"
#include "../../physics/PhysicsWorld.h"

namespace Engine::Scene {
class WorldPlayback final {
public:
    enum class State { Editing, Playing, Paused };
    State GetState() const { return state_; }
    bool Play(const EntityWorld& world)
    {
        if (state_ == State::Playing) return false;
        if (state_ == State::Editing) snapshot_ = world.Capture();
        state_ = State::Playing;
        return true;
    }
    void Pause() { if (state_ == State::Playing) state_ = State::Paused; }
    bool Stop(EntityWorld& world, Scripting::ScriptRuntime& scripts, Physics::PhysicsWorld& physics, std::string& error)
    {
        if (state_ == State::Editing) return false;
        EntityWorld restored;
        if (!restored.Restore(snapshot_, error)) return false;
        scripts.Shutdown(world);
        world = std::move(restored);
        physics.ResetSimulation();
        state_ = State::Editing;
        snapshot_ = SceneValue{};
        return true;
    }
    float SimulationDelta(float delta) const { return state_ == State::Playing ? delta : 0.0f; }
private:
    State state_ = State::Editing;
    SceneValue snapshot_;
};
}
