#pragma once
#include "PrefabFieldAdoption.h"
#include "PrefabApplyPlan.h"

namespace Engine::Editor {
struct PrefabEntityRemovalPlan {
    Scene::EntityWorld world;
    // Complete baseline/visible descendant closure, including absent tombstones.
    // Every row carries full canonical values for a caller's destructive preview.
    std::vector<PrefabThreeWayEntry> removals;
    std::vector<Scene::EntityId> removedEntities;
    bool RequiresApproval() const noexcept {
        return std::any_of(removals.begin(),removals.end(),[](const auto& row){return row.change==PrefabThreeWayChange::Conflict;});
    }
};
static_assert(std::is_nothrow_move_assignable_v<PrefabEntityRemovalPlan>);

// Read-only proposal, not permission to delete. A caller owns native source
// freshness and frozen World/session admission, must show the entire closure
// and approve conflicts before publication. Approval never bypasses dependencies.
inline bool BuildPrefabEntityRemovalPlan(const Scene::EntityWorld& world,Scene::EntityId root,
    const PrefabValue& source,const std::string& guid,const PrefabThreeWayEntry& requested,
    PrefabEntityRemovalPlan& output,std::string& error) {
    try {
        if(&world==&output.world){error="Entity removal plan must not alias input World";return false;}
        if(requested.kind!=PrefabThreeWayKind::EntityPresence || !requested.baseline || requested.source){
            error="Entity removal requires a source-deleted baseline presence row";return false;
        }
        PrefabThreeWayReport report;if(!InspectPrefabThreeWay(world,root,source,guid,report,error))return false;
        if(std::none_of(report.entries.begin(),report.entries.end(),[&](const auto& row){return SamePrefabThreeWayEntry(row,requested);})){error="Entity removal comparison is stale or invented";return false;}
        PrefabValue normalized;if(!ValidatePrefabDocument(source,normalized,error))return false;
        PrefabInstanceCapture capture;if(!CapturePrefabInstance(world,root,capture,error,&normalized))return false;
        using Id=Scene::EntityId;using Nodes=std::map<Id,const PrefabValue*>;
        const auto index=[](const PrefabValue& document){Nodes nodes;for(const auto& node:document["world"]["entities"].get<crude_json::array>())nodes.emplace(Id(node["id"].get<crude_json::number>()),&node);return nodes;};
        const auto base=index(capture.baseline),mine=index(capture.instance),incoming=index(normalized);
        const auto rootLocal=Id(capture.baseline["root"].get<crude_json::number>());
        std::map<Id,std::set<Id>> children;
        // Both graphs matter: a moved child still needs baseline cleanup; a
        // newly attached visible child must never be lost by Scene rebuilding.
        for(const auto* nodes:{&base,&mine})for(const auto& [id,node]:*nodes)children[Id((*node)["parent"].get<crude_json::number>())].insert(id);
        std::map<Id,const PrefabThreeWayEntry*> rows;
        for(const auto& row:report.entries)if(row.kind==PrefabThreeWayKind::EntityPresence)rows.emplace(row.local,&row);
        PrefabEntityRemovalPlan candidate;std::set<Id> removed{requested.local};std::vector<Id> queue{requested.local};
        for(size_t cursor=0;cursor<queue.size();++cursor){
            const auto id=queue[cursor];
            if(id==rootLocal){error="Entity removal cannot delete the linked root";return false;}
            if(!base.contains(id)){error="Entity removal would delete an instance-owned descendant; resolve its parent explicitly";return false;}
            if(incoming.contains(id)){error="Entity removal dependency survives in source; adopt its new parent explicitly";return false;}
            if(!rows.contains(id)){error="Entity removal dependency lacks a fresh presence comparison";return false;}
            candidate.removals.push_back(*rows.at(id));
            if(mine.contains(id))candidate.removedEntities.push_back(capture.sceneByLocal.at(id));
            for(const auto child:children[id])if(removed.insert(child).second){
                if(queue.size()>=10000){error="Entity removal closure exceeds 10000 Entities";return false;}queue.push_back(child);
            }
        }
        auto baseline=capture.baseline,instance=capture.instance;
        for(auto* document:{&baseline,&instance}){
            auto& nodes=(*document)["world"]["entities"].get<crude_json::array>();
            nodes.erase(std::remove_if(nodes.begin(),nodes.end(),[&](const auto& node){return removed.contains(Id(node["id"].template get<crude_json::number>()));}),nodes.end());
        }
        Scene::RetagEntityDocument(baseline["world"]);Scene::RetagEntityDocument(instance["world"]);if(!BuildPrefabMergedCandidate(world,root,guid,capture,baseline,instance,candidate.world,error))return false;
        output=std::move(candidate);error.clear();return true;
    }catch(const std::exception& exception){error=exception.what();return false;}
}

inline bool BuildPrefabEntityRemovalCandidate(const Scene::EntityWorld& world,Scene::EntityId root,
    const PrefabValue& source,const std::string& guid,const PrefabThreeWayEntry& requested,
    Scene::EntityWorld& output,std::string& error,bool approveConflicts=false) {
    try {
        if(&world==&output){error="Entity removal candidate must not alias input World";return false;}
        PrefabEntityRemovalPlan plan;if(!BuildPrefabEntityRemovalPlan(world,root,source,guid,requested,plan,error))return false;
        if(plan.RequiresApproval() && !approveConflicts){error="Entity removal conflicts require explicit approval of the complete closure";return false;}
        output=std::move(plan.world);error.clear();return true;
    }catch(const std::exception& exception){error=exception.what();return false;}
}

inline bool AdoptPrefabEntityRemoval(Scene::EntityWorld& world,Scene::EntityId root,
    const PrefabValue& source,const std::string& guid,const PrefabThreeWayEntry& requested,
    EntitySelection& selection,EntityHistory& history,std::string& error,
    EntityDocumentState* document=nullptr,bool approveConflicts=false) {
    static_assert(std::is_nothrow_move_assignable_v<Scene::EntityWorld> && std::is_nothrow_move_assignable_v<EntitySelection>);
    try {
        if(document && document->HasPendingAction()){error="Entity removal document action is pending";return false;}
        const auto selected=selection.Capture();EntitySelection checked;if(!checked.Restore(world,selected,error))return false;
        const auto before=world.Capture();Scene::EntityWorld candidate;
        if(!BuildPrefabEntityRemovalCandidate(world,root,source,guid,requested,candidate,error,approveConflicts))return false;
        auto nextSelection=selection;auto primary=nextSelection.Primary();nextSelection.Synchronize(candidate,primary);
        auto nextHistory=history;nextHistory.Commit();
        if(!nextHistory.Record(before,candidate,selected,nextSelection.Capture(),error))return false;
        nextHistory.Commit();const bool dirty=document && document->WouldBeDirty(candidate);
        world=std::move(candidate);selection=std::move(nextSelection);history.Swap(nextHistory);
        if(document)document->ObservePrepared(dirty);
        error.clear();return true;
    }catch(const std::exception& exception){error=exception.what();return false;}
}
}
