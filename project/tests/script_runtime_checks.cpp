#include "../engine/script/ScriptRuntime.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <Windows.h>

namespace {
void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void CheckInstanceTransitions()
{
    using namespace Engine::Scene;
    using namespace Engine::Scripting;
    EntityWorld world;
    const auto id = world.CreateEntity("Lifecycle");
    world.SetScript(id, "first");
    ScriptRuntime runtime;
    int firstCreated = 0, firstDestroyed = 0, secondCreated = 0, secondDestroyed = 0;
    const ScriptCallbacks first{
        [&](EntityRecord&) { ++firstCreated; },
        [](EntityRecord&, float) {},
        [&](EntityRecord&) { ++firstDestroyed; }};
    runtime.Register("first", first);
    runtime.Register("second", {
        [&](EntityRecord&) { ++secondCreated; },
        [](EntityRecord&, float) {},
        [&](EntityRecord&) { ++secondDestroyed; }});
    runtime.Update(world, 0.1f);
    world.Find(id)->script.enabled = false;
    runtime.Update(world, 0.1f);
    runtime.Update(world, 0.1f);
    Require(firstDestroyed == 1 && runtime.GetRunningCount() == 0 && !world.Find(id)->script.started,
        "Disable did not retire the instance exactly once");
    world.Find(id)->script.enabled = true;
    runtime.Update(world, 0.1f);
    Require(firstCreated == 2, "Reenable did not start a fresh instance");
    world.SetScript(id, "second");
    runtime.Update(world, 0.1f);
    Require(firstDestroyed == 2 && secondCreated == 1, "Replacement lost the old destruction callback");
    world.RemoveScript(id);
    runtime.Update(world, 0.1f);
    Require(secondDestroyed == 1 && runtime.GetRunningCount() == 0, "Component removal leaked an instance");
    world.SetScript(id, "first");
    runtime.Update(world, 0.1f);
    runtime.Unregister("first");
    runtime.Update(world, 0.1f);
    Require(firstDestroyed == 3, "Unregister discarded a running destruction callback");
    runtime.Register("first", first);
    runtime.Update(world, 0.1f);
    runtime.Register("first", first);
    runtime.Update(world, 0.1f);
    Require(firstCreated == 5 && firstDestroyed == 4, "Registration revision did not restart the instance");
    runtime.Clear();
    runtime.Shutdown(world);
    runtime.Shutdown(world);
    Require(firstDestroyed == 5 && runtime.GetRunningCount() == 0, "Clear/shutdown lost or duplicated destruction");

    runtime.Register("first", first);
    runtime.Update(world, 0.1f);
    world.Find(id)->active = false;
    runtime.Update(world, 0.1f);
    Require(firstDestroyed == 6, "Inactive entity kept a script instance");
    world.Find(id)->active = true;
    runtime.Update(world, 0.1f);
    world.DestroyEntity(id);
    runtime.Update(world, 0.1f);
    Require(firstDestroyed == 7 && runtime.GetRunningCount() == 0, "Entity deletion leaked an instance");

    const auto mutableId = world.CreateEntity("Mutating callback");
    world.SetScript(mutableId, "mutation");
    int mutationDestroyed = 0;
    runtime.Register("mutation", {
        [&](EntityRecord&) { for (int i = 0; i < 128; ++i) world.CreateEntity("Created in hook"); },
        [&](EntityRecord& entity, float) {
            Require(entity.id == mutableId, "Create hook invalidated the update record");
            world.DestroyEntity(mutableId);
        },
        [&](EntityRecord& entity) {
            Require(entity.id == mutableId && entity.name == "Mutating callback", "Deleted instance lost its retained record");
            ++mutationDestroyed;
        }});
    runtime.Update(world, 0.1f);
    Require(!world.Find(mutableId) && mutationDestroyed == 1 && runtime.GetRunningCount() == 0,
        "Self deletion in a callback was not reconciled");

    EntityWorld reloadWorld;
    const auto reloadId = reloadWorld.CreateEntity("Reload");
    reloadWorld.SetScript(reloadId, "reload");
    ScriptRuntime reloadRuntime;
    int oldDestroyed = 0, newCreated = 0, newDestroyed = 0;
    reloadRuntime.Register("reload", {
        {}, [](EntityRecord&, float) {}, [&](EntityRecord&) { ++oldDestroyed; }});
    reloadRuntime.Update(reloadWorld, 0.1f);
    reloadRuntime.Register("reload", {
        [&](EntityRecord&) { ++newCreated; }, [](EntityRecord&, float) {},
        [&](EntityRecord&) { ++newDestroyed; }});
    reloadRuntime.Update(reloadWorld, 0.1f);
    Require(oldDestroyed == 1 && newCreated == 1 && newDestroyed == 0,
        "Reload used the new destruction callback for the old instance");
    reloadRuntime.Shutdown(reloadWorld);
    Require(oldDestroyed == 1 && newDestroyed == 1, "Reloaded instance did not own its new callbacks");
}
}

int main()
{
    using namespace Engine::Scene;
    using namespace Engine::Scripting;
    try {
        CheckInstanceTransitions();
        EntityWorld world;
        const EntityId entity = world.CreateEntity("Mover");
        Require(world.SetScript(entity, "Scripts/mover.kubo.lua"), "Script component was not assigned");
        int created = 0;
        int updated = 0;
        int destroyed = 0;
        ScriptRuntime runtime;
        Require(runtime.Register("Scripts/mover.kubo.lua", {
            [&created](EntityRecord&) { ++created; },
            [&updated](EntityRecord& record, float dt) { ++updated; record.transform.position.x += dt; },
            [&destroyed](EntityRecord&) { ++destroyed; },
        }), "Script callback registration failed");
        runtime.Update(world, 0.5f);
        runtime.Update(world, 0.25f);
        Require(created == 1 && updated == 2, "Script lifecycle was not dispatched");
        Require(std::fabs(world.Find(entity)->transform.position.x - 0.75f) < 0.001f, "Script update did not mutate entity");

        const auto snapshot = world.Capture();
        EntityWorld restored;
        std::string error;
        Require(restored.Restore(snapshot, error), error.c_str());
        Require(restored.Find(entity)->hasScript && restored.Find(entity)->script.assetPath == "Scripts/mover.kubo.lua", "Script component was not persisted");
        runtime.Shutdown(world);
        Require(destroyed == 1, "Script destroy callback was not dispatched");
        runtime.Clear();
        Require(runtime.GetRegisteredCount() == 0, "Script runtime was not cleared");
        const auto scriptRoot = std::filesystem::temp_directory_path() / ("kubo-script-assets-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
        std::filesystem::create_directories(scriptRoot / "Scripts");
        { std::ofstream output(scriptRoot / "Scripts/mover.kubo", std::ios::binary); output << "on_create set_active true\non_update translate 2 0 0\n"; }
        EntityWorld assetWorld;
        const auto assetEntity = assetWorld.CreateEntity("AssetMover");
        Require(assetWorld.SetScript(assetEntity, "Scripts/mover.kubo"), "Asset script component was not assigned");
        ScriptRuntime assetRuntime;
        Require(assetRuntime.LoadWorldAssets(assetWorld, scriptRoot, error), error.c_str());
        assetRuntime.Update(assetWorld, 0.5f);
        Require(std::fabs(assetWorld.Find(assetEntity)->transform.position.x - 1.0f) < 0.001f, "Text script did not update entity");
        std::filesystem::remove_all(scriptRoot);
        std::cout << "PASS: script persistence, disable/remove/replace/reload/shutdown lifecycle and callback world mutation\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
