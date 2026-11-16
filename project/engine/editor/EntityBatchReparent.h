#pragma once
#include "EntityHistory.h"
#include <unordered_map>

namespace Engine::Editor {
// Move the selected forest roots, not every selected descendant independently.
inline bool ReparentSelectedSubtrees(Scene::EntityWorld& world,const EntitySelection& selection,
    Scene::EntityId destination,bool keepWorld,EntityHistory& history,std::string& error,size_t maximumRoots=10000) {
    using Id=Scene::EntityId;
    const auto reject=[&](const char* why){error=why;return false;};
    const auto selected=selection.Capture();
    if(selected.ids.empty() || selected.ids.size()>10000 || !maximumRoots || maximumRoots>10000)
        return reject("Reparent selection/root budget must contain 1..10000 Entities");
    const auto before=world.Capture();Scene::EntityWorld checked;EntitySelection checkedSelection;
    if(!checked.Restore(before,error) || !checkedSelection.Restore(checked,selected,error))return false;
    std::unordered_map<Id,const Scene::EntityRecord*> records;
    for(const auto& entity:world.GetEntities())records.emplace(entity.id,&entity);
    if(destination && !records.contains(destination))return reject("Reparent destination does not exist");
    std::unordered_set<Id> selectedIds(selected.ids.begin(),selected.ids.end()),roots;
    for(const auto id:selected.ids) {
        bool covered=false;size_t depth=0;
        for(auto parent=records.at(id)->parent;parent;) {
            if(++depth>records.size() || !records.contains(parent))return reject("Reparent ancestry is missing or cyclic");
            if(selectedIds.contains(parent))covered=true;
            parent=records.at(parent)->parent;
        }
        if(!covered)roots.insert(id);
    }
    if(roots.size()>maximumRoots)return reject("Reparent forest exceeds aggregate root budget");
    size_t destinationDepth=0;
    for(auto parent=destination;parent;) {
        if(roots.contains(parent))return reject("Reparent destination is inside the selected forest");
        if(++destinationDepth>records.size() || !records.contains(parent))return reject("Reparent destination ancestry is missing or cyclic");
        parent=records.at(parent)->parent;
    }
    auto candidate=world;
    // Stable storage order is independent of selection click order.
    for(const auto& entity:world.GetEntities())if(roots.contains(entity.id))
        if(!candidate.SetParent(entity.id,destination,keepWorld,error))return false;
    if(before.dump()==candidate.Capture().dump()){error.clear();return true;}
    history.Commit();
    if(!history.Record(before,candidate,selected,selected,error))return false;
    history.Commit();world=std::move(candidate);error.clear();return true;
}
}
