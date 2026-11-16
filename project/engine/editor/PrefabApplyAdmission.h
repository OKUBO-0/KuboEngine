#pragma once
#include "PrefabApplyPlan.h"
#include "PrefabFieldApplyPlan.h"
#include "EntityHistory.h"
#include "EntityDocumentState.h"

namespace Engine::Editor {
// The initial source publication is distinct from subsequent Undo/Redo replay.
// A concrete provider must validate native revision/identity, publish with a
// retained recovery intent, and keep source ownership until End. It must not
// re-enter or mutate the live Scene/selection/history. Failed publication must
// preserve the source or retain diagnosed recovery data. Bytes includes reserved
// initial-publication and replay payload; it cannot grow after admission.
class PrefabApplyEffect:public EntityHistoryEffect {
public:
    explicit PrefabApplyEffect(size_t retainedBytes):EntityHistoryEffect(retainedBytes){}
    virtual bool PublishInitial(const PrefabValue& before,const PrefabValue& after,
        const std::string& guid,std::string& error)=0;
};

// Read-only admission, followed by a serialized, single-use publication. Every
// candidate/history allocation and budget check happens before PublishInitial.
// The caller owns a freshly validated source revision even for Scene-only/no-op
// refresh. Value equality here is not native file identity/freshness evidence.
// This core does not implement the native provider or Game's authoring guards.
class PrefabApplyAdmission final {
    using World=Scene::EntityWorld;
    enum class State {Empty,Ready,Consumed};
    State state_=State::Empty;
    struct Payload {
        PrefabApplyPlan plan;
        PrefabValue originalSource;
        std::string guid,beforeWorld;
        Scene::EntityId beforeHighWater=0;
        EntitySelection::Snapshot beforeSelection;
        EntitySelection nextSelection;
        EntityHistory beforeHistory,nextHistory;
        std::shared_ptr<PrefabApplyEffect> effect;
        bool sourceChanged=false,worldChanged=false;
        std::unique_ptr<EntityDocumentState> beforeDocument;
        bool nextDirty=false;
    };
    std::unique_ptr<Payload> data_;
public:
    PrefabApplyAdmission()=default;
    PrefabApplyAdmission(const PrefabApplyAdmission&)=delete;
    PrefabApplyAdmission& operator=(const PrefabApplyAdmission&)=delete;
    bool IsReady() const noexcept{return state_==State::Ready;}
    bool SourceChanged() const noexcept{return data_ && data_->sourceChanged;}
    bool WorldChanged() const noexcept{return data_ && data_->worldChanged;}
    bool Prepare(const World& world,Scene::EntityId root,const PrefabValue& source,const std::string& guid,
        const EntitySelection& selection,const EntityHistory& history,std::shared_ptr<PrefabApplyEffect> effect,std::string& error,
        const EntityDocumentState* document=nullptr) {
        return PrepareInternal(world,root,source,guid,selection,history,std::move(effect),error,document,nullptr);
    }
    bool PrepareField(const World& world,Scene::EntityId root,const PrefabValue& source,const std::string& guid,
        const PrefabOverrideEntry& field,const EntitySelection& selection,const EntityHistory& history,
        std::shared_ptr<PrefabApplyEffect> effect,std::string& error,const EntityDocumentState* document=nullptr) {
        return PrepareInternal(world,root,source,guid,selection,history,std::move(effect),error,document,&field);
    }
private:
    bool PrepareInternal(const World& world,Scene::EntityId root,const PrefabValue& source,const std::string& guid,
        const EntitySelection& selection,const EntityHistory& history,std::shared_ptr<PrefabApplyEffect> effect,std::string& error,
        const EntityDocumentState* document,const PrefabOverrideEntry* field) {
        try {
            if(state_!=State::Empty){error="Apply admission is already used";return false;}
            if(field && document && document->HasPendingAction()){error="Field Apply document action is pending";return false;}
            PrefabValue normalized;if(!ValidatePrefabDocument(source,normalized,error))return false;
            const auto before=world.Capture();const auto selected=selection.Capture();EntitySelection checkedSelection;
            if(!checkedSelection.Restore(world,selected,error))return false;
            PrefabApplyPlan candidate;
            if(field) {
                std::unique_ptr<PrefabApplyPlan> plan;
                if(!BuildPrefabFieldApplyPlan(world,root,normalized,guid,*field,plan,error))return false;
                candidate=std::move(*plan);
            } else if(!BuildPrefabApplyPlan(world,root,normalized,guid,candidate,error))return false;
            const auto beforeText=before.dump();
            const bool sourceChanged=candidate.source.dump()!=normalized.dump();
            const bool worldChanged=candidate.world.Capture().dump()!=beforeText;
            if(sourceChanged!=bool(effect)){error=sourceChanged?"Source Apply requires an initial-publication history effect":"Scene-only/no-op Apply must not own a source effect";return false;}
            auto primary=checkedSelection.Primary();checkedSelection.Synchronize(candidate.world,primary);
            EntityHistory baseline=history,next=history;
            if(sourceChanged || worldChanged) {
                // Keep this command separate from pending inspector edits even
                // for a baseline refresh whose physical source stays unchanged.
                next.Commit();
                if(!next.Record(before,candidate.world,selected,checkedSelection.Capture(),error,effect))return false;
                next.Commit();
            }
            // Even retained-output assignments may allocate. Complete all of
            // them locally before the no-throw unique_ptr admission publication.
            auto data=std::make_unique<Payload>();
            data->plan=std::move(candidate);data->originalSource=std::move(normalized);data->guid=guid;data->beforeWorld=beforeText;
            data->beforeHighWater=world.AllocationHighWatermark();data->beforeSelection=selected;
            data->nextSelection=std::move(checkedSelection);data->beforeHistory.Swap(baseline);data->nextHistory.Swap(next);
            data->effect=std::move(effect);data->sourceChanged=sourceChanged;data->worldChanged=worldChanged;
            if(document){data->beforeDocument=std::make_unique<EntityDocumentState>(*document);data->nextDirty=document->WouldBeDirty(data->plan.world);}
            data_=std::move(data);state_=State::Ready;
            error.clear();return true;
        }catch(const std::exception& exception){error=exception.what();return false;}
    }
public:
    bool Publish(World& world,EntitySelection& selection,EntityHistory& history,std::string& error,EntityDocumentState* document=nullptr) {
        static_assert(std::is_nothrow_move_assignable_v<World> && std::is_nothrow_move_assignable_v<EntitySelection>);
        try {
            if(state_!=State::Ready){error="Apply admission is not ready/already consumed";return false;}
            auto& data=*data_;
            if(bool(document)!=bool(data.beforeDocument) || (document && !document->HasSameState(*data.beforeDocument))) {
                error="Apply document baseline/path/action changed; request confirmation again";return false;
            }
            if(world.Capture().dump()!=data.beforeWorld || world.AllocationHighWatermark()!=data.beforeHighWater ||
                selection.Capture()!=data.beforeSelection || !history.HasSameState(data.beforeHistory)) {
                error="Apply admission changed: rebuild from current Scene/selection/history";return false;
            }
            state_=State::Consumed;
            if(!data.sourceChanged && !data.worldChanged){error.clear();return true;}
            struct Lease {std::shared_ptr<PrefabApplyEffect> effect;~Lease(){if(effect)effect->End();}} lease{data.effect};
            if(lease.effect && !lease.effect->PublishInitial(data.originalSource,data.plan.source,data.guid,error))return false;
            // No allocating validation/history/selection work after source success.
            world=std::move(data.plan.world);selection=std::move(data.nextSelection);history.Swap(data.nextHistory);
            if(document)document->ObservePrepared(data.nextDirty);
            error.clear();return true;
        }catch(const std::exception& exception){error=exception.what();return false;}
    }
};
}
