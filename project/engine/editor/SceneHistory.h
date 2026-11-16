#pragma once
#include "engine/scene/core/SceneDocument.h"
#include <algorithm>
#include <string>
#include <vector>

namespace Engine::Editor {

// Store only authored property changes, so undo never rewinds unrelated simulation state.
class SceneHistory {
    using J = Engine::Scene::SceneValue;
    struct Change { std::vector<std::string> path; J before; J after; };
    using Command = std::vector<Change>;
public:
    void Record(const J& before, const J& after)
    {
        Command changes;
        Diff(before, after, {}, changes);
        for (auto& change : changes) {
            const auto found = std::find_if(pending_.begin(), pending_.end(), [&](const Change& c) { return c.path == change.path; });
            if (found == pending_.end()) { pending_.push_back(std::move(change)); }
            else { found->after = std::move(change.after); }
        }
    }
    void Commit()
    {
        std::erase_if(pending_, [](const Change& c) { return c.before.dump() == c.after.dump(); });
        if (pending_.empty()) { return; }
        undo_.push_back(std::move(pending_)); pending_.clear(); redo_.clear();
        if (undo_.size() > 100) { undo_.erase(undo_.begin()); }
    }
    bool CanUndo() const { return !undo_.empty(); }
    bool CanRedo() const { return !redo_.empty(); }
    size_t UndoCount() const { return undo_.size(); }
    void Clear() { pending_.clear(); undo_.clear(); redo_.clear(); }
    J UndoValue(J current) const { if (CanUndo()) { Apply(current, undo_.back(), false); } return current; }
    J RedoValue(J current) const { if (CanRedo()) { Apply(current, redo_.back(), true); } return current; }
    void DidUndo() { if (CanUndo()) { redo_.push_back(std::move(undo_.back())); undo_.pop_back(); } }
    void DidRedo() { if (CanRedo()) { undo_.push_back(std::move(redo_.back())); redo_.pop_back(); } }
private:
    static void Diff(const J& a, const J& b, std::vector<std::string> path, Command& result)
    {
        if (a.dump() == b.dump()) { return; }
        if (a.is_object() && b.is_object() && a.get<crude_json::object>().size() == b.get<crude_json::object>().size()) {
            for (const auto& [key, value] : a.get<crude_json::object>()) {
                if (!b.contains(key)) { result.push_back({path, a, b}); return; }
            }
            for (const auto& [key, value] : a.get<crude_json::object>()) {
                auto child = path; child.push_back(key); Diff(value, b[key], std::move(child), result);
            }
        } else { result.push_back({std::move(path), a, b}); }
    }
    static void Apply(J& value, const Command& command, bool forward)
    {
        for (const auto& change : command) {
            J* field = &value;
            for (const auto& key : change.path) { field = &(*field)[key]; }
            *field = forward ? change.after : change.before;
        }
    }
    Command pending_;
    std::vector<Command> undo_, redo_;
};
}
