#pragma once
#include "AnimatorControllerAuthoringIds.h"
#include <array>
namespace Engine::Editor::ControllerDetails {
class StableSelection final {
    uint64_t id_=0;size_t index_=0;
public:
    size_t Resolve(const std::vector<uint64_t>& ids) noexcept {
        const auto found=std::find(ids.begin(),ids.end(),id_);
        index_=found==ids.end()?0:static_cast<size_t>(found-ids.begin());
        id_=ids.empty()?0:ids[index_];return index_;
    }
    void Pick(const std::vector<uint64_t>& ids,size_t index){const auto id=ids.at(index);id_=id;index_=index;}
    uint64_t Identity() const noexcept{return id_;}
};
// Bounded per-layer presentation retention, including removed layers for Undo.
// Eviction falls back to the first entry, never an old compacted index.
class TransitionView final {
    struct Entry {uint64_t layer=0;StableSelection selection;};
    std::array<Entry,64> entries_{};size_t evict_=0;
public:
    StableSelection& For(const AnimatorControllerAuthoringIds::Layer& layer) noexcept {
        for(auto& entry:entries_)if(entry.layer==layer.id){entry.selection.Resolve(layer.transitions);return entry.selection;}
        for(auto& entry:entries_)if(!entry.layer){entry.layer=layer.id;entry.selection.Resolve(layer.transitions);return entry.selection;}
        auto& entry=entries_[evict_++%entries_.size()];entry={layer.id,{}};entry.selection.Resolve(layer.transitions);return entry.selection;
    }
};
}
