#include "../engine/scene/entity/WorldPlayback.h"
#include <iostream>
#include <stdexcept>

void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main()
{
    using namespace Engine::Scene;
    try {
        EntityWorld world;
        auto entity = world.CreateEntity("Authored");
        world.SetScript(entity, "move");
        Require(world.SetCamera(entity, {true, 0.8f, 0.2f, 500.0f}), "Authored camera could not be added");
        Engine::AnimationSystem::AnimatorComponent animator; animator.clip = "authored clip"; animator.speed = 0.5f; animator.loop = false; animator.playOnAwake = false;
        Require(world.SetAnimator(entity, animator), "Authored Animator could not be added");
        const auto authored = world.Capture().dump();
        Engine::Scripting::ScriptRuntime scripts;
        Engine::Physics::PhysicsWorld physics;
        int created = 0, destroyed = 0;
        scripts.Register("move", {
            [&](EntityRecord&) { ++created; },
            [](EntityRecord& entity, float dt) { entity.transform.position.x += dt; },
            [&](EntityRecord&) { ++destroyed; }});
        WorldPlayback playback;
        Require(playback.SimulationDelta(1.0f) == 0.0f, "Editing simulated");
        playback.Play(world);
        scripts.Update(world, playback.SimulationDelta(1.0f));
        world.CreateEntity("Runtime entity");
        world.Find(entity)->camera.fovY = 1.2f;
        auto runtimeAnimator = animator; runtimeAnimator.clip = "runtime clip"; runtimeAnimator.speed = 2; runtimeAnimator.loop = true;
        Require(world.SetAnimator(entity, runtimeAnimator), "Runtime Animator fixture failed");
        playback.Pause();
        Require(playback.SimulationDelta(1.0f) == 0.0f, "Paused simulated");
        playback.Play(world);
        scripts.Update(world, playback.SimulationDelta(1.0f));
        Require(created == 1, "Resume recreated script instance");
        std::string error;
        Require(playback.Stop(world, scripts, physics, error), "Stop failed");
        Require(world.Capture().dump() == authored, "Stop did not restore authored world");
        Require(world.Find(entity)->hasAnimator && world.Find(entity)->animator.clip == animator.clip && world.Find(entity)->animator.speed == animator.speed &&
            !world.Find(entity)->animator.loop && !world.Find(entity)->animator.playOnAwake, "Stop failed to restore authored Animator fields");
        Require(destroyed == 1, "Stop did not shutdown script instance");
        playback.Play(world);
        scripts.Update(world, playback.SimulationDelta(1.0f));
        Require(created == 2, "Second Play did not recreate script instance");
        const auto transient = world.CreateEntity("Deleted during Play");
        world.SetScript(transient, "move");
        scripts.Update(world, playback.SimulationDelta(1.0f));
        world.DestroyEntity(transient);
        // Stop must retire even an already-deleted runtime entity when no Update
        // occurs between deletion and stopping playback.
        Require(playback.Stop(world, scripts, physics, error), "Second Stop failed");
        Require(created == 3 && destroyed == 3 && scripts.GetRunningCount() == 0,
            "Stop lost the destruction hook of a deleted runtime entity");
        Require(world.Capture().dump() == authored, "Second Stop failed to restore the authored scene");
        std::cout << "PASS: edit/play/pause/resume/stop snapshot and script lifecycle\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
