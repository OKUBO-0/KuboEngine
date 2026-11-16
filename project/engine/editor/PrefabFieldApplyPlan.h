#pragma once
#include "PrefabFieldRevert.h"
#include "PrefabApplyPlan.h"

namespace Engine::Editor {
// Object parents may be absent in an older source/baseline. Create only the
// selected path; subsequent schema validation rejects incomplete Components.
inline bool SetPrefabApplyField(PrefabValue& node,const std::vector<std::string>& tokens,const PrefabValue* value,std::string& error) {
    if(tokens.empty()){error="Field Apply cannot replace an Entity";return false;}
    auto* target=&node;
    for(size_t i=0;i+1<tokens.size();++i) {
        if(!target->is_object()){error="Field Apply cannot descend into an atomic value";return false;}
        if(!target->contains(tokens[i])) {
            if(!value)return true;
            (*target)[tokens[i]]=crude_json::object{};
        }
        target=&(*target)[tokens[i]];
    }
    if(!target->is_object()){error="Field Apply requires an object parent";return false;}
    if(value)(*target)[tokens.back()]=*value;else target->erase(tokens.back());
    return true;
}
inline PrefabValue* PrefabApplyFieldNode(PrefabValue& document,Scene::EntityId local) {
    for(auto& node:document["world"]["entities"].get<crude_json::array>())if(node["id"].get<crude_json::number>()==double(local))return &node;
    return nullptr;
}
// Field-only adoption: other source fields/Entities are not refreshed as a side
// effect. Deleted instance targets stay deleted; mappings/additions stay local.
inline bool BuildPrefabFieldInstanceCandidate(const Scene::EntityWorld& world,Scene::EntityId root,
    const PrefabValue& source,const std::string& guid,const PrefabOverrideEntry& selected,bool elected,
    Scene::EntityWorld& output,PrefabMergeReport& report,std::string& error) {
    static_assert(std::is_nothrow_move_assignable_v<Scene::EntityWorld> && std::is_nothrow_move_assignable_v<PrefabMergeReport>);
    PrefabInstanceCapture captured;if(!CapturePrefabInstance(world,root,captured,error,&source))return false;
    if(world.Find(root)->prefabLink["sourceGuid"].get<crude_json::string>()!=guid){error="Field adoption source GUID mismatch";return false;}
    PrefabValue normalizedSource;if(!ValidatePrefabDocument(source,normalizedSource,error))return false;
    const auto* next=PrefabApplyFieldNode(normalizedSource,selected.local);
    auto* base=PrefabApplyFieldNode(captured.baseline,selected.local);auto* mine=PrefabApplyFieldNode(captured.instance,selected.local);
    if(!base || !next || (elected && !mine)){error="Field adoption requires a known mapped source Entity";return false;}
    std::vector<std::string> tokens;if(!PrefabOverrideTokens(selected.path,tokens,error))return false;
    const auto* value=PrefabOverrideValue(*next,tokens);const auto* old=PrefabOverrideValue(*base,tokens);
    const auto* authored=mine?PrefabOverrideValue(*mine,tokens):nullptr;
    PrefabMergeReport decisions;const auto path="entities/"+std::to_string(selected.local)+selected.path;
    if(!mine)decisions.overrides.push_back(path+"/(deleted)");
    else if(!elected && (selected.path=="/name" || !PrefabEqual(authored,old)))decisions.overrides.push_back(path);
    else {
        if(!SetPrefabApplyField(*mine,tokens,value,error))return false;
        decisions.inherited.push_back(path);
    }
    if(!SetPrefabApplyField(*base,tokens,value,error))return false;
    PrefabValue newBase,newMine;
    Scene::RetagEntityDocument(captured.baseline["world"]);Scene::RetagEntityDocument(captured.instance["world"]);
    if(!ValidatePrefabDocument(captured.baseline,newBase,error) || !ValidatePrefabDocument(captured.instance,newMine,error))return false;
    auto link=world.Find(root)->prefabLink;link["base"]=newBase["world"];
    auto document=world.Capture();
    if(mine) {
        const auto* node=PrefabApplyFieldNode(newMine,selected.local);auto copy=*node;
        const auto scene=captured.sceneByLocal.at(selected.local),parent=Scene::EntityId(copy["parent"].get<crude_json::number>());
        copy["id"]=double(scene);copy["parent"]=scene==root?double(world.Find(root)->parent):double(captured.sceneByLocal.at(parent));
        for(auto& current:document["entities"].get<crude_json::array>())if(current["id"].get<crude_json::number>()==double(scene))current=copy;
    }
    for(auto& node:document["entities"].get<crude_json::array>())if(node["id"].get<crude_json::number>()==double(root))node["prefabLink"]=link;
    if(document.dump().size()>Scene::SceneDocument::MaxBytes){error="Field adoption exceeds Scene budget";return false;}
    Scene::RetagEntityDocument(document);Scene::EntityWorld candidate;if(!candidate.Restore(document,error))return false;
    candidate.RetainAllocationHighWatermark(world);output=std::move(candidate);report=std::move(decisions);error.clear();return true;
}
// Read-only plan. Native source publication, revision ownership and history
// admission are caller responsibilities, not provided by this candidate core.
inline bool BuildPrefabFieldApplyPlan(const Scene::EntityWorld& world,Scene::EntityId chosenRoot,
    const PrefabValue& currentSource,const std::string& guid,const PrefabOverrideEntry& selected,
    std::unique_ptr<PrefabApplyPlan>& output,std::string& error) {
    try {
    if(selected.kind!=PrefabOverrideKind::Field && selected.kind!=PrefabOverrideKind::InstanceName){error="Structural override requires structural Apply";return false;}
    PrefabOverrideReport fresh;if(!InspectPrefabOverrides(world,chosenRoot,fresh,error))return false;
    if(fresh.sourceGuid!=guid){error="Field Apply source GUID mismatch";return false;}
    if(std::none_of(fresh.entries.begin(),fresh.entries.end(),[&](const auto& entry){return SamePrefabOverride(entry,selected);})){error="Field Apply snapshot is stale or invented";return false;}
    PrefabApplyPlan candidate;if(!ValidatePrefabDocument(currentSource,candidate.source,error))return false;
    PrefabInstanceCapture elected;if(!CapturePrefabInstance(world,chosenRoot,elected,error,&candidate.source))return false;
    auto* next=PrefabApplyFieldNode(candidate.source,selected.local);const auto* base=PrefabApplyFieldNode(elected.baseline,selected.local);
    const auto* mine=PrefabApplyFieldNode(elected.instance,selected.local);
    if(!next || !base || !mine){error="Field Apply requires a mapped live Entity in baseline/instance/current source";return false;}
    std::vector<std::string> tokens;if(!PrefabOverrideTokens(selected.path,tokens,error))return false;
    if(!SetPrefabApplyField(*next,tokens,PrefabOverrideValue(*mine,tokens),error))return false;
    Scene::RetagEntityDocument(candidate.source["world"]);PrefabValue normalized;if(!ValidatePrefabDocument(candidate.source,normalized,error))return false;
    candidate.source=std::move(normalized);PrefabMergeReport decisions;
    if(!BuildPrefabFieldInstanceCandidate(world,chosenRoot,candidate.source,guid,selected,true,candidate.world,decisions,error))return false;
    candidate.instances.push_back({chosenRoot,std::move(decisions)});
    std::set<Scene::EntityId> others;
    for(const auto& entity:world.GetEntities())if(entity.id!=chosenRoot && !entity.prefabLink.is_null() && entity.prefabLink["sourceGuid"].get<crude_json::string>()==guid)others.insert(entity.id);
    for(const auto root:others) {
        Scene::EntityWorld nextWorld;PrefabMergeReport report;
        if(!BuildPrefabFieldInstanceCandidate(candidate.world,root,candidate.source,guid,selected,false,nextWorld,report,error))return false;
        candidate.world=std::move(nextWorld);candidate.instances.push_back({root,std::move(report)});
    }
    // JSON move construction can allocate on MSVC. Finish it before swapping
    // the owned result so every preparation failure retains the old plan.
    auto ready=std::make_unique<PrefabApplyPlan>(std::move(candidate));
    output=std::move(ready);error.clear();return true;
    }catch(const std::exception& exception){error=exception.what();return false;}
}
}
