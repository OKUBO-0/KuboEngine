#pragma once
#include "PrefabDocument.h"
#include "EntityHistory.h"
#include "EntityDocumentState.h"
#include <map>
#include <set>
#include <type_traits>

namespace Engine::Editor {
// Candidate only: callers own source GUID/revision resolution and mutation locks.
// Revert resets names/TRS/components (including root TRS), keeps external parent
// and known Scene identities, restores deleted source children, removes extras.
inline bool BuildPrefabRevertCandidate(const Scene::EntityWorld& world,Scene::EntityId root,
    const PrefabValue& source,const std::string& sourceGuid,Scene::EntityWorld& output,std::string& error) {
    using Id=Scene::EntityId;
    PrefabValue normalized;if(!ValidatePrefabDocument(source,normalized,error))return false;
    const auto before=world.Capture();Scene::EntityWorld checked;if(!checked.Restore(before,error))return false;
    const auto* instance=checked.Find(root);
    if(!instance || instance->prefabLink.is_null()){error="Revert requires a linked Prefab root";return false;}
    const auto& oldLink=instance->prefabLink;
    if(oldLink["sourceGuid"].get<crude_json::string>()!=sourceGuid){error="Revert source GUID does not match instance";return false;}
    const auto rootLocal=Id(oldLink["rootLocal"].get<crude_json::number>());
    if(normalized["root"].get<crude_json::number>()!=double(rootLocal)){error="Revert source root local identity changed";return false;}
    std::unordered_map<Id,std::vector<Id>> children;
    for(const auto& entity:checked.GetEntities())children[entity.parent].push_back(entity.id);
    std::set<Id> removed{root};std::vector<Id> queue{root};
    for(size_t index=0;index<queue.size();++index)for(const auto child:children[queue[index]])if(removed.insert(child).second) {
        if(queue.size()>=10000){error="Revert subtree exceeds 10000 Entities";return false;}
        queue.push_back(child);
    }
    std::map<Id,Id> oldMap,mapping;
    for(const auto& pair:oldLink["mapping"].get<crude_json::array>())oldMap.emplace(Id(pair["local"].get<crude_json::number>()),Id(pair["entity"].get<crude_json::number>()));
    Id maximum=(std::max)(world.AllocationHighWatermark(),checked.AllocationHighWatermark())-1;
    const auto& nodes=normalized["world"]["entities"].get<crude_json::array>();
    // Allocate source additions in local-ID order, independent of source storage.
    std::set<Id> sourceIds;for(const auto& node:nodes)sourceIds.insert(Id(node["id"].get<crude_json::number>()));
    for(const auto local:sourceIds) {
        if(const auto found=oldMap.find(local);found!=oldMap.end())mapping.emplace(local,found->second);
        else {
            if(maximum>=Scene::EntityWorld::MaximumEntityId){error="Revert Scene identities exhausted";return false;}
            mapping.emplace(local,++maximum);
        }
    }
    PrefabValue link;link["sourceGuid"]=sourceGuid;link["rootLocal"]=double(rootLocal);link["base"]=normalized["world"];link["mapping"]=crude_json::array{};
    for(const auto& node:nodes){const auto local=Id(node["id"].get<crude_json::number>());PrefabValue pair;pair["local"]=double(local);pair["entity"]=double(mapping.at(local));link["mapping"].push_back(std::move(pair));}
    auto document=before;
    // Keep original Scene storage positions for surviving identities. This makes
    // an unchanged source/instance a true no-op and retains Hierarchy range order.
    std::map<Id,PrefabValue> replacement;
    for(const auto& node:nodes) {
        auto copy=node;const auto local=Id(node["id"].get<crude_json::number>()),parent=Id(node["parent"].get<crude_json::number>());
        copy["id"]=double(mapping.at(local));copy["parent"]=local==rootLocal?double(instance->parent):double(mapping.at(parent));
        if(local==rootLocal)copy["prefabLink"]=link;
        replacement.emplace(mapping.at(local),std::move(copy));
    }
    document["entities"]=crude_json::array{};
    for(const auto& node:before["entities"].get<crude_json::array>()) {
        const auto id=Id(node["id"].get<crude_json::number>());
        if(const auto found=replacement.find(id);found!=replacement.end()){document["entities"].push_back(std::move(found->second));replacement.erase(found);}
        else if(!removed.contains(id))document["entities"].push_back(node);
    }
    for(auto& [unused,node]:replacement)document["entities"].push_back(std::move(node));
    if(document.dump().size()>Scene::SceneDocument::MaxBytes){error="Reverted instance exceeds Scene document budget";return false;}
    Scene::RetagEntityDocument(document);Scene::EntityWorld candidate;if(!candidate.Restore(document,error))return false;
    candidate.RetainAllocationHighWatermark(world);candidate.RetainAllocationHighWatermark(checked);
    output=std::move(candidate);error.clear();return true;
}
inline bool RevertPrefabInstance(Scene::EntityWorld& world,Scene::EntityId root,const PrefabValue& source,
    const std::string& sourceGuid,EntitySelection& selection,EntityHistory& history,std::string& error,
    EntityDocumentState* document=nullptr) {
    static_assert(std::is_nothrow_move_assignable_v<Scene::EntityWorld>);
    static_assert(std::is_nothrow_move_assignable_v<EntitySelection>);
    try {
        if(document && document->HasPendingAction()){error="Prefab Revert document action is pending";return false;}
        const auto selected=selection.Capture();EntitySelection checkedSelection;
        if(!checkedSelection.Restore(world,selected,error))return false;
        const auto before=world.Capture();Scene::EntityWorld candidate;
        if(!BuildPrefabRevertCandidate(world,root,source,sourceGuid,candidate,error))return false;
        auto nextSelection=selection;auto primary=nextSelection.Primary();nextSelection.Synchronize(candidate,primary);
        auto nextHistory=history;nextHistory.Commit();
        if(!nextHistory.Record(before,candidate,selected,nextSelection.Capture(),error))return false;
        nextHistory.Commit();const bool dirty=document && document->WouldBeDirty(candidate);
        // Caller retains source ownership through this allocation-free publication.
        world=std::move(candidate);selection=std::move(nextSelection);history.Swap(nextHistory);
        if(document)document->ObservePrepared(dirty);
        error.clear();return true;
    }catch(const std::exception& exception){error=exception.what();return false;}
}
}
