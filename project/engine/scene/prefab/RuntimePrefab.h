#pragma once
#include "PrefabDocument.h"
#include <stdexcept>

namespace Engine::Prefab {
// Baked runtime instantiation. Project/asset admission belongs to the caller;
// this pure World operation never reads files or creates authoring links.
// Failure preserves both live storage/high-water and createdRoot. Success
// invalidates borrowed records, like EntityWorld::Swap and runtime Clone.
inline bool InstantiateRuntimePrefab(Scene::EntityWorld& world,const PrefabValue& definition,
    Scene::EntityId parent,Scene::EntityId& createdRoot,std::string& error,size_t maximumCreated=64) {
    try{
        const auto reject=[&](const char* why){error=why;return false;};
        if(!maximumCreated || maximumCreated>10000 || world.GetEntities().size()>10000 || parent>Scene::EntityWorld::MaximumEntityId)return reject("Invalid runtime Prefab limits/parent");
        if(parent && !world.Find(parent))return reject("Runtime Prefab destination parent is missing");
        const auto current=world.Capture();Scene::EntityWorld checkedWorld;
        if(current.dump().size()>Scene::SceneDocument::MaxBytes)return reject("Runtime Prefab destination World exceeds snapshot budget");
        if(!checkedWorld.Restore(current,error))return false;
        PrefabValue normalized;if(!ValidatePrefabDocument(definition,normalized,error))return false;
        Scene::EntityWorld source;if(!source.Restore(normalized["world"],error))return false;
        const auto count=source.GetEntities().size();
        if(count>maximumCreated || count>10000-world.GetEntities().size())return reject("Runtime Prefab creation budget exceeded");
        const auto root=static_cast<Scene::EntityId>(normalized["root"].get<crude_json::number>());
        std::vector<Scene::EntityId> order{root};order.reserve(count);
        for(size_t i=0;i<order.size();++i)for(const auto& entity:source.GetEntities())if(entity.parent==order[i]){
            if(order.size()==count)return reject("Invalid runtime Prefab subtree");order.push_back(entity.id);
        }
        if(order.size()!=count)return reject("Disconnected runtime Prefab subtree");
        auto candidate=world;std::unordered_map<Scene::EntityId,Scene::EntityId> remapped;remapped.reserve(count);Scene::EntityId result=0;
        for(const auto id:order){
            auto copy=*source.Find(id);const auto destinationParent=id==root?parent:remapped.at(copy.parent);
            const auto created=candidate.CreateEntity(copy.name,destinationParent);
            if(!created)return reject("Runtime Prefab identity allocation refused");
            copy.id=created;copy.parent=destinationParent;copy.prefabLink=PrefabValue{};
            copy.script.started=false;copy.rigidBody.grounded=false;
            *candidate.Find(created)=std::move(copy);remapped.emplace(id,created);if(id==root)result=created;
        }
        if(candidate.Capture().dump().size()>Scene::SceneDocument::MaxBytes)return reject("Runtime Prefab result exceeds snapshot budget");
        // No owning record copies or allocations after live publication.
        world.Swap(candidate);createdRoot=result;error.clear();return true;
    }catch(const std::exception& exception){error=exception.what();return false;}
}
}
