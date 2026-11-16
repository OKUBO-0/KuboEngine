#pragma once
#include "PrefabFieldAdoption.h"
#include "PrefabApplyPlan.h"

namespace Engine::Editor {
// One new source Entity plus only missing, new-source ancestor dependencies.
// Existing/deleted mapped parents are never implicitly reset or restored.
inline bool BuildPrefabEntityAdditionCandidate(const Scene::EntityWorld& world,Scene::EntityId root,
    const PrefabValue& source,const std::string& guid,const PrefabThreeWayEntry& requested,
    Scene::EntityWorld& output,std::string& error) {
    try {
        static_assert(std::is_nothrow_move_assignable_v<Scene::EntityWorld>);
        if(&world==&output){error="Entity addition output must not alias input World";return false;}
        if(requested.kind!=PrefabThreeWayKind::EntityPresence || requested.change!=PrefabThreeWayChange::SourceOnly ||
            requested.scene || requested.baseline || requested.instance || !requested.source){error="Entity addition requires a new source-only presence row";return false;}
        PrefabThreeWayReport report;if(!InspectPrefabThreeWay(world,root,source,guid,report,error))return false;
        if(std::none_of(report.entries.begin(),report.entries.end(),[&](const auto& row){return SamePrefabThreeWayEntry(row,requested);})){error="Entity addition comparison is stale or invented";return false;}
        PrefabValue normalized;if(!ValidatePrefabDocument(source,normalized,error))return false;
        PrefabInstanceCapture capture;if(!CapturePrefabInstance(world,root,capture,error,&normalized))return false;
        using Id=Scene::EntityId;using Nodes=std::map<Id,const PrefabValue*>;
        const auto index=[](const PrefabValue& document){Nodes result;for(const auto& node:document["world"]["entities"].get<crude_json::array>())result.emplace(Id(node["id"].get<crude_json::number>()),&node);return result;};
        const auto incoming=index(normalized),base=index(capture.baseline),mine=index(capture.instance);
        std::vector<Id> additions;std::set<Id> visited;Id next=requested.local;
        while(!(base.contains(next) && mine.contains(next))) {
            if(base.contains(next) || mine.contains(next)){error="Entity addition dependency is deleted or instance-owned; resolve it explicitly";return false;}
            if(!next || !incoming.contains(next) || !visited.insert(next).second || additions.size()>=10000){error="Entity addition ancestor dependency is invalid or exceeds budget";return false;}
            additions.push_back(next);next=Id((*incoming.at(next))["parent"].get<crude_json::number>());
        }
        auto newBase=capture.baseline,instance=capture.instance;
        for(auto it=additions.rbegin();it!=additions.rend();++it){newBase["world"]["entities"].push_back(*incoming.at(*it));instance["world"]["entities"].push_back(*incoming.at(*it));}
        Scene::RetagEntityDocument(newBase["world"]);Scene::RetagEntityDocument(instance["world"]);Scene::EntityWorld candidate;
        if(!BuildPrefabMergedCandidate(world,root,guid,capture,newBase,instance,candidate,error))return false;
        output=std::move(candidate);error.clear();return true;
    }catch(const std::exception& exception){error=exception.what();return false;}
}
inline bool AdoptPrefabEntityAddition(Scene::EntityWorld& world,Scene::EntityId root,const PrefabValue& source,
    const std::string& guid,const PrefabThreeWayEntry& requested,const EntitySelection& selection,
    EntityHistory& history,std::string& error,EntityDocumentState* document=nullptr) {
    try {
        if(document && document->HasPendingAction()){error="Entity addition document action is pending";return false;}
        const auto before=world.Capture();Scene::EntityWorld candidate;
        if(!BuildPrefabEntityAdditionCandidate(world,root,source,guid,requested,candidate,error))return false;
        const auto selected=selection.Capture();EntitySelection checked;if(!checked.Restore(candidate,selected,error))return false;
        auto nextHistory=history;nextHistory.Commit();if(!nextHistory.Record(before,candidate,selected,selected,error))return false;
        nextHistory.Commit();const bool dirty=document && document->WouldBeDirty(candidate);
        world=std::move(candidate);history.Swap(nextHistory);if(document)document->ObservePrepared(dirty);
        error.clear();return true;
    }catch(const std::exception& exception){error=exception.what();return false;}
}
}
