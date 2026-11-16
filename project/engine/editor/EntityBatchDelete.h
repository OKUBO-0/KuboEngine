#pragma once
#include "EntitySelection.h"
#include "EntityHistory.h"
#include <unordered_map>

namespace Engine::Editor {
// Explicit subtree deletion, unlike DestroyEntity's legacy root-children policy.
inline bool DeleteSelectedSubtrees(Scene::EntityWorld& world,EntitySelection& selection,
    EntityHistory& history,std::string& error,size_t maximumDeletes=10000) {
    using Id=Scene::EntityId;
    const auto reject=[&](const char* why){error=why;return false;};
    const auto& ids=selection.Ids();const auto primary=selection.Primary();
    if(ids.empty() || ids.size()>10000 || !maximumDeletes || maximumDeletes>10000)
        return reject("Delete selection/Entity budget must contain 1..10000 Entities");
    const auto before=world.Capture();Scene::EntityWorld checked;
    if(!checked.Restore(before,error))return false;
    std::unordered_map<Id,const Scene::EntityRecord*> records;
    std::unordered_map<Id,std::vector<Id>> children;
    for(const auto& entity:world.GetEntities()){records.emplace(entity.id,&entity);children[entity.parent].push_back(entity.id);}
    std::unordered_set<Id> removed;std::vector<Id> order;
    for(const auto id:ids) {
        if(!records.contains(id) || !removed.insert(id).second)return reject("Delete selection is missing or repeated");
        order.push_back(id);
    }
    if(!removed.contains(primary))return reject("Delete primary must belong to selection");
    if(order.size()>maximumDeletes)return reject("Delete forest exceeds aggregate Entity budget");
    for(size_t index=0;index<order.size();++index) {
        const auto found=children.find(order[index]);if(found==children.end())continue;
        for(const auto child:found->second)if(removed.insert(child).second) {
            if(order.size()>=maximumDeletes)return reject("Delete forest exceeds aggregate Entity budget");
            order.push_back(child);
        }
    }
    std::unordered_map<Id,size_t> depths;
    for(const auto id:order) {
        size_t depth=0;
        for(auto parent=records.at(id)->parent;parent;) {
            if(++depth>records.size() || !records.contains(parent))return reject("Delete ancestry is missing or cyclic");
            parent=records.at(parent)->parent;
        }
        depths.emplace(id,depth);
    }
    std::sort(order.begin(),order.end(),[&](Id a,Id b){return depths.at(a)==depths.at(b)?a<b:depths.at(a)>depths.at(b);});
    auto candidate=world;
    for(const auto id:order)if(!candidate.DestroyEntity(id))return reject("Delete candidate Entity disappeared");
    for(const auto& entity:candidate.GetEntities())if(removed.contains(entity.parent))return reject("Delete left a dangling parent");
    history.Commit();
    if(!history.Record(before,candidate,selection.Capture(),EntitySelection::Snapshot{},error))return false;
    history.Commit();world=std::move(candidate);selection.Replace(0);error.clear();return true;
}
}
