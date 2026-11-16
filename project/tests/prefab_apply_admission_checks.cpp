#include "engine/editor/PrefabApplyAdmission.h"
#include "engine/editor/PrefabInstance.h"
#include "engine/editor/EntityDocumentState.h"
#include <functional>
#include <iostream>
#include <limits>
#include <cstdlib>
namespace {bool denyApplyAllocations=false;size_t deniedApplyAllocations=0;}
void* operator new(size_t bytes) {
    if(denyApplyAllocations){++deniedApplyAllocations;throw std::bad_alloc();}
    if(auto* memory=std::malloc(bytes?bytes:1))return memory;throw std::bad_alloc();
}
void* operator new[](size_t bytes){return ::operator new(bytes);}
void operator delete(void* memory) noexcept{std::free(memory);}
void operator delete[](void* memory) noexcept{std::free(memory);}
void operator delete(void* memory,size_t) noexcept{std::free(memory);}
void operator delete[](void* memory,size_t) noexcept{std::free(memory);}
namespace {
using namespace Engine::Editor;using namespace Engine::Scene;
void Require(bool condition,const char* why){if(!condition)throw std::runtime_error(why);}
// Contract provider only: integer external state is not native Prefab I/O.
class Effect final:public PrefabApplyEffect {
public:
    int source=0,initials=0,replays=0,ends=0;bool held=false,fail=false,throwing=false,denyAfterInitial=false;
    std::string expectedBefore,expectedAfter,guid;
    std::function<void(bool)> observe;
    explicit Effect(size_t cost):PrefabApplyEffect(cost){}
    bool PublishInitial(const PrefabValue& before,const PrefabValue& after,const std::string& identity,std::string& error) override {
        ++initials;if(observe)observe(true);
        if(fail || source!=0 || before.dump()!=expectedBefore || after.dump()!=expectedAfter || identity!=guid){error="Initial source changed/refused";return false;}
        if(throwing)throw std::runtime_error("Injected initial source exception");
        source=1;held=true;error.clear();if(denyAfterInitial)denyApplyAllocations=true;return true;
    }
    bool Begin(bool undo,std::string& error) override {
        ++replays;if(source!=(undo?1:0)){error="Replay source changed";return false;}
        source=undo?0:1;held=true;return true;
    }
    void End() noexcept override {denyApplyAllocations=false;++ends;held=false;if(observe)observe(false);}
};
}
int main(){try {
    denyApplyAllocations=true;bool allocationGuardActive=false;
    try{auto* memory=::operator new(1);::operator delete(memory);}catch(const std::bad_alloc&){allocationGuardActive=true;}
    denyApplyAllocations=false;Require(allocationGuardActive && deniedApplyAllocations==1,"Allocation refusal instrumentation inactive");deniedApplyAllocations=0;
    const std::string guid="0123456789abcdef0123456789abcdef";std::string error;
    EntityWorld source;source.CreateEntity("Root");source.CreateEntity("Child",1);source.CreateEntity("Deleted",1);
    PrefabValue definition;Require(CapturePrefabSubtree(source,1,definition,error),error.c_str());
    for(const auto* scenario:{"success","scene-budget","effect-budget","effect-overflow","stale-world","stale-selection","stale-anchor","stale-high-water",
        "stale-history-pending","stale-history-committed","stale-history-redo","stale-document-baseline","stale-document-path","stale-document-action","document-unbound","source-failure","source-exception","wrong-guid","bad-selection","missing-effect"}) {
        const std::string mode=scenario;EntityWorld world;world.CreateEntity("Outside");EntitySelection selection;EntityHistory construction;EntityId chosen=0,other=0;
        Require(InstantiatePrefab(world,definition,guid,1,selection,construction,chosen,error) && InstantiatePrefab(world,definition,guid,1,selection,construction,other,error),error.c_str());
        Require(chosen==2 && other==5,"Fixture allocation changed");
        Require(selection.Assign(world,{3,7},7,error),error.c_str());const auto prior=world.Capture();
        world.Find(chosen)->name="Applied name";world.Find(3)->transform.position.x=7;
        Require(world.DestroyEntity(4),"Chosen source deletion failed");world.Find(other)->name="Other override";world.Find(6)->active=false;
        const auto before=world.Capture();const auto beforeSelection=selection.Capture();
        EntityHistory history;Require(history.Record(prior,world,beforeSelection,beforeSelection,error),error.c_str());
        if(mode=="scene-budget")history=EntityHistory(100,1);
        if(mode=="effect-budget")history=EntityHistory(100,64*1024);
        const auto unchangedHistory=history;
        const size_t cost=mode=="effect-overflow"?(std::numeric_limits<size_t>::max)():mode=="effect-budget"?64*1024:1024;
        auto effect=std::make_shared<Effect>(cost);PrefabApplyPlan expected;Require(BuildPrefabApplyPlan(world,chosen,definition,guid,expected,error),error.c_str());
        effect->expectedBefore=definition.dump();effect->expectedAfter=expected.source.dump();effect->guid=guid;
        EntityDocumentState document;document.Opened(world,std::filesystem::path(L"保存.scene"));const auto wasDirty=document.IsDirty();
        if(mode=="bad-selection")selection.Replace(9999);
        const auto liveSelection=selection.Capture();PrefabApplyAdmission command;
        const bool admitted=command.Prepare(world,chosen,definition,mode=="wrong-guid"?"ffffffffffffffffffffffffffffffff":guid,selection,history,mode=="missing-effect"?nullptr:effect,error,&document);
        Require(world.Capture().dump()==before.dump() && selection.Capture()==liveSelection && history.HasSameState(unchangedHistory) && effect->initials==0,"Prepare changed live state/invoked source");
        if(mode=="scene-budget" || mode=="effect-budget" || mode=="effect-overflow" || mode=="wrong-guid" || mode=="bad-selection" || mode=="missing-effect") {
            Require(!admitted && !command.IsReady() && !command.SourceChanged() && !command.Publish(world,selection,history,error) && effect->initials==0,"Invalid/budget admission published source/candidate");continue;
        }
        Require(admitted && command.IsReady() && command.SourceChanged() && command.WorldChanged(),error.c_str());
        Require(!command.Prepare(world,chosen,definition,guid,selection,history,effect,error) && command.IsReady(),"Ready admission reused/invalidated");
        if(mode=="stale-world")world.Find(1)->active=false;
        if(mode=="stale-selection")selection.Replace(chosen);
        if(mode=="stale-anchor"){auto altered=selection.Capture();altered.anchor=chosen;Require(selection.Restore(world,altered,error),error.c_str());}
        if(mode=="stale-high-water"){const auto transient=world.CreateEntity("Temporary");Require(world.DestroyEntity(transient) && world.Capture().dump()==before.dump(),"High-water fixture changed snapshot");}
        if(mode=="stale-history-committed")history.Commit();
        if(mode=="stale-history-pending"){auto alternate=world;alternate.Find(1)->name="Different same-count history payload";EntityHistory replacement;Require(replacement.Record(prior,alternate,beforeSelection,beforeSelection,error),error.c_str());history.Swap(replacement);}
        if(mode=="stale-history-redo"){auto temporary=world;auto transientSelection=selection;EntityHistory replacement;Require(replacement.Record(prior,world,beforeSelection,beforeSelection,error) && replacement.Undo(temporary,transientSelection,error),error.c_str());history.Swap(replacement);}
        if(mode=="stale-document-baseline"){auto alternate=world;alternate.Find(1)->name="Other saved baseline";document.MarkSaved(alternate);}
        if(mode=="stale-document-path")Require(document.SavedAs(world,std::filesystem::path(L"別.scene")),"Doc path fixture failed");
        if(mode=="stale-document-action")Require(document.Request(EntityDocumentState::Action::Close),"Doc action fixture failed");
        const auto publishBefore=world.Capture().dump();const auto selectedBefore=selection.Capture();const auto historyBefore=history;
        if(mode.starts_with("stale-") || mode=="document-unbound") {
            const auto docBefore=document;
            Require(!command.Publish(world,selection,history,error,mode=="document-unbound"?nullptr:&document) && effect->initials==0 && world.Capture().dump()==publishBefore && selection.Capture()==selectedBefore && history.HasSameState(historyBefore) && document.HasSameState(docBefore),"Stale admission invoked source or published live state");continue;
        }
        effect->fail=mode=="source-failure";effect->throwing=mode=="source-exception";
        effect->denyAfterInitial=mode=="success";deniedApplyAllocations=0;
        bool beganOnOriginal=false,releasedAfterPublication=false;
        effect->observe=[&](bool begin){
            if(begin)beganOnOriginal=world.Capture().dump()==before.dump() && selection.Capture()==beforeSelection && history.HasSameState(unchangedHistory);
            else releasedAfterPublication=effect->source==1?world.Capture().dump()==expected.world.Capture().dump() && selection.Ids()==std::vector<EntityId>{3} && selection.Primary()==3 && history.UndoCount()==2 && document.IsDirty():
                world.Capture().dump()==before.dump() && history.HasSameState(unchangedHistory);
        };
        const bool published=command.Publish(world,selection,history,error,&document);
        Require(beganOnOriginal && releasedAfterPublication && !effect->held && effect->initials==1 && effect->ends==1 && !command.IsReady(),"Source ownership did not span candidate/history publication");
        if(mode!="success") {
            Require(!published && effect->source==0 && world.Capture().dump()==before.dump() && selection.Capture()==beforeSelection && history.HasSameState(unchangedHistory) && document.IsDirty()==wasDirty,"Source failure changed candidate/history/document");
            Require(!command.Publish(world,selection,history,error) && effect->initials==1,"Failed single-use source publication retried");continue;
        }
        Require(published && world.Find(other)->name=="Other override" && !world.Find(6)->active && world.Find(6)->transform.position.x==7 && !world.Find(7),"All-instance source propagation/overrides/deletion lost");
        Require(deniedApplyAllocations==0 && !denyApplyAllocations,"Initial Apply allocated after source success and before ownership release");
        const auto after=world.Capture().dump();const auto afterSelection=selection.Capture();effect->observe={};
        Require(!command.Publish(world,selection,history,error) && effect->initials==1,"Successful Apply reused source publication");
        document.Observe(world);Require(document.IsDirty(),"Apply did not change observed Scene baseline");
        Require(history.Undo(world,selection,error) && effect->source==0 && world.Capture().dump()==before.dump() && selection.Capture()==beforeSelection,"Initial Apply Undo lost source/full selection/Scene");
        document.Observe(world);Require(!document.IsDirty(),"Apply Undo did not restore observed saved baseline");
        Require(history.Redo(world,selection,error) && effect->source==1 && world.Capture().dump()==after && selection.Capture()==afterSelection,"Initial Apply Redo lost source/full selection/Scene");
        document.Observe(world);Require(document.IsDirty(),"Apply Redo did not restore observed dirty state");
    }
    for(const auto* scenario:{"success","missing-effect","budget","pending-prepare","stale-world","stale-selection","stale-high-water","stale-history","stale-document-baseline","stale-document-path","stale-document-action","document-unbound","source-failure","source-exception"}) {
        const std::string mode=scenario;EntityWorld world;world.CreateEntity("Outside");EntitySelection selection;EntityHistory construction;EntityId chosen=0,other=0;
        Require(InstantiatePrefab(world,definition,guid,1,selection,construction,chosen,error) && InstantiatePrefab(world,definition,guid,1,selection,construction,other,error),error.c_str());
        Require(selection.Assign(world,{3,7},7,error),error.c_str());const auto prior=world.Capture();world.Find(chosen)->name="Unselected elected name";world.Find(3)->transform.position.x=7;
        Require(world.DestroyEntity(4),"Field delete fixture failed");world.Find(other)->name="Unselected other name";world.Find(6)->active=false;
        PrefabOverrideReport report;Require(InspectPrefabOverrides(world,chosen,report,error),error.c_str());const auto selected=std::find_if(report.entries.begin(),report.entries.end(),[](const auto& entry){return entry.local==2 && entry.path=="/transform/position";});Require(selected!=report.entries.end(),"Field admission fixture has no target");
        std::unique_ptr<PrefabApplyPlan> expected;Require(BuildPrefabFieldApplyPlan(world,chosen,definition,guid,*selected,expected,error),error.c_str());
        EntityHistory history;Require(history.Record(prior,world,selection.Capture(),selection.Capture(),error),error.c_str());if(mode=="budget")history=EntityHistory(100,1);
        EntityDocumentState document;document.Opened(world,L"field.scene");if(mode=="pending-prepare")Require(document.Request(EntityDocumentState::Action::Close),"Pending field fixture failed");
        const auto before=world.Capture().dump();const auto full=selection.Capture();const auto beforeHistory=history;const auto beforeDocument=document;
        auto effect=std::make_shared<Effect>(1024);effect->guid=guid;effect->expectedBefore=definition.dump();effect->expectedAfter=expected->source.dump();PrefabApplyAdmission admission;
        const bool prepared=admission.PrepareField(world,chosen,definition,guid,*selected,selection,history,mode=="missing-effect"?nullptr:effect,error,&document);
        Require(world.Capture().dump()==before && selection.Capture()==full && history.HasSameState(beforeHistory) && document.HasSameState(beforeDocument) && effect->initials==0,"Field preparation published live state/source");
        if(mode=="budget" || mode=="missing-effect" || mode=="pending-prepare"){Require(!prepared && !admission.IsReady(),"Invalid field preparation admitted source");continue;}
        Require(prepared && admission.SourceChanged() && admission.WorldChanged(),error.c_str());
        if(mode=="stale-world")world.Find(1)->active=false;
        if(mode=="stale-selection")selection.Replace(chosen);
        if(mode=="stale-high-water"){const auto id=world.CreateEntity("Transient");Require(world.DestroyEntity(id) && world.Capture().dump()==before,"Field high-water fixture changed World");}
        if(mode=="stale-history")history.Commit();
        if(mode=="stale-document-baseline"){auto saved=world;saved.Find(1)->name="Other baseline";document.MarkSaved(saved);}
        if(mode=="stale-document-path")Require(document.SavedAs(world,L"other-field.scene"),"Field path fixture failed");
        if(mode=="stale-document-action")Require(document.Request(EntityDocumentState::Action::Close),"Field pending fixture failed");
        const auto publishBefore=world.Capture().dump();const auto publishSelection=selection.Capture();const auto publishHistory=history;const auto publishDocument=document;
        if(mode.starts_with("stale-") || mode=="document-unbound"){Require(!admission.Publish(world,selection,history,error,mode=="document-unbound"?nullptr:&document) && effect->initials==0 && world.Capture().dump()==publishBefore && selection.Capture()==publishSelection && history.HasSameState(publishHistory) && document.HasSameState(publishDocument),"Stale field admission invoked source/published state");continue;}
        effect->fail=mode=="source-failure";effect->throwing=mode=="source-exception";effect->denyAfterInitial=mode=="success";deniedApplyAllocations=0;bool endObserved=false;
        effect->observe=[&](bool begin){if(!begin)endObserved=effect->source==1?world.Capture().dump()==expected->world.Capture().dump() && selection.Capture()==full && document.IsDirty():world.Capture().dump()==before && history.HasSameState(beforeHistory) && document.HasSameState(beforeDocument);};
        const bool published=admission.Publish(world,selection,history,error,&document);
        Require(endObserved && effect->ends==1 && !effect->held && !admission.IsReady(),"Field source lease did not span Scene/history/dirty");
        if(mode!="success"){Require(!published && effect->source==0 && world.Capture().dump()==before && document.HasSameState(beforeDocument) && history.HasSameState(beforeHistory),"Failed field source published state");continue;}
        Require(published && deniedApplyAllocations==0 && history.UndoCount()==2 && world.Find(chosen)->name=="Unselected elected name" && world.Find(other)->name=="Unselected other name" && world.Find(7),"Field publication changed unselected names/deletion/selection or allocated after source success");
        const auto after=world.Capture().dump();effect->observe={};
        Require(history.Undo(world,selection,error,&document) && effect->source==0 && world.Capture().dump()==before && selection.Capture()==full && !document.IsDirty(),error.c_str());
        Require(history.Redo(world,selection,error,&document) && effect->source==1 && world.Capture().dump()==after && selection.Capture()==full && document.IsDirty(),error.c_str());
    }
    {
        EntityWorld world;EntitySelection selection;EntityHistory history;EntityId root=0;Require(InstantiatePrefab(world,definition,guid,0,selection,history,root,error),error.c_str());world.Find(root)->name="Current source name";
        PrefabOverrideReport report;Require(InspectPrefabOverrides(world,root,report,error) && report.entries.size()==1,error.c_str());auto current=definition;(*PrefabApplyFieldNode(current,1))["name"]="Current source name";
        EntityDocumentState document;document.Opened(world,L"equal-source.scene");PrefabApplyAdmission admission;
        Require(admission.PrepareField(world,root,current,guid,report.entries.front(),selection,history,nullptr,error,&document) && !admission.SourceChanged() && admission.WorldChanged() && admission.Publish(world,selection,history,error,&document) && document.IsDirty(),"Source-equal field baseline refresh required source effect or lost dirty");
    }
    std::cout<<"PASS: single-field prepared admission/budget, document binding/action guards, World/high-water/full-selection/history freeze, source failure/exception, allocation-free owned publication/dirty Undo/Redo and source-equal Scene-only baseline refresh (contract provider)\n";
    // Exact no-op does not commit pending history or invalidate an existing redo.
    for(const bool withRedo:{false,true}) {
        EntityWorld world;world.CreateEntity("Outside");EntitySelection selection;EntityHistory history;EntityId chosen=0;
        Require(InstantiatePrefab(world,definition,guid,0,selection,history,chosen,error),error.c_str());history.Clear();
        auto before=world.Capture();world.Find(1)->name="Edit outside";const auto selected=selection.Capture();
        Require(history.Record(before,world,selected,selected,error),error.c_str());
        if(withRedo)Require(history.Undo(world,selection,error),error.c_str());
        const auto frozen=history;PrefabApplyAdmission noOp;
        Require(noOp.Prepare(world,chosen,definition,guid,selection,history,nullptr,error) && !noOp.SourceChanged() && !noOp.WorldChanged() && noOp.Publish(world,selection,history,error) && history.HasSameState(frozen),"Exact no-op altered pending/redo history");
        auto unnecessary=std::make_shared<Effect>(1);PrefabApplyAdmission refused;
        Require(!refused.Prepare(world,chosen,definition,guid,selection,history,unnecessary,error) && unnecessary->initials==0,"No-op accepted source effect");
    }
    // External source refresh needs Scene history but no physical source effect.
    EntityWorld refresh;EntitySelection rs;EntityHistory rh;EntityId root=0;
    Require(InstantiatePrefab(refresh,definition,guid,0,rs,rh,root,error),error.c_str());rh.Clear();
    const auto original=refresh.Capture().dump();auto newer=definition;newer["world"]["entities"][size_t(1)]["transform"]["position"][size_t(0)]=8.0;
    PrefabApplyAdmission sceneOnly;
    Require(sceneOnly.Prepare(refresh,root,newer,guid,rs,rh,nullptr,error),error.c_str());
    Require(!sceneOnly.SourceChanged() && sceneOnly.WorldChanged(),"External refresh did not classify Scene-only change");
    Require(sceneOnly.Publish(refresh,rs,rh,error),error.c_str());
    Require(refresh.Find(2)->transform.position.x==8 && rh.UndoCount()==1,"Source-unchanged baseline refresh did not publish Scene history");
    Require(rh.Undo(refresh,rs,error) && refresh.Capture().dump()==original && rh.Redo(refresh,rs,error) && refresh.Find(2)->transform.position.x==8,"Scene-only refresh Undo/Redo failed");
    // Snapshot comparison must include limits, effect pointer, payload, pending
    // and redo; equal counts/bytes are not sufficient admission evidence.
    EntityHistory limitsA(100,4096),limitsB(99,4096);Require(!limitsA.HasSameState(limitsB),"History command limit ignored");
    Require(!limitsA.HasSameState(EntityHistory(100,4095)),"History byte limit ignored");
    auto alternateA=refresh,alternateB=refresh;alternateA.Find(1)->name="AA";alternateB.Find(1)->name="BB";
    EntityHistory payloadA,payloadB;const auto s=rs.Capture();const auto sharedBefore=refresh.Capture();
    Require(payloadA.Record(sharedBefore,alternateA,s,s,error) && payloadB.Record(sharedBefore,alternateB,s,s,error) && payloadA.StoredBytes()==payloadB.StoredBytes() && !payloadA.HasSameState(payloadB),"Equal-size pending history payload considered identical");
    auto effectA=std::make_shared<Effect>(16),effectB=std::make_shared<Effect>(16);EntityHistory identityA,identityB;
    Require(identityA.Record(sharedBefore,refresh,s,s,error,effectA) && identityB.Record(sharedBefore,refresh,s,s,error,effectB) && identityA.StoredBytes()==identityB.StoredBytes() && !identityA.HasSameState(identityB),"Equal-size source effect identities considered identical");
    std::cout<<"PASS: all-instance initial Apply admission/budget, full selection pruning/restoration, exact no-op/Scene-only refresh, stale Scene/high-water/selection/history refusal, single-use source-effect contract/exception isolation and ownership through preallocated publication/Undo/Redo (contract provider, not native source writing/Game UI)\n";return 0;
}catch(const std::exception& exception){std::cerr<<"FAIL: "<<exception.what()<<"\n";return 1;}}
