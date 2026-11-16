#pragma once
#include "EntitySelection.h"
#include "EntityHistory.h"
#include <unordered_map>

namespace Engine::Editor {
// Authoring command: selected descendants are copied by their selected ancestor once.
inline bool DuplicateSelectedSubtrees(Scene::EntityWorld& world,EntitySelection& selection,
    EntityHistory& history,std::string& error,size_t maximumClones=10000) {
    using Id=Scene::EntityId;
    const auto reject=[&](const char* why){error=why;return false;};
    const auto& selected=selection.Ids();const auto oldPrimary=selection.Primary();
    if(selected.empty() || selected.size()>10000 || !maximumClones || maximumClones>10000)
        return reject("Duplicate selection/clone budget must contain 1..10000 Entities");
    const auto before=world.Capture();Scene::EntityWorld checked;
    if(!checked.Restore(before,error))return false;
    std::unordered_map<Id,const Scene::EntityRecord*> records;
    std::unordered_map<Id,std::vector<Id>> children;
    for(const auto& entity:world.GetEntities()){records.emplace(entity.id,&entity);children[entity.parent].push_back(entity.id);}
    std::unordered_set<Id> selectedIds;
    for(const auto id:selected)if(!records.contains(id) || !selectedIds.insert(id).second)return reject("Duplicate selection is missing or repeated");
    if(!selectedIds.contains(oldPrimary))return reject("Duplicate primary is not selected");
    std::unordered_set<Id> roots;
    for(const auto id:selected) {
        bool covered=false;size_t depth=0;
        for(auto parent=records.at(id)->parent;parent;) {
            if(++depth>records.size() || !records.contains(parent))return reject("Duplicate ancestry is missing or cyclic");
            if(selectedIds.contains(parent))covered=true;
            parent=records.at(parent)->parent;
        }
        if(!covered)roots.insert(id);
    }
    struct Copy {Id root,parent;std::vector<Id> order;};std::vector<Copy> copies;size_t total=0;
    // Storage order makes clone IDs independent of selection click order.
    for(const auto& entity:world.GetEntities())if(roots.contains(entity.id)) {
        Copy copy{entity.id,entity.parent,{entity.id}};
        for(size_t index=0;index<copy.order.size();++index) {
            const auto found=children.find(copy.order[index]);if(found==children.end())continue;
            for(const auto child:found->second) {
                if(copy.order.size()>=maximumClones-total)return reject("Duplicate forest exceeds aggregate clone budget");
                copy.order.push_back(child);
            }
        }
        if(copy.order.size()>maximumClones-total)return reject("Duplicate forest exceeds aggregate clone budget");
        total+=copy.order.size();copies.push_back(std::move(copy));
    }
    auto candidate=world;std::unordered_map<Id,Id> remapped;
    for(const auto& copy:copies) {
        const auto start=candidate.GetEntities().size();Id created=0;
        if(!candidate.DuplicateSubtree(copy.root,copy.parent,created,error,copy.order.size()))return false;
        if(candidate.GetEntities().size()-start!=copy.order.size())return reject("Duplicate subtree clone count mismatch");
        // DuplicateSubtree appends its validated breadth-first source order.
        for(size_t index=0;index<copy.order.size();++index)remapped.emplace(copy.order[index],candidate.GetEntities()[start+index].id);
        if(remapped.at(copy.root)!=created)return reject("Duplicate subtree root mapping mismatch");
        for(const auto id:copy.order) {
            const auto* clone=candidate.Find(remapped.at(id));
            const auto parent=id==copy.root?copy.parent:remapped.at(records.at(id)->parent);
            if(!clone || clone->parent!=parent)return reject("Duplicate subtree parent mapping mismatch");
        }
    }
    std::vector<Id> newIds;newIds.reserve(selected.size());for(const auto id:selected)newIds.push_back(remapped.at(id));
    EntitySelection nextSelection;
    if(!nextSelection.Assign(candidate,newIds,remapped.at(oldPrimary),error))return false;
    history.Commit();
    if(!history.Record(before,candidate,selection.Capture(),nextSelection.Capture(),error))return false;
    history.Commit();world=std::move(candidate);selection=std::move(nextSelection);error.clear();return true;
}
}
