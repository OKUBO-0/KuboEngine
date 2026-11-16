#pragma once
#include "PrefabDocument.h"
#include <map>
#include <set>

namespace Engine::Editor {
struct PrefabInstanceCapture {
    PrefabValue baseline,instance;
    // Includes baseline tombstones as well as candidate identities for additions.
    // This is not published into the existing Scene link by capture alone.
    std::map<Scene::EntityId,Scene::EntityId> sceneByLocal;
};
// Capture a linked instance in source-local identity space for Apply/Revert/merge.
// Incoming source IDs, when provided, reserve identities before assigning local
// IDs to instance-added nodes. No World/selection/history/filesystem mutation.
inline bool CapturePrefabInstance(const Scene::EntityWorld& world,Scene::EntityId root,
    PrefabInstanceCapture& output,std::string& error,const PrefabValue* incomingSource=nullptr) {
    using Id=Scene::EntityId;
    const auto snapshot=world.Capture();Scene::EntityWorld checked;
    if(!checked.Restore(snapshot,error))return false;
    const auto* record=checked.Find(root);
    if(!record || record->prefabLink.is_null()){error="Capture requires a linked Prefab root";return false;}
    const auto& link=record->prefabLink;const auto rootLocal=Id(link["rootLocal"].get<crude_json::number>());
    PrefabInstanceCapture candidate;
    candidate.baseline["format"]="kubo-prefab-1";candidate.baseline["root"]=double(rootLocal);candidate.baseline["world"]=link["base"];
    PrefabValue normalized;if(!ValidatePrefabDocument(candidate.baseline,normalized,error))return false;
    candidate.baseline=std::move(normalized);
    Id maximum=0;std::map<Id,Id> localByScene;
    for(const auto& pair:link["mapping"].get<crude_json::array>()) {
        const auto local=Id(pair["local"].get<crude_json::number>()),scene=Id(pair["entity"].get<crude_json::number>());
        candidate.sceneByLocal.emplace(local,scene);localByScene.emplace(scene,local);maximum=(std::max)(maximum,local);
    }
    if(incomingSource) {
        PrefabValue incoming;if(!ValidatePrefabDocument(*incomingSource,incoming,error))return false;
        if(incoming["root"].get<crude_json::number>()!=double(rootLocal)){error="Incoming Prefab root local identity changed";return false;}
        for(const auto& node:incoming["world"]["entities"].get<crude_json::array>())maximum=(std::max)(maximum,Id(node["id"].get<crude_json::number>()));
    }
    std::unordered_map<Id,std::vector<Id>> children;
    for(const auto& entity:checked.GetEntities())children[entity.parent].push_back(entity.id);
    std::set<Id> members{root};std::vector<Id> queue{root};
    for(size_t index=0;index<queue.size();++index)for(const auto child:children[queue[index]])if(members.insert(child).second) {
        if(queue.size()>=10000){error="Prefab instance capture exceeds 10000 Entities";return false;}
        queue.push_back(child);
    }
    // Scene-ID order makes assignments independent of storage/Hierarchy order.
    for(const auto scene:members)if(!localByScene.contains(scene)) {
        if(maximum>=Scene::EntityWorld::MaximumEntityId){error="Prefab addition local identities exhausted";return false;}
        const auto local=++maximum;candidate.sceneByLocal.emplace(local,scene);localByScene.emplace(scene,local);
    }
    std::map<Id,PrefabValue> localNodes;
    for(const auto& value:snapshot["entities"].get<crude_json::array>()) {
        const auto scene=Id(value["id"].get<crude_json::number>());if(!members.contains(scene))continue;
        auto node=value;node.erase("prefabLink");node["id"]=double(localByScene.at(scene));
        const auto parent=Id(value["parent"].get<crude_json::number>());
        node["parent"]=scene==root?0.0:double(localByScene.at(parent));
        localNodes.emplace(localByScene.at(scene),std::move(node));
    }
    candidate.instance["format"]="kubo-prefab-1";candidate.instance["root"]=double(rootLocal);
    candidate.instance["world"]["version"]=1.0;candidate.instance["world"]["entities"]=crude_json::array{};
    for(auto& [unused,node]:localNodes)candidate.instance["world"]["entities"].push_back(std::move(node));
    Scene::RetagEntityDocument(candidate.instance["world"]);if(!ValidatePrefabDocument(candidate.instance,normalized,error))return false;
    candidate.instance=std::move(normalized);output=std::move(candidate);error.clear();return true;
}
}
