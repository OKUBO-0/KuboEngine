#pragma once
#include "PrefabThreeWay.h"
#include "PrefabFieldApplyPlan.h"

namespace Engine::Editor {
inline bool SamePrefabThreeWayEntry(const PrefabThreeWayEntry& a,const PrefabThreeWayEntry& b) noexcept {
    return a.kind==b.kind && a.change==b.change && a.local==b.local && a.scene==b.scene &&
        a.name==b.name && a.path==b.path && a.baseline==b.baseline && a.source==b.source && a.instance==b.instance;
}
// Adopt one freshly reported source field into one instance and its baseline.
// Unlike override-only Revert, a SourceOnly row is an eligible target. Presence
// changes are separate structural commands; no Entities or source files change.
// Caller owns native source revision freshness and Editor authoring guards.
inline bool BuildPrefabFieldAdoptionCandidate(const Scene::EntityWorld& world,Scene::EntityId root,
    const PrefabValue& source,const std::string& guid,const PrefabThreeWayEntry& requested,
    Scene::EntityWorld& output,std::string& error,bool approveConflict=false) {
    try {
        static_assert(std::is_nothrow_move_assignable_v<Scene::EntityWorld>);
        if(&world==&output){error="Field adoption candidate must not alias the input World";return false;}
        if(requested.kind!=PrefabThreeWayKind::Field && requested.kind!=PrefabThreeWayKind::InstanceName){error="Entity presence requires structural adoption";return false;}
        PrefabThreeWayReport fresh;if(!InspectPrefabThreeWay(world,root,source,guid,fresh,error))return false;
        if(std::none_of(fresh.entries.begin(),fresh.entries.end(),[&](const auto& entry){return SamePrefabThreeWayEntry(entry,requested);})){error="Field adoption snapshot is stale or invented";return false;}
        if(requested.change==PrefabThreeWayChange::Conflict && !approveConflict){error="Field adoption conflict requires explicit source-value approval";return false;}
        PrefabOverrideEntry field;field.kind=requested.kind==PrefabThreeWayKind::InstanceName?PrefabOverrideKind::InstanceName:PrefabOverrideKind::Field;
        field.local=requested.local;field.scene=requested.scene;field.name=requested.name;field.path=requested.path;field.baseline=requested.baseline;field.instance=requested.instance;
        Scene::EntityWorld candidate;PrefabMergeReport report;
        // The elected adoption path always adopts the selected source value;
        // other matching roots are deliberately not traversed or propagated.
        if(!BuildPrefabFieldInstanceCandidate(world,root,source,guid,field,true,candidate,report,error))return false;
        output=std::move(candidate);error.clear();return true;
    }catch(const std::exception& exception){error=exception.what();return false;}
}
inline bool AdoptPrefabField(Scene::EntityWorld& world,Scene::EntityId root,const PrefabValue& source,
    const std::string& guid,const PrefabThreeWayEntry& requested,const EntitySelection& selection,
    EntityHistory& history,std::string& error,EntityDocumentState* document=nullptr,bool approveConflict=false) {
    try {
        static_assert(std::is_nothrow_move_assignable_v<Scene::EntityWorld>);
        if(document && document->HasPendingAction()){error="Field adoption document action is pending";return false;}
        const auto before=world.Capture();Scene::EntityWorld candidate;
        if(!BuildPrefabFieldAdoptionCandidate(world,root,source,guid,requested,candidate,error,approveConflict))return false;
        const auto selected=selection.Capture();EntitySelection checked;if(!checked.Restore(candidate,selected,error))return false;
        auto nextHistory=history;nextHistory.Commit();if(!nextHistory.Record(before,candidate,selected,selected,error))return false;
        nextHistory.Commit();const bool dirty=document && document->WouldBeDirty(candidate);
        world=std::move(candidate);history.Swap(nextHistory);if(document)document->ObservePrepared(dirty);
        error.clear();return true;
    }catch(const std::exception& exception){error=exception.what();return false;}
}
}
