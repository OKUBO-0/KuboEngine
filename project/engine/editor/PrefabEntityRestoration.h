#pragma once
#include "PrefabFieldAdoption.h"
#include "PrefabApplyPlan.h"

namespace Engine::Editor {
struct PrefabEntityRestorationPlan {
    Scene::EntityWorld world;
    // Requested node and missing parents, ordered parent-first. Canonical rows
    // disclose changed deleted ancestors as well as new-source dependencies.
    std::vector<PrefabThreeWayEntry> restorations;
    bool RequiresApproval() const noexcept {
        return std::any_of(restorations.begin(),restorations.end(),[](const auto& row){return row.change==PrefabThreeWayChange::Conflict;});
    }
};
static_assert(std::is_nothrow_move_assignable_v<PrefabEntityRestorationPlan>);

// Read-only restoration proposal. Caller owns native revision and World/session
// admission; explicit conflict approval is never a source freshness bypass.
inline bool BuildPrefabEntityRestorationPlan(const Scene::EntityWorld& world,Scene::EntityId root,
    const PrefabValue& source,const std::string& guid,const PrefabThreeWayEntry& requested,
    PrefabEntityRestorationPlan& output,std::string& error) {
    try {
        if(&world==&output.world){error="Entity restoration plan must not alias input World";return false;}
        if(requested.kind!=PrefabThreeWayKind::EntityPresence || !requested.baseline || !requested.source || requested.instance){error="Entity restoration requires an instance-deleted mapped presence row";return false;}
        PrefabThreeWayReport report;if(!InspectPrefabThreeWay(world,root,source,guid,report,error))return false;
        if(std::none_of(report.entries.begin(),report.entries.end(),[&](const auto& row){return SamePrefabThreeWayEntry(row,requested);})){error="Entity restoration comparison is stale or invented";return false;}
        PrefabValue normalized;if(!ValidatePrefabDocument(source,normalized,error))return false;
        PrefabInstanceCapture capture;if(!CapturePrefabInstance(world,root,capture,error,&normalized))return false;
        using Id=Scene::EntityId;using Nodes=std::map<Id,const PrefabValue*>;
        const auto index=[](const PrefabValue& document){Nodes nodes;for(const auto& node:document["world"]["entities"].get<crude_json::array>())nodes.emplace(Id(node["id"].get<crude_json::number>()),&node);return nodes;};
        const auto incoming=index(normalized),base=index(capture.baseline),mine=index(capture.instance);
        std::map<Id,const PrefabThreeWayEntry*> rows;
        for(const auto& row:report.entries)if(row.kind==PrefabThreeWayKind::EntityPresence)rows.emplace(row.local,&row);
        std::vector<Id> missing;std::set<Id> visited;Id next=requested.local;
        while(!mine.contains(next)){
            if(!next || !incoming.contains(next) || !visited.insert(next).second || missing.size()>=10000){error="Entity restoration parent dependency is invalid or exceeds budget";return false;}
            if(!rows.contains(next) || !rows.at(next)->source || rows.at(next)->instance){error="Entity restoration dependency lacks a fresh missing-instance presence row";return false;}
            missing.push_back(next);next=Id((*incoming.at(next))["parent"].get<crude_json::number>());
        }
        if(!base.contains(next)){error="Entity restoration anchor is instance-owned; resolve its identity explicitly";return false;}
        auto baseline=capture.baseline,instance=capture.instance;PrefabEntityRestorationPlan candidate;
        for(auto it=missing.rbegin();it!=missing.rend();++it){
            const auto id=*it;const auto& node=*incoming.at(id);
            auto& baselineNodes=baseline["world"]["entities"].get<crude_json::array>();
            const auto existing=std::find_if(baselineNodes.begin(),baselineNodes.end(),[&](const auto& value){return value["id"].template get<crude_json::number>()==double(id);});
            if(existing==baselineNodes.end())baselineNodes.push_back(node);else *existing=node;
            instance["world"]["entities"].push_back(node);candidate.restorations.push_back(*rows.at(id));
        }
        Scene::RetagEntityDocument(baseline["world"]);Scene::RetagEntityDocument(instance["world"]);if(!BuildPrefabMergedCandidate(world,root,guid,capture,baseline,instance,candidate.world,error))return false;
        output=std::move(candidate);error.clear();return true;
    }catch(const std::exception& exception){error=exception.what();return false;}
}

inline bool BuildPrefabEntityRestorationCandidate(const Scene::EntityWorld& world,Scene::EntityId root,
    const PrefabValue& source,const std::string& guid,const PrefabThreeWayEntry& requested,
    Scene::EntityWorld& output,std::string& error,bool approveConflicts=false) {
    try {
        if(&world==&output){error="Entity restoration candidate must not alias input World";return false;}
        PrefabEntityRestorationPlan plan;if(!BuildPrefabEntityRestorationPlan(world,root,source,guid,requested,plan,error))return false;
        if(plan.RequiresApproval() && !approveConflicts){error="Entity restoration requires explicit source-value approval for all changed deleted nodes";return false;}
        output=std::move(plan.world);error.clear();return true;
    }catch(const std::exception& exception){error=exception.what();return false;}
}

inline bool AdoptPrefabEntityRestoration(Scene::EntityWorld& world,Scene::EntityId root,
    const PrefabValue& source,const std::string& guid,const PrefabThreeWayEntry& requested,
    const EntitySelection& selection,EntityHistory& history,std::string& error,
    EntityDocumentState* document=nullptr,bool approveConflicts=false) {
    static_assert(std::is_nothrow_move_assignable_v<Scene::EntityWorld>);
    try {
        if(document && document->HasPendingAction()){error="Entity restoration document action is pending";return false;}
        const auto selected=selection.Capture();EntitySelection checked;if(!checked.Restore(world,selected,error))return false;
        const auto before=world.Capture();Scene::EntityWorld candidate;
        if(!BuildPrefabEntityRestorationCandidate(world,root,source,guid,requested,candidate,error,approveConflicts))return false;
        if(!checked.Restore(candidate,selected,error))return false;
        auto nextHistory=history;nextHistory.Commit();if(!nextHistory.Record(before,candidate,selected,selected,error))return false;
        nextHistory.Commit();const bool dirty=document && document->WouldBeDirty(candidate);
        world=std::move(candidate);history.Swap(nextHistory);if(document)document->ObservePrepared(dirty);
        error.clear();return true;
    }catch(const std::exception& exception){error=exception.what();return false;}
}
}
