#pragma once
#include "../scene/entity/EntityWorld.h"
#include <filesystem>
#include <string>
#include <utility>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace Engine::Editor {

// Authored document state; owners must not Observe a playing/paused runtime world.
class EntityDocumentState final {
public:
    enum class Action { None, Reload, Close, Open, New };
    void Opened(const Scene::EntityWorld& world, std::filesystem::path path)
    {
        if(generation_==(std::numeric_limits<uint64_t>::max)())throw std::overflow_error("Document generation exhausted");
        auto saved=world.Capture().dump();
        path_.swap(path);saved_.swap(saved);++generation_;
        untitled_ = path_.empty();
        dirty_=untitled_;
        Cancel();
    }
    // Changes only on successful Open/New/Reload, not Save, edits or playback.
    uint64_t Generation() const noexcept {return generation_;}
    void Observe(const Scene::EntityWorld& world) { dirty_ = untitled_ || world.Capture().dump() != saved_; }
    bool WouldBeDirty(const Scene::EntityWorld& world) const { return untitled_ || world.Capture().dump()!=saved_; }
    // Owner prepares this observation before a no-throw authored publication,
    // and verifies HasSameState before applying it under the source lease.
    void ObservePrepared(bool dirty) noexcept { dirty_=untitled_ || dirty; }
    bool HasSameState(const EntityDocumentState& other) const noexcept {
        return generation_==other.generation_ && path_.native()==other.path_.native() && pendingTarget_.native()==other.pendingTarget_.native() &&
            saved_==other.saved_ && dirty_==other.dirty_ && ready_==other.ready_ && untitled_==other.untitled_ && pending_==other.pending_;
    }
    void MarkSaved(const Scene::EntityWorld& world) { saved_ = world.Capture().dump(); dirty_ = untitled_; }
    bool SavedAs(const Scene::EntityWorld& world,std::filesystem::path target)
    {
        if(target.empty() || target.native().find(L'\0')!=std::wstring::npos) return false;
        path_=std::move(target);untitled_=false;MarkSaved(world);return true;
    }
    bool IsUntitled() const { return untitled_; }
    bool MarkSavedAt(const Scene::EntityWorld& world, const std::filesystem::path& target)
    {
        if(untitled_ || path_.empty() || target.empty()) return false;
        std::error_code error;
        const auto current = std::filesystem::absolute(path_, error).lexically_normal();
        if (error) return false;
        const auto savedPath = std::filesystem::absolute(target, error).lexically_normal();
        if (error) return false;
        // equivalent handles Windows case aliases and existing symlink aliases.
        if (current != savedPath && !std::filesystem::equivalent(current, savedPath, error)) return false;
        MarkSaved(world);
        return true;
    }
    bool IsDirty() const { return dirty_; }
    const std::filesystem::path& Path() const { return path_; }
    bool HasPendingAction() const { return pending_ != Action::None; }
    bool NeedsConfirmation() const { return HasPendingAction() && !ready_; }
    const std::filesystem::path& PendingTarget() const { return pendingTarget_; }
    bool RequestOpen(std::filesystem::path target)
    {
        if(target.empty() || target.native().find(L'\0')!=std::wstring::npos || HasPendingAction()) return false;
        pendingTarget_=std::move(target);pending_=Action::Open;ready_=!dirty_;return true;
    }
    bool Request(Action action)
    {
        if (action == Action::None || action == Action::Open || HasPendingAction()) return false;
        pending_ = action;
        ready_ = !dirty_;
        return true;
    }
    void Discard() { if (HasPendingAction()) ready_ = true; }
    bool SavedAndProceed()
    {
        if (!HasPendingAction() || dirty_) return false;
        ready_ = true;
        return true;
    }
    void Cancel() { pending_ = Action::None; ready_ = false; pendingTarget_.clear(); }
    Action TakeReadyAction()
    {
        if (!HasPendingAction() || !ready_) return Action::None;
        const auto action = pending_;
        Cancel();
        return action;
    }
private:
    uint64_t generation_=0;
    std::filesystem::path path_;
    std::filesystem::path pendingTarget_;
    std::string saved_;
    bool dirty_ = false, ready_ = false, untitled_ = false;
    Action pending_ = Action::None;
};
}
