#pragma once
#include "PrefabInstanceCapture.h"
#include "PrefabMerge.h"

namespace Engine::Editor {
struct PrefabInstanceUpdateReport {
    Scene::EntityId root=0;
    PrefabMergeReport merge;
};
struct PrefabApplyPlan {
    PrefabValue source;
    Scene::EntityWorld world;
    std::vector<PrefabInstanceUpdateReport> instances;
};
// Publish a previously frozen local-ID capture into a candidate only. The source
// baseline and visible merged instance are deliberately separate: deleted source
// children retain reserved mappings; retained source-deleted edits become extras.
inline bool BuildPrefabMergedCandidate(const Scene::EntityWorld& world,Scene::EntityId root,
    const std::string& guid,const PrefabInstanceCapture& capture,const PrefabValue& sourceInput,
    const PrefabValue& mergedInput,Scene::EntityWorld& output,std::string& error) {
    using Id=Scene::EntityId;
    PrefabValue source,merged;
    if(!ValidatePrefabDocument(sourceInput,source,error) || !ValidatePrefabDocument(mergedInput,merged,error))return false;
    Scene::EntityWorld checked;if(!checked.Restore(world.Capture(),error))return false;
    const auto* instance=checked.Find(root);
    if(!instance || instance->prefabLink.is_null() || instance->prefabLink["sourceGuid"].get<crude_json::string>()!=guid) {
        error="Merged candidate requires the matching linked root";return false;
    }
    const auto rootLocal=Id(instance->prefabLink["rootLocal"].get<crude_json::number>());
    if(source["root"].get<crude_json::number>()!=double(rootLocal) || merged["root"].get<crude_json::number>()!=double(rootLocal)) {
        error="Merged candidate root local identity changed";return false;
    }
    // Reject a stale or invented capture before trusting its Scene correspondences.
    PrefabValue currentBaseline;currentBaseline["format"]="kubo-prefab-1";currentBaseline["root"]=double(rootLocal);currentBaseline["world"]=instance->prefabLink["base"];
    PrefabValue validatedBaseline;if(!ValidatePrefabDocument(currentBaseline,validatedBaseline,error))return false;
    // Fresh capture can assign DIFFERENT local IDs to additions after source grows.
    // Validate the frozen correspondence using current Scene IDs and its own local
    // document, rather than replacing it with freshly assigned local identities.
    if(capture.baseline.dump()!=validatedBaseline.dump()) {error="Frozen Prefab baseline changed";return false;}
    std::set<Id> subtree;std::unordered_map<Id,std::vector<Id>> children;
    for(const auto& entity:checked.GetEntities())children[entity.parent].push_back(entity.id);
    std::vector<Id> queue{root};subtree.insert(root);
    for(size_t i=0;i<queue.size();++i)for(const auto child:children[queue[i]])if(subtree.insert(child).second) {
        if(queue.size()>=10000){error="Merged Prefab subtree exceeds 10000 Entities";return false;}queue.push_back(child);
    }
    PrefabValue frozen; if(!ValidatePrefabDocument(capture.instance,frozen,error))return false;
    if(frozen["root"].get<crude_json::number>()!=double(rootLocal)){error="Frozen capture root changed";return false;}
    if(capture.sceneByLocal.size()>20000){error="Frozen correspondence exceeds identity budget";return false;}
    std::map<Id,Id> localByScene;
    for(const auto& [local,scene]:capture.sceneByLocal) {
        if(!local || local>Scene::EntityWorld::MaximumEntityId || !scene || scene>Scene::EntityWorld::MaximumEntityId || !localByScene.emplace(scene,local).second) {
            error="Frozen capture contains invalid or duplicate identities";return false;
        }
    }
    if(!capture.sceneByLocal.contains(rootLocal) || capture.sceneByLocal.at(rootLocal)!=root){error="Frozen capture root Scene identity changed";return false;}
    for(const auto& pair:instance->prefabLink["mapping"].get<crude_json::array>()) {
        const auto local=Id(pair["local"].get<crude_json::number>()),scene=Id(pair["entity"].get<crude_json::number>());
        if(!capture.sceneByLocal.contains(local) || capture.sceneByLocal.at(local)!=scene){error="Frozen baseline mapping changed";return false;}
    }
    std::set<Id> capturedLive;
    const auto snapshot=world.Capture();std::map<Id,PrefabValue> liveNodes;
    for(const auto& node:snapshot["entities"].get<crude_json::array>())liveNodes.emplace(Id(node["id"].get<crude_json::number>()),node);
    for(const auto& node:frozen["world"]["entities"].get<crude_json::array>()) {
        const auto local=Id(node["id"].get<crude_json::number>());
        if(!capture.sceneByLocal.contains(local)){error="Frozen node lacks Scene mapping";return false;}
        const auto scene=capture.sceneByLocal.at(local);
        if(!subtree.contains(scene) || !capturedLive.insert(scene).second){error="Frozen node is not a current subtree Entity";return false;}
        auto current=liveNodes.at(scene);current.erase("prefabLink");current["id"]=double(local);
        const auto parent=Id(current["parent"].get<crude_json::number>());
        if(scene!=root && !localByScene.contains(parent)){error="Frozen parent lacks local identity";return false;}
        current["parent"]=scene==root?0.0:double(localByScene.at(parent));
        if(current.dump()!=node.dump()){error="Frozen instance changed";return false;}
    }
    if(capturedLive!=subtree){error="Frozen instance membership changed";return false;}
    // Any non-baseline frozen mapping must belong to a live addition, never an
    // unrelated/tombstoned Entity which could otherwise be overwritten below.
    std::set<Id> baselineLocals;
    for(const auto& pair:instance->prefabLink["mapping"].get<crude_json::array>())baselineLocals.insert(Id(pair["local"].get<crude_json::number>()));
    for(const auto& [local,scene]:capture.sceneByLocal)if(!baselineLocals.contains(local) && !capturedLive.contains(scene)) {
        error="Frozen addition mapping is not live";return false;
    }
    std::set<Id> required;
    for(const auto* document:{&source,&merged})for(const auto& node:(*document)["world"]["entities"].get<crude_json::array>())required.insert(Id(node["id"].get<crude_json::number>()));
    auto mapping=capture.sceneByLocal;
    Id maximum=(std::max)(world.AllocationHighWatermark(),checked.AllocationHighWatermark())-1;
    for(const auto local:required)if(!mapping.contains(local)) {
        if(maximum>=Scene::EntityWorld::MaximumEntityId){error="Merged Prefab Scene identities exhausted";return false;}
        mapping.emplace(local,++maximum);
    }
    PrefabValue link;link["sourceGuid"]=guid;link["rootLocal"]=double(rootLocal);link["base"]=source["world"];link["mapping"]=crude_json::array{};
    for(const auto& node:source["world"]["entities"].get<crude_json::array>()) {
        const auto local=Id(node["id"].get<crude_json::number>());PrefabValue pair;pair["local"]=double(local);pair["entity"]=double(mapping.at(local));link["mapping"].push_back(std::move(pair));
    }
    std::map<Id,PrefabValue> replacements;
    for(const auto& node:merged["world"]["entities"].get<crude_json::array>()) {
        auto copy=node;const auto local=Id(node["id"].get<crude_json::number>()),parent=Id(node["parent"].get<crude_json::number>());
        copy["id"]=double(mapping.at(local));copy["parent"]=local==rootLocal?double(instance->parent):double(mapping.at(parent));
        if(local==rootLocal)copy["prefabLink"]=link;
        replacements.emplace(mapping.at(local),std::move(copy));
    }
    auto document=snapshot;document["entities"]=crude_json::array{};
    for(const auto& node:snapshot["entities"].get<crude_json::array>()) {
        const auto id=Id(node["id"].get<crude_json::number>());
        if(const auto found=replacements.find(id);found!=replacements.end()){document["entities"].push_back(std::move(found->second));replacements.erase(found);}
        else if(!subtree.contains(id))document["entities"].push_back(node);
    }
    for(auto& [unused,node]:replacements)document["entities"].push_back(std::move(node));
    if(document.dump().size()>Scene::SceneDocument::MaxBytes){error="Merged Prefab exceeds Scene budget";return false;}
    Scene::RetagEntityDocument(document);Scene::EntityWorld candidate;if(!candidate.Restore(document,error))return false;
    candidate.RetainAllocationHighWatermark(world);candidate.RetainAllocationHighWatermark(checked);
    output=std::move(candidate);error.clear();return true;
}
// Read-only all-instance Apply plan. No source file, live World, selection or
// history mutation. A caller must atomically coordinate source revision/writing
// and history admission before publishing this plan.
inline bool BuildPrefabApplyPlan(const Scene::EntityWorld& world,Scene::EntityId chosenRoot,
    const PrefabValue& currentSource,const std::string& guid,PrefabApplyPlan& output,std::string& error) {
    Scene::EntityWorld checked;if(!checked.Restore(world.Capture(),error))return false;
    const auto* chosen=checked.Find(chosenRoot);
    if(!chosen || chosen->prefabLink.is_null() || chosen->prefabLink["sourceGuid"].get<crude_json::string>()!=guid) {
        error="Apply requires the matching linked Prefab root";return false;
    }
    PrefabInstanceCapture elected;
    if(!CapturePrefabInstance(world,chosenRoot,elected,error,&currentSource))return false;
    PrefabApplyPlan candidate;PrefabMergeReport chosenReport;
    if(!MergePrefabDocuments(elected.baseline,currentSource,elected.instance,candidate.source,chosenReport,error))return false;
    // The merge core indexes/sorts local IDs. Keep surviving source storage order
    // for this publication so an unchanged non-sorted definition remains no-op.
    std::map<Scene::EntityId,PrefabValue> sourceNodes;
    for(const auto& node:candidate.source["world"]["entities"].get<crude_json::array>())sourceNodes.emplace(Scene::EntityId(node["id"].get<crude_json::number>()),node);
    candidate.source["world"]["entities"]=crude_json::array{};
    for(const auto& node:currentSource["world"]["entities"].get<crude_json::array>()) {
        const auto local=Scene::EntityId(node["id"].get<crude_json::number>());
        if(const auto found=sourceNodes.find(local);found!=sourceNodes.end()){candidate.source["world"]["entities"].push_back(std::move(found->second));sourceNodes.erase(found);}
    }
    for(auto& [unused,node]:sourceNodes)candidate.source["world"]["entities"].push_back(std::move(node));
    if(!BuildPrefabMergedCandidate(world,chosenRoot,guid,elected,candidate.source,candidate.source,candidate.world,error))return false;
    candidate.instances.push_back({chosenRoot,std::move(chosenReport)});
    std::set<Scene::EntityId> others;
    for(const auto& entity:checked.GetEntities())if(entity.id!=chosenRoot && !entity.prefabLink.is_null() && entity.prefabLink["sourceGuid"].get<crude_json::string>()==guid)others.insert(entity.id);
    for(const auto root:others) {
        PrefabInstanceCapture captured;PrefabValue merged;PrefabMergeReport report;Scene::EntityWorld next;
        if(!CapturePrefabInstance(candidate.world,root,captured,error,&candidate.source) ||
           !MergePrefabDocuments(captured.baseline,candidate.source,captured.instance,merged,report,error) ||
           !BuildPrefabMergedCandidate(candidate.world,root,guid,captured,candidate.source,merged,next,error))return false;
        candidate.world=std::move(next);candidate.instances.push_back({root,std::move(report)});
    }
    output=std::move(candidate);error.clear();return true;
}
}
