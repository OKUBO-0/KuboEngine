#pragma once
#include "PrefabDocument.h"
#include "EntityHistory.h"
#include <optional>

namespace Engine::Editor {
// Candidate-only allocation, link persistence and one-command selection/history.
// The caller owns project paths, source freshness and Editor mutation guards.
inline bool InstantiatePrefab(Scene::EntityWorld& world,const PrefabValue& definition,
    const std::string& sourceGuid,Scene::EntityId parent,EntitySelection& selection,
    EntityHistory& history,Scene::EntityId& createdRoot,std::string& error,
    std::optional<Vector3> placement = std::nullopt)
{
    using Id=Scene::EntityId;
    PrefabValue normalized;if(!ValidatePrefabDocument(definition,normalized,error))return false;
    if(!Scene::ModelComponent::IsValidGuid(sourceGuid)){error="Invalid Prefab source GUID";return false;}
    const auto before=world.Capture();Scene::EntityWorld checked;
    if(!checked.Restore(before,error))return false;
    if(parent && !checked.Find(parent)){error="Prefab destination parent does not exist";return false;}
    EntitySelection checkedSelection;if(!checkedSelection.Restore(checked,selection.Capture(),error))return false;
    Id maximum=world.AllocationHighWatermark()-1;
    for(const auto& entity:checked.GetEntities()) {
        maximum=(std::max)(maximum,entity.id);
        if(!entity.prefabLink.is_null())for(const auto& pair:entity.prefabLink["mapping"].get<crude_json::array>())maximum=(std::max)(maximum,Id(pair["entity"].get<crude_json::number>()));
    }
    const auto& nodes=normalized["world"]["entities"].get<crude_json::array>();
    constexpr Id maxExact=Scene::EntityWorld::MaximumEntityId;
    if(maximum>maxExact || nodes.size()>maxExact-maximum){error="Prefab Scene identities exhausted";return false;}
    std::unordered_map<Id,Id> mapping;Id next=maximum+1;
    for(const auto& node:nodes)mapping.emplace(Id(node["id"].get<crude_json::number>()),next++);
    const auto localRoot=Id(normalized["root"].get<crude_json::number>()),root=mapping.at(localRoot);
    PrefabValue link;link["sourceGuid"]=sourceGuid;link["rootLocal"]=double(localRoot);link["base"]=normalized["world"];link["mapping"]=crude_json::array{};
    for(const auto& node:nodes){PrefabValue pair;pair["local"]=node["id"];pair["entity"]=double(mapping.at(Id(node["id"].get<crude_json::number>())));link["mapping"].push_back(std::move(pair));}
    auto document=before;
    for(const auto& node:nodes) {
        auto copy=node;const auto local=Id(node["id"].get<crude_json::number>()),localParent=Id(node["parent"].get<crude_json::number>());
        copy["id"]=double(mapping.at(local));copy["parent"]=double(local==localRoot?parent:mapping.at(localParent));
        if(local==localRoot) {
            copy["prefabLink"]=link;
            if (placement) copy["transform"]["position"]=crude_json::array{
                double(placement->x),double(placement->y),double(placement->z)};
        }
        document["entities"].push_back(std::move(copy));
    }
    if(document.dump().size()>Scene::SceneDocument::MaxBytes){error="Prefab instance exceeds Scene document budget";return false;}
    Scene::RetagEntityDocument(document);Scene::EntityWorld candidate;if(!candidate.Restore(document,error))return false;
    EntitySelection nextSelection;nextSelection.Replace(root);
    history.Commit();
    if(!history.Record(before,candidate,selection.Capture(),nextSelection.Capture(),error))return false;
    history.Commit();world=std::move(candidate);selection=std::move(nextSelection);createdRoot=root;error.clear();return true;
}
inline bool UnlinkPrefab(Scene::EntityWorld& world,Scene::EntityId root,
    const EntitySelection& selection,EntityHistory& history,std::string& error)
{
    const auto* entity=world.Find(root);
    if(!entity || entity->prefabLink.is_null()){error="Entity is not a linked Prefab root";return false;}
    const auto before=world.Capture();auto candidate=world;candidate.Find(root)->prefabLink=Scene::SceneValue{};
    Scene::EntityWorld checked;if(!checked.Restore(candidate.Capture(),error))return false;
    history.Commit();if(!history.Record(before,candidate,selection.Capture(),selection.Capture(),error))return false;
    history.Commit();world=std::move(candidate);error.clear();return true;
}
}
