#include "../engine/editor/EntityDocumentState.h"
#include "../engine/editor/EntityHistory.h"
#include "../engine/scene/entity/WorldPlayback.h"
#include "../engine/base/core/CloseRequestGate.h"
#include <Windows.h>
#include <iostream>
#include <stdexcept>

namespace {
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
}
int main()
{
    using namespace Engine::Scene;
    using Engine::Editor::EntityDocumentState;
    using Action = EntityDocumentState::Action;
    const auto fixture = std::filesystem::temp_directory_path() /
        (L"kubo-document-checks-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    try {
        std::filesystem::create_directories(fixture);
        const auto path = fixture / "main.scene";
        EntityWorld world;
        EntityId selected = world.CreateEntity("Authored");
        std::string error;
        Require(world.Save(path, error), error.c_str());
        const auto initial = world.Capture();
        EntityDocumentState document;
        document.Opened(world, path);
        Require(!document.IsDirty() && !document.HasPendingAction(), "Opened document is dirty");
        Require(document.Generation()==1,"Initial document session generation missing");
        auto firstSession=document;Require(firstSession.HasSameState(document),"Copied document lost session identity");
        document.Opened(world,path);
        Require(document.Generation()==2 && !firstSession.HasSameState(document) && document.Path()==firstSession.Path() && !document.IsDirty(),"Identical same-path reopen retained prior session identity");
        const auto currentGeneration=document.Generation();
        document.MarkSaved(world);Require(document.SavedAs(world,path) && document.Generation()==currentGeneration,"Save advanced document session");
        Require(document.Request(Action::Close) && !document.NeedsConfirmation(), "Clean close needs confirmation");
        Require(document.TakeReadyAction() == Action::Close && document.TakeReadyAction() == Action::None, "Clean action not consumed exactly once");

        Engine::Editor::EntityHistory history;
        world.Find(selected)->name = "Edited";
        Require(history.Record(initial, world, selected, selected, error), error.c_str());
        history.Commit();
        document.Observe(world);
        Require(document.IsDirty(), "Edit did not dirty the document");
        Require(history.Undo(world, selected, error), error.c_str());
        document.Observe(world);
        Require(!document.IsDirty(), "Undo to saved baseline remains dirty");
        Require(history.Redo(world, selected, error), error.c_str());
        document.Observe(world);
        Require(document.IsDirty(), "Redo did not dirty the document");
        Require(document.Request(Action::Reload) && document.NeedsConfirmation(), "Dirty reload bypassed confirmation");
        Require(!document.Request(Action::Close), "Second action replaced the pending reload");
        Require(document.TakeReadyAction() == Action::None, "Unconfirmed action proceeded");
        document.Cancel();
        Require(document.IsDirty() && !document.HasPendingAction() && world.Find(selected)->name == "Edited", "Cancel discarded edits");

        Require(document.Request(Action::Close), "Close request failed");
        const HANDLE locked = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        Require(locked != INVALID_HANDLE_VALUE, "Could not lock save destination");
        const bool savedWhileLocked = world.Save(path, error);
        CloseHandle(locked);
        Require(!savedWhileLocked && !document.SavedAndProceed() && document.NeedsConfirmation() && document.IsDirty(),
            "Failed save cleared dirty state or allowed close");
        EntityWorld disk;
        Require(disk.Load(path, error) && disk.Capture().dump() == initial.dump(), "Failed save changed disk data");
        const auto alternate = fixture / "alternate.scene";
        Require(world.Save(alternate, error), error.c_str());
        Require(!document.MarkSavedAt(world, alternate) && document.IsDirty(), "Alternate MCP save cleared current dirty state");
        Require(!document.SavedAndProceed(), "Alternate save approved a destructive action");
        Require(world.Save(path, error), error.c_str());
        Require(document.MarkSavedAt(world, fixture / "MAIN.SCENE"), "Windows case alias did not match the saved document");
        Require(!document.IsDirty() && document.SavedAndProceed(), "Successful save did not approve close");
        Require(document.TakeReadyAction() == Action::Close, "Saved close did not proceed");

        world.Find(selected)->transform.position.x = 7;
        document.Observe(world);
        const auto edited = world.Capture().dump();
        Require(document.Request(Action::Reload), "Reload request failed");
        document.Discard();
        Require(document.IsDirty() && world.Capture().dump() == edited, "Discard approval prematurely overwrote the world");
        Require(document.TakeReadyAction() == Action::Reload, "Discarded reload was not approved");
        Require(!world.Load(fixture / "missing.scene", error) && world.Capture().dump() == edited && document.IsDirty(),
            "Failed reload discarded the current edits");
        Require(world.Load(path, error), error.c_str());
        document.Opened(world, path);
        Require(!document.IsDirty() && !document.HasPendingAction(), "Successful reload retained stale document state");

        // Runtime changes must not be compared against the saved authored baseline.
        WorldPlayback playback;
        Engine::Scripting::ScriptRuntime scripts;
        Engine::Physics::PhysicsWorld physics;
        playback.Play(world);
        world.Find(selected)->name = "Runtime only";
        world.CreateEntity("Runtime entity");
        Require(!document.IsDirty(), "Playback changed authored dirty state without observation");
        Require(playback.Stop(world, scripts, physics, error), error.c_str());
        document.Observe(world);
        Require(!document.IsDirty(), "Stopped clean playback dirtied the document");
        const auto playbackGeneration=document.Generation();
        world.Find(selected)->name = "Unsaved before play";
        document.Observe(world);
        playback.Play(world);
        world.Find(selected)->name = "Another runtime edit";
        Require(playback.Stop(world, scripts, physics, error), error.c_str());
        document.Observe(world);
        Require(document.IsDirty() && world.Find(selected)->name == "Unsaved before play", "Playback lost pre-play authored dirty state");
        Require(document.Generation()==playbackGeneration,"Playback or authored edits advanced document session");

        const auto openTarget=fixture/L"日本語/別.scene";
        const auto beforeOpen=world.Capture();
        Require(!document.Request(Action::Open) && !document.RequestOpen({}),"Open action accepted without a target");
        Require(document.RequestOpen(openTarget) && document.PendingTarget()==openTarget && document.NeedsConfirmation(),"Dirty Scene Open bypassed confirmation or lost target");
        Require(!document.RequestOpen(path) && !document.Request(Action::Close) && document.PendingTarget()==openTarget,"Second request replaced pending Open target");
        Require(document.TakeReadyAction()==Action::None && world.Capture()==beforeOpen,"Unconfirmed Open proceeded");
        document.Cancel();
        Require(document.PendingTarget().empty() && document.IsDirty() && world.Capture()==beforeOpen,"Cancelled Open lost edits or kept target");
        Require(document.RequestOpen(openTarget),"Open retry failed");
        Require(!document.SavedAndProceed() && document.NeedsConfirmation(),"Dirty Open approved without a save");
        document.Discard();
        const auto capturedTarget=document.PendingTarget();
        Require(document.TakeReadyAction()==Action::Open && capturedTarget==openTarget && document.PendingTarget().empty(),"Approved Open did not consume target/action once");
        Require(document.IsDirty() && world.Capture()==beforeOpen,"Open approval prematurely discarded live edits");
        Require(!world.Load(openTarget,error) && world.Capture()==beforeOpen && document.IsDirty(),"Failed Open lost authored edits");
        Require(document.RequestOpen(openTarget) && world.Save(path,error),error.c_str());
        document.MarkSaved(world);
        Require(document.SavedAndProceed() && document.TakeReadyAction()==Action::Open,"Saved Open did not proceed");
        EntityWorld replacement;replacement.CreateEntity("Replacement");Require(replacement.Save(openTarget,error),error.c_str());
        Require(world.Load(openTarget,error),error.c_str());document.Opened(world,openTarget);
        Require(document.Path()==openTarget && !document.IsDirty() && !document.HasPendingAction(),"Successful Open retained old dirty/path/pending state");

        Engine::Base::CloseRequestGate gate;
        Require(!gate.OnClose() && !gate.Consume(), "Player close was deferred by default");
        gate.Enable(true);
        Require(gate.OnClose() && gate.OnClose() && gate.Consume() && !gate.Consume(), "Editor close was not latched exactly once");
        gate.OnClose();
        gate.Enable(false);
        Require(!gate.Consume() && !gate.OnClose(), "Disabling close confirmation left a stale request");
        std::filesystem::remove_all(fixture);
        std::cout << "PASS: authored dirty baseline, undo/redo, save/discard/cancel, failed save/load isolation, MCP save path, playback and close gate\n";
        return 0;
    } catch (const std::exception& exception) {
        std::filesystem::remove_all(fixture);
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
