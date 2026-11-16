#include "../engine/editor/SceneHistory.h"
#include <iostream>
#include <stdexcept>
void Require(bool condition, const char* message) { if (!condition) { throw std::runtime_error(message); } }
int main()
{
    using Engine::Scene::SceneValue;
    using Engine::Editor::SceneHistory;
    SceneValue a; a["position"] = 0.0; a["color"] = 1.0;
    auto b = a; b["position"] = 1.0;
    auto c = b; c["position"] = 2.0;
    SceneHistory history;
    history.Record(a, b); history.Record(b, c); history.Commit();
    Require(history.UndoCount() == 1, "A continuous drag must be one command");
    auto runtime = c; runtime["color"] = 0.5;
    auto undone = history.UndoValue(runtime); history.DidUndo();
    Require(undone["position"].get<double>() == 0 && undone["color"].get<double>() == 0.5,
        "Undo must preserve unrelated runtime changes");
    auto redone = history.RedoValue(undone); history.DidRedo();
    Require(redone.dump() == runtime.dump(), "Redo must restore the last drag value");
    history.DidUndo(); history.Record(a, b); history.Commit();
    Require(!history.CanRedo(), "A new edit must invalidate the redo branch");
    history.Clear(); history.Record(a, b); history.Record(b, a); history.Commit();
    Require(!history.CanUndo(), "A drag returning to its start must be a no-op");
    for (int i = 0; i < 105; ++i) { history.Record(a, b); history.Commit(); }
    Require(history.UndoCount() == 100, "History must remain bounded");
    std::cout << "PASS: grouped edits, property isolation, redo branching, no-op and bounded history\n";
}
