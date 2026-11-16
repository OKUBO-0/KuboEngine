#include "../engine/editor/EntityHistory.h"
#include "../engine/scene/entity/WorldPlayback.h"
#include <iostream>
#include <stdexcept>

namespace {
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
}
int main()
{
    using namespace Engine::Scene;
    using Engine::Editor::EntityHistory;
    try {
        EntityWorld world;
        const auto root = world.CreateEntity("Root");
        const auto child = world.CreateEntity("Child", root);
        EntityId selection = child;
        EntityHistory history;
        std::string error;
        const auto initial = world.Capture();
        auto before = initial;
        world.Find(child)->transform.position.x = 1;
        Require(history.Record(before, world, selection, selection, error), error.c_str());
        before = world.Capture();
        world.Find(child)->transform.position.x = 3;
        Require(history.Record(before, world, selection, selection, error), error.c_str());
        Require(history.CanUndo(), "Pending edit not available for undo");
        history.Commit();
        Require(history.UndoCount() == 1, "Drag frames were not grouped");
        const auto dragged = world.Capture().dump();
        Require(history.Undo(world, selection, error), error.c_str());
        Require(world.Capture().dump() == initial.dump() && selection == child, "Drag undo failed");
        Require(history.Redo(world, selection, error), error.c_str());
        Require(world.Capture().dump() == dragged, "Drag redo failed");

        before = world.Capture();
        Require(world.SetCamera(child, {true, 0.8f, 0.2f, 500, true, 12}), "Orthographic camera addition failed");
        Require(world.SetScript(child, "Scripts/mover.kubo"), "Script addition failed");
        Require(world.SetCollider(child, {}), "Collider addition failed");
        Require(world.SetRigidBody(child, {}), "Body addition failed");
        Require(history.Record(before, world, child, child, error), error.c_str());
        history.Commit();
        const auto components = world.Capture().dump();
        Require(history.Undo(world, selection, error) && !world.Find(child)->hasCamera && !world.Find(child)->hasScript,
            "Component addition undo failed");
        Require(history.Redo(world, selection, error) && world.Capture().dump() == components, "Components redo failed");
        before = world.Capture();
        world.RemoveCamera(child);
        world.RemoveScript(child);
        Require(world.SetParent(child, 0), "Reparent failed");
        Require(history.Record(before, world, child, child, error), error.c_str());
        history.Commit();
        Require(history.Undo(world, selection, error) && world.Find(child)->hasCamera && world.Find(child)->hasScript &&
            world.Find(child)->parent == root, "Component removal/reparent undo failed");
        Require(history.Redo(world, selection, error) && !world.Find(child)->hasCamera && world.Find(child)->parent == 0,
            "Component removal/reparent redo failed");
        Require(history.Undo(world, selection, error), error.c_str());

        before = world.Capture();
        Require(world.DestroyEntity(root), "Deletion failed");
        selection = 0;
        Require(history.Record(before, world, root, selection, error), error.c_str());
        history.Commit();
        Require(history.Undo(world, selection, error) && selection == root && world.Find(child)->parent == root,
            "Delete undo did not restore hierarchy/selection");
        Require(history.Redo(world, selection, error) && selection == 0 && world.Find(child)->parent == 0,
            "Delete redo failed");

        before = world.Capture();
        const auto created = world.CreateEntity("Created");
        selection = created;
        Require(history.Record(before, world, 0, selection, error), error.c_str());
        history.Commit();
        Require(history.Undo(world, selection, error) && !world.Find(created) && selection == 0, "Create undo failed");
        Require(history.Redo(world, selection, error) && world.Find(created) && selection == created, "Create redo failed");
        Require(history.Undo(world, selection, error), error.c_str());
        before = world.Capture();
        world.Find(child)->name = "New branch";
        Require(history.Record(before, world, selection, child, error), error.c_str());
        selection = child;
        history.Commit();
        Require(!history.CanRedo(), "New edit retained the redo branch");

        const auto count = history.UndoCount();
        world.Find(child)->active = false; // unrecorded external mutation
        const auto changed = world.Capture().dump();
        Require(!history.Undo(world, selection, error) && world.Capture().dump() == changed && history.UndoCount() == count,
            "Undo overwrote an unrecorded external edit or lost the command");
        world.Find(child)->active = true;
        Require(history.Undo(world, selection, error), error.c_str());
        world.Find(child)->active = false;
        const auto redoCount = history.RedoCount();
        Require(!history.Redo(world, selection, error) && history.RedoCount() == redoCount, "Redo overwrote an external edit");
        world.Find(child)->active = true;
        Require(history.Redo(world, selection, error), error.c_str());

        history.Clear();
        before = world.Capture();
        world.Find(child)->transform.position.x += 1;
        Require(history.Record(before, world, child, child, error), error.c_str());
        const auto midway = world.Capture();
        Require(world.Restore(before, error), error.c_str());
        Require(history.Record(midway, world, child, child, error), error.c_str());
        history.Commit();
        Require(!history.CanUndo(), "Drag returning to start produced a command");
        Require(history.Record(before, world, 0, child, error), error.c_str());
        history.Commit();
        Require(!history.CanUndo(), "Selection-only change produced a command");

        before = world.Capture();
        world.Find(child)->name = "Temporary name";
        Require(history.Record(before, world, child, child, error), error.c_str());
        history.Commit();
        Require(history.Undo(world, selection, error), error.c_str());
        const auto noOpStart = world.Capture();
        world.Find(child)->transform.position.z += 1;
        Require(history.Record(noOpStart, world, child, child, error), error.c_str());
        const auto noOpMid = world.Capture();
        Require(world.Restore(noOpStart, error), error.c_str());
        Require(history.Record(noOpMid, world, child, child, error), error.c_str());
        history.Commit();
        Require(history.CanRedo(), "No-op edit erased redo history");
        history.Clear();

        // Bad document inputs must not change any history entry.
        world.Find(child)->camera.nearClip = -1;
        Require(!history.Record(before, world, child, child, error) && !history.CanUndo(), "Invalid after-state recorded");
        Require(world.Restore(before, error), error.c_str());
        auto invalidBefore = before;
        invalidBefore["entities"][0]["parent"] = 999.0;
        world.Find(child)->name = "Valid change";
        Require(!history.Record(invalidBefore, world, child, child, error), "Invalid before-state recorded");
        Require(world.Restore(before, error), error.c_str());

        // Count and byte limits evict the oldest commands, not the newest one.
        EntityHistory bounded(3);
        for (int i = 0; i < 6; ++i) {
            before = world.Capture();
            world.Find(child)->transform.position.x = float(i + 10);
            Require(bounded.Record(before, world, child, child, error), error.c_str());
            bounded.Commit();
        }
        Require(bounded.UndoCount() == 3, "Count limit was exceeded");
        for (int i = 0; i < 3; ++i) Require(bounded.Undo(world, selection, error), error.c_str());
        Require(!bounded.CanUndo() && bounded.RedoCount() == 3, "Bounded undo commands incorrect");
        const auto bytes = world.Capture().dump().size();
        EntityHistory byteBounded(100, bytes * 3);
        for (int i = 0; i < 5; ++i) {
            before = world.Capture();
            world.Find(child)->transform.position.y = float(i + 1);
            Require(byteBounded.Record(before, world, child, child, error), error.c_str());
            Require(byteBounded.StoredBytes() <= bytes * 3, "Pending edit exceeded byte budget");
            byteBounded.Commit();
        }
        Require(byteBounded.StoredBytes() <= bytes * 3 && byteBounded.UndoCount() == 1, "Byte limit was exceeded");
        EntityHistory tooSmall(100, 1);
        before = world.Capture();
        world.Find(child)->name = "Beyond budget";
        Require(!tooSmall.Record(before, world, child, child, error) && !tooSmall.CanUndo(), "Oversized edit was recorded");

        // Playback restores the authored state, allowing pre-play history to resume.
        EntityHistory playbackHistory;
        before = world.Capture();
        world.Find(child)->camera.fovY = 1;
        Require(playbackHistory.Record(before, world, child, child, error), error.c_str());
        playbackHistory.Commit();
        WorldPlayback playback;
        Engine::Scripting::ScriptRuntime scripts;
        Engine::Physics::PhysicsWorld physics;
        playback.Play(world);
        world.Find(child)->transform.position.x += 100;
        world.CreateEntity("Runtime only");
        Require(playback.Stop(world, scripts, physics, error), error.c_str());
        Require(playbackHistory.Undo(world, selection, error) && world.Capture().dump() == before.dump(),
            "Pre-play history did not survive runtime rollback");
        history.Clear();
        Require(!history.CanUndo() && !history.CanRedo() && history.StoredBytes() == 0, "Clear retained history");
        std::cout << "PASS: entity edit grouping, hierarchy/components/selection, redo branching, conflict isolation, limits and playback rollback\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
