#pragma once
#include "EntitySelection.h"
#include "EntityHistoryEffect.h"
#include "EntityDocumentState.h"
#include <algorithm>
#include <deque>
#include <string>
#include <utility>
#include <memory>
#include <limits>
#include <type_traits>

namespace Engine::Editor {

// Authored-world transactions only. Runtime state must never be recorded.
// Complete snapshots support hierarchy and component creation/deletion; expected
// state checks prevent undo from silently overwriting unrecorded external edits.
class EntityHistory final {
    using World = Scene::EntityWorld;
    using Value = Scene::SceneValue;
    using Id = Scene::EntityId;
    struct Entry {
        std::string before, after;
        EntitySelection::Snapshot beforeSelection, afterSelection;
        std::shared_ptr<EntityHistoryEffect> effect;
        bool Changed() const{return !before.empty() && (before!=after || effect);}
        size_t Bytes() const {
            if(before.empty())return 0;
            const auto scene=before.size()+after.size()+beforeSelection.Bytes()+afterSelection.Bytes();
            const auto extra=effect?effect->Bytes():0;
            return extra>(std::numeric_limits<size_t>::max)()-scene?(std::numeric_limits<size_t>::max)():scene+extra;
        }
        void Swap(Entry& other) noexcept{
            before.swap(other.before);after.swap(other.after);effect.swap(other.effect);
            beforeSelection.ids.swap(other.beforeSelection.ids);afterSelection.ids.swap(other.afterSelection.ids);
            std::swap(beforeSelection.primary,other.beforeSelection.primary);std::swap(beforeSelection.anchor,other.beforeSelection.anchor);
            std::swap(afterSelection.primary,other.afterSelection.primary);std::swap(afterSelection.anchor,other.afterSelection.anchor);
        }
    };
public:
    explicit EntityHistory(size_t maxCommands = 100, size_t maxBytes = 64 * 1024 * 1024)
        : maxCommands_(std::max(size_t{1}, maxCommands)), maxBytes_(maxBytes) {}

    bool Record(const Value& before, const World& world, Id beforeSelection, Id afterSelection, std::string& error)
    {
        EntitySelection a,b;a.Replace(beforeSelection);b.Replace(afterSelection);
        return Record(before,world,a.Capture(),b.Capture(),error);
    }
    bool Record(const Value& before, const World& world,const EntitySelection::Snapshot& beforeSelection,
        const EntitySelection::Snapshot& afterSelection,std::string& error,std::shared_ptr<EntityHistoryEffect> effect={})
    {
        const auto after = world.Capture();
        auto beforeText = before.dump();
        auto afterText = after.dump();
        if (beforeText == afterText && !effect) { error.clear(); return true; }
        World checked;
        EntitySelection checkedSelection;
        if (!checked.Restore(before,error) || !checkedSelection.Restore(checked,beforeSelection,error) ||
            !checked.Restore(after,error) || !checkedSelection.Restore(checked,afterSelection,error)) return false;
        const bool merging = !effect && !pending_.effect && !pending_.before.empty() && pending_.after == beforeText && pending_.afterSelection == beforeSelection;
        const size_t bytes = (merging ? pending_.before.size()+pending_.beforeSelection.Bytes() : beforeText.size()+beforeSelection.Bytes()) + afterText.size()+afterSelection.Bytes();
        if (bytes > maxBytes_ || (effect && effect->Bytes()>maxBytes_-bytes)) { error = "Entity edit exceeds history memory budget"; return false; }
        if (!merging) {
            Commit();
            pending_ = {std::move(beforeText), std::move(afterText), beforeSelection, afterSelection,std::move(effect)};
        } else {
            pending_.after = std::move(afterText);
            pending_.afterSelection = afterSelection;
        }
        Trim();
        error.clear();
        return true;
    }

