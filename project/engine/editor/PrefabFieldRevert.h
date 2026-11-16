#pragma once
#include "PrefabOverrides.h"
#include "EntityHistory.h"

namespace Engine::Editor {
inline bool SamePrefabOverride(const PrefabOverrideEntry& a,const PrefabOverrideEntry& b) {
    return a.kind==b.kind && a.local==b.local && a.scene==b.scene && a.name==b.name &&
        a.path==b.path && a.baseline==b.baseline && a.instance==b.instance;
}
inline bool PrefabOverrideTokens(const std::string& path,std::vector<std::string>& output,std::string& error) {
    if(path.empty() || path.front()!='/'){error="Field Revert requires an object field path";return false;}
    std::vector<std::string> candidate;std::string token;
    for(size_t i=1;i<=path.size();++i) {
        if(i==path.size() || path[i]=='/') {candidate.push_back(std::move(token));token.clear();continue;}
        if(path[i]=='~') {
            if(i+1>=path.size() || (path[i+1]!='0' && path[i+1]!='1')){error="Invalid field path escape";return false;}
            token+=path[++i]=='0'?'~':'/';
        } else token+=path[i];
    }
    output=std::move(candidate);return true;
}
inline const PrefabValue* PrefabOverrideValue(const PrefabValue& node,const std::vector<std::string>& tokens) {
    const auto* value=&node;
    for(const auto& token:tokens) {
        if(!value->is_object() || !value->contains(token))return nullptr;
        value=&(*value)[token];
    }
    return value;
}
inline bool ReplacePrefabOverrideValue(PrefabValue& node,const std::vector<std::string>& tokens,const PrefabValue* value,std::string& error) {
    if(tokens.empty()){error="Field Revert cannot replace an entire Entity";return false;}
    auto* target=&node;
    for(size_t i=0;i+1<tokens.size();++i) {
        if(!target->is_object() || !target->contains(tokens[i]) || !(*target)[tokens[i]].is_object()){error="Field Revert cannot invent parent objects";return false;}
        target=&(*target)[tokens[i]];
    }
    if(value)(*target)[tokens.back()]=*value;else target->erase(tokens.back());
    return true;
}
// Revert one presently reported field to current source, advancing only that
// field of the persisted baseline. Caller owns source freshness/Editor locks.
// No allocation of Entities, source writes, or whole-instance propagation.
inline bool BuildPrefabFieldRevertCandidate(const Scene::EntityWorld& world,Scene::EntityId root,
    const PrefabValue& source,const std::string& guid,const PrefabOverrideEntry& requested,
    Scene::EntityWorld& output,std::string& error) {
    using Id=Scene::EntityId;
    if(requested.kind!=PrefabOverrideKind::Field && requested.kind!=PrefabOverrideKind::InstanceName){error="Entity addition/deletion requires structural Revert";return false;}
    PrefabOverrideReport report;if(!InspectPrefabOverrides(world,root,report,error))return false;
    if(report.sourceGuid!=guid){error="Field Revert source GUID mismatch";return false;}
    const auto found=std::find_if(report.entries.begin(),report.entries.end(),[&](const auto& entry){return SamePrefabOverride(entry,requested);});
    if(found==report.entries.end()){error="Field Revert snapshot is stale or invented";return false;}
    PrefabValue normalized;if(!ValidatePrefabDocument(source,normalized,error))return false;
    PrefabInstanceCapture captured;if(!CapturePrefabInstance(world,root,captured,error))return false;
    if(normalized["root"].get<crude_json::number>()!=captured.baseline["root"].get<crude_json::number>()){error="Field Revert source root identity changed";return false;}
    const auto findNode=[](PrefabValue& document,Id id)->PrefabValue* {
        for(auto& node:document["world"]["entities"].get<crude_json::array>())if(node["id"].get<crude_json::number>()==double(id))return &node;
        return nullptr;
    };
    auto* base=findNode(captured.baseline,requested.local);auto* mine=findNode(captured.instance,requested.local);
    const auto* next=findNode(normalized,requested.local);
    if(!base || !mine || !next){error="Field Revert requires a mapped live Entity in baseline, instance and current source";return false;}
    std::vector<std::string> tokens;if(!PrefabOverrideTokens(requested.path,tokens,error))return false;
    const auto* value=PrefabOverrideValue(*next,tokens);
    if(!ReplacePrefabOverrideValue(*base,tokens,value,error) || !ReplacePrefabOverrideValue(*mine,tokens,value,error))return false;
    PrefabValue newBase,newMine;
    Scene::RetagEntityDocument(captured.baseline["world"]);Scene::RetagEntityDocument(captured.instance["world"]);
    if(!ValidatePrefabDocument(captured.baseline,newBase,error) || !ValidatePrefabDocument(captured.instance,newMine,error))return false;
    const auto* replacement=findNode(newMine,requested.local);
    const auto* rootEntity=world.Find(root);auto link=rootEntity->prefabLink;link["base"]=newBase["world"];
    auto authored=*replacement;authored["id"]=double(requested.scene);
    const auto localParent=Id(authored["parent"].get<crude_json::number>());
    authored["parent"]=requested.scene==root?double(rootEntity->parent):double(captured.sceneByLocal.at(localParent));
    auto document=world.Capture();
    for(auto& node:document["entities"].get<crude_json::array>()) {
        const auto id=Id(node["id"].get<crude_json::number>());
        if(id==requested.scene)node=authored;
        if(id==root)node["prefabLink"]=link;
    }
    if(document.dump().size()>Scene::SceneDocument::MaxBytes){error="Field Revert exceeds Scene document budget";return false;}
    Scene::RetagEntityDocument(document);Scene::EntityWorld candidate;if(!candidate.Restore(document,error))return false;
    candidate.RetainAllocationHighWatermark(world);output=std::move(candidate);error.clear();return true;
}
inline bool RevertPrefabField(Scene::EntityWorld& world,Scene::EntityId root,const PrefabValue& source,
    const std::string& guid,const PrefabOverrideEntry& requested,const EntitySelection& selection,
    EntityHistory& history,std::string& error,EntityDocumentState* document=nullptr) {
    try {
        if(document && document->HasPendingAction()){error="Field Revert document action is pending";return false;}
        const auto before=world.Capture();Scene::EntityWorld candidate;
        if(!BuildPrefabFieldRevertCandidate(world,root,source,guid,requested,candidate,error))return false;
        const auto selected=selection.Capture();EntitySelection checked;
        if(!checked.Restore(candidate,selected,error))return false;
        auto nextHistory=history;nextHistory.Commit();
        if(!nextHistory.Record(before,candidate,selected,selected,error))return false;
        nextHistory.Commit();const bool dirty=document && document->WouldBeDirty(candidate);
        world=std::move(candidate);history.Swap(nextHistory);if(document)document->ObservePrepared(dirty);
        error.clear();return true;
    }catch(const std::exception& exception){error=exception.what();return false;}
}
}