    void Commit()
    {
        if (pending_.before.empty()) return;
        if (pending_.Changed()) {
            redo_.clear();
            undo_.push_back(std::move(pending_));
            pending_ = {};
            Trim();
        }
        pending_ = {};
    }
    void Clear() { pending_ = {}; undo_.clear(); redo_.clear(); }
    bool CanUndo() const { return !undo_.empty() || pending_.Changed(); }
    bool CanRedo() const { return redo_.size() != 0 && !pending_.Changed(); }
    // Exact admission snapshot comparison, including pending/redo payload and
    // effect identity; counts/bytes alone cannot detect intervening authoring.
    bool HasSameState(const EntityHistory& other) const noexcept {
        const auto same=[](const Entry& a,const Entry& b) {
            return a.before==b.before && a.after==b.after && a.beforeSelection==b.beforeSelection &&
                a.afterSelection==b.afterSelection && a.effect==b.effect;
        };
        return maxCommands_==other.maxCommands_ && maxBytes_==other.maxBytes_ && same(pending_,other.pending_) &&
            undo_.size()==other.undo_.size() && redo_.size()==other.redo_.size() &&
            std::equal(undo_.begin(),undo_.end(),other.undo_.begin(),same) &&
            std::equal(redo_.begin(),redo_.end(),other.redo_.begin(),same);
    }
    void Swap(EntityHistory& other) noexcept{
        std::swap(maxCommands_,other.maxCommands_);std::swap(maxBytes_,other.maxBytes_);
        pending_.Swap(other.pending_);undo_.swap(other.undo_);redo_.swap(other.redo_);
    }
    size_t UndoCount() const { return undo_.size(); }
    size_t RedoCount() const { return redo_.size(); }
    size_t StoredBytes() const
    {
        size_t bytes = pending_.Bytes();
        for (const auto& entry : undo_) bytes += entry.Bytes();
        for (const auto& entry : redo_) bytes += entry.Bytes();
        return bytes;
    }
    bool Undo(World& world, Id& selection, std::string& error)
    {
        EntitySelection full;full.Replace(selection);
        if(!Undo(world,full,error))return false;
        selection=full.Primary();return true;
    }
    bool Undo(World& world, EntitySelection& selection, std::string& error,EntityDocumentState* document=nullptr)
    {
        return Replay(true,world,selection,error,document);
    }
    bool Redo(World& world, Id& selection, std::string& error)
    {
        EntitySelection full;full.Replace(selection);
        if(!Redo(world,full,error))return false;
        selection=full.Primary();return true;
    }
    bool Redo(World& world, EntitySelection& selection, std::string& error,EntityDocumentState* document=nullptr)
    {
        return Replay(false,world,selection,error,document);
    }
private:
    bool Replay(bool undo,World& world,EntitySelection& selection,std::string& error,EntityDocumentState* document){
        static_assert(std::is_nothrow_move_assignable_v<World> && std::is_nothrow_move_assignable_v<EntitySelection>);
        try {
            if(document && document->HasPendingAction()){error="History document action is pending";return false;}
            // Every allocating operation and history transfer precedes Begin.
            // Failed validation/effect must leave even pending/redo state intact.
            EntityHistory next=*this;next.Commit();
            auto& from=undo?next.undo_:next.redo_;auto& to=undo?next.redo_:next.undo_;
            if(from.empty()){error=undo?"No entity edit to undo":"No entity edit to redo";return false;}
            const auto& entry=from.back();
            if(world.Capture().dump()!=(undo?entry.after:entry.before)){error=undo?"Undo rejected: world changed outside recorded history":"Redo rejected: world changed outside recorded history";return false;}
            World candidate;EntitySelection nextSelection;
            if(!candidate.Restore(Value::parse(undo?entry.before:entry.after),error) || !nextSelection.Restore(candidate,undo?entry.beforeSelection:entry.afterSelection,error))return false;
            // Dirty serialization may allocate: prepare it before external state
            // changes, then publish the observation while source ownership is held.
            const bool nextDirty=document && document->WouldBeDirty(candidate);
            struct Lease {std::shared_ptr<EntityHistoryEffect> effect;~Lease(){if(effect)effect->End();}} lease{entry.effect};
            to.push_back(std::move(from.back()));from.pop_back();
            if(lease.effect && !lease.effect->Begin(undo,error))return false;
            world=std::move(candidate);selection=std::move(nextSelection);Swap(next);
            if(document)document->ObservePrepared(nextDirty);
            error.clear();return true;
        }catch(const std::exception& exception){error=exception.what();return false;}
    }
    void Trim()
    {
        while (!undo_.empty() && undo_.size() > maxCommands_) undo_.pop_front();
        while (StoredBytes() > maxBytes_) {
            if (!undo_.empty()) undo_.pop_front();
            else if (!redo_.empty()) redo_.pop_front();
            else break; // A pending command was size-checked before admission.
        }
    }
    size_t maxCommands_, maxBytes_;
    Entry pending_;
    std::deque<Entry> undo_, redo_;
};
}
