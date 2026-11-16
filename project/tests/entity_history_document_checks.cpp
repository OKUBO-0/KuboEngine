#include "engine/editor/EntityHistory.h"
#include <cstdlib>
#include <functional>
#include <iostream>
#include <new>
#include <stdexcept>

namespace {bool denyReplayAllocations=false,countPreparationAllocations=false;size_t deniedReplayAllocations=0,preparationAllocationCount=0,failPreparationAllocation=0;}
void* operator new(size_t bytes){
    if(countPreparationAllocations && ++preparationAllocationCount==failPreparationAllocation){countPreparationAllocations=false;failPreparationAllocation=0;throw std::bad_alloc();}
    if(denyReplayAllocations){++deniedReplayAllocations;throw std::bad_alloc();}if(auto* memory=std::malloc(bytes?bytes:1))return memory;throw std::bad_alloc();
}
void* operator new[](size_t bytes){return ::operator new(bytes);}
void operator delete(void* memory) noexcept{std::free(memory);}
void operator delete[](void* memory) noexcept{std::free(memory);}
void operator delete(void* memory,size_t) noexcept{std::free(memory);}
void operator delete[](void* memory,size_t) noexcept{std::free(memory);}
namespace {
using namespace Engine::Editor;using World=Engine::Scene::EntityWorld;
void Require(bool condition,const char* why){if(!condition)throw std::runtime_error(why);}
// Contract provider only; native file ownership is tested separately.
class Effect final:public EntityHistoryEffect {
public:
    int external=2,begins=0,ends=0;bool held=false,fail=false,throwing=false,denyAfterSuccess=false;
    std::function<void(bool,bool)> observe;
    Effect():EntityHistoryEffect(128){}
    bool Begin(bool undo,std::string& error) override{
        ++begins;if(observe)observe(true,undo);
        if(fail || external!=(undo?2:1)){error="External replay refused";return false;}
        if(throwing)throw std::runtime_error("Injected replay failure");
        external=undo?1:2;held=true;if(denyAfterSuccess)denyReplayAllocations=true;return true;
    }
    void End() noexcept override{
        denyReplayAllocations=false;++ends;
        if(observe)observe(false,external==1);
        held=false;
    }
};
struct PreparationFixture {
    World world;EntitySelection selection;EntityDocumentState document;EntityHistory history;std::shared_ptr<Effect> effect;std::string error;
    PreparationFixture(){
        const auto root=world.CreateEntity("Before"),child=world.CreateEntity("Child",root);
        Require(selection.Assign(world,{root,child},child,error),error.c_str());document.Opened(world,L"preparation.scene");const auto before=world.Capture();const auto selected=selection.Capture();
        world.Find(root)->name="After";selection.Replace(root);document.Observe(world);effect=std::make_shared<Effect>();
        Require(history.Record(before,world,selected,selection.Capture(),error,effect),error.c_str());
        effect->observe=[](bool begin,bool){if(begin)countPreparationAllocations=false;};
    }
};
}
int main(){try {
    denyReplayAllocations=true;bool active=false;try{auto* memory=::operator new(1);::operator delete(memory);}catch(const std::bad_alloc&){active=true;}
    denyReplayAllocations=false;Require(active && deniedReplayAllocations==1,"Replay allocation-refusal probe inactive");deniedReplayAllocations=0;
    PreparationFixture allocationProbe;preparationAllocationCount=0;countPreparationAllocations=true;
    const bool probeReplayed=allocationProbe.history.Undo(allocationProbe.world,allocationProbe.selection,allocationProbe.error,&allocationProbe.document);
    countPreparationAllocations=false;Require(probeReplayed,"Preparation allocation probe failed");const auto preparationCount=preparationAllocationCount;Require(preparationCount>0,"Preparation allocation probe inactive");
    for(size_t failAt=1;failAt<=preparationCount;++failAt){
        PreparationFixture fixture;const auto heldWorld=fixture.world.Capture().dump();const auto heldSelection=fixture.selection.Capture();const auto heldHistory=fixture.history;const auto heldDocument=fixture.document;
        if(failAt==1 || failAt%100==0 || failAt==preparationCount)std::cout<<"RUN_REPLAY_ALLOCATION="<<failAt<<"/"<<preparationCount<<std::endl;
        preparationAllocationCount=0;failPreparationAllocation=failAt;countPreparationAllocations=true;
        const bool replayed=fixture.history.Undo(fixture.world,fixture.selection,fixture.error,&fixture.document);
        countPreparationAllocations=false;failPreparationAllocation=0;
        Require(!replayed && fixture.effect->begins==0 && fixture.world.Capture().dump()==heldWorld && fixture.selection.Capture()==heldSelection && fixture.history.HasSameState(heldHistory) && fixture.document.HasSameState(heldDocument),"Preparation allocation failure invoked source/published World/selection/history/document");
    }
    std::string error;World world;const auto root=world.CreateEntity("Before"),child=world.CreateEntity("Child",root);
    EntitySelection selection;Require(selection.Assign(world,{root,child},child,error),error.c_str());
    const auto before=world.Capture();const auto beforeSelection=selection.Capture();
    EntityDocumentState document;document.Opened(world,L"日本語/保存.scene");
    world.Find(root)->name="After";selection.Replace(root);document.Observe(world);
    const auto after=world.Capture();const auto afterSelection=selection.Capture();
    EntityHistory history;auto effect=std::make_shared<Effect>();
    Require(history.Record(before,world,beforeSelection,afterSelection,error,effect),error.c_str());
    const auto pendingHistory=history;const auto dirtyDocument=document;
    effect->fail=true;
    Require(!history.Undo(world,selection,error,&document) && document.HasSameState(dirtyDocument) && history.HasSameState(pendingHistory) && world.Capture().dump()==after.dump() && selection.Capture()==afterSelection,"Failed effect published document/history/world/selection");
    effect->fail=false;effect->throwing=true;
    Require(!history.Undo(world,selection,error,&document) && document.HasSameState(dirtyDocument) && history.HasSameState(pendingHistory) && effect->ends==2,"Throwing effect changed document or leaked lease");effect->throwing=false;
    Require(document.Request(EntityDocumentState::Action::Close),"Pending document fixture failed");
    const auto pendingDocument=document;const auto calls=effect->begins;
    Require(!history.Undo(world,selection,error,&document) && effect->begins==calls && document.HasSameState(pendingDocument) && history.HasSameState(pendingHistory),"Pending document action invoked effect/changed state");document.Cancel();
    world.Find(root)->active=false;const auto staleWorld=world.Capture().dump();
    Require(!history.Undo(world,selection,error,&document) && effect->begins==calls && document.HasSameState(dirtyDocument) && world.Capture().dump()==staleWorld,"Stale World changed document/invoked effect");world.Find(root)->active=true;
    effect->observe=[&](bool begin,bool undo){
        if(begin){Require(document.IsDirty()==undo,"Document dirty changed before external Begin");return;}
        if(effect->held){
            Require(deniedReplayAllocations==0 && document.IsDirty()!=undo,"Replay allocated after source success or published dirty after End");
            Require(world.Capture().dump()==(undo?before:after).dump() && selection.Capture()==(undo?beforeSelection:afterSelection) && (undo?history.CanRedo():history.CanUndo()),"Lease released before prepared World/selection/history");
        }
    };
    effect->denyAfterSuccess=true;
    Require(history.Undo(world,selection,error,&document) && !document.IsDirty() && effect->external==1 && !effect->held,error.c_str());
    Require(history.Redo(world,selection,error,&document) && document.IsDirty() && effect->external==2 && !effect->held,error.c_str());
    effect->observe={};document.MarkSaved(world);Require(!document.IsDirty(),"Changed save baseline fixture failed");
    Require(history.Undo(world,selection,error,&document) && document.IsDirty(),"Undo ignored latest saved baseline");
    Require(history.Redo(world,selection,error,&document) && !document.IsDirty(),"Redo ignored latest saved baseline");
    document.Opened(world,{});Require(document.IsUntitled() && document.IsDirty(),"Untitled fixture failed");
    Require(history.Undo(world,selection,error,&document) && document.IsDirty() && history.Redo(world,selection,error,&document) && document.IsDirty(),"Replay cleared untitled dirty");
    EntityHistory sourceOnly;auto only=std::make_shared<Effect>();Require(sourceOnly.Record(after,world,afterSelection,afterSelection,error,only),error.c_str());
    document.Opened(world,L"日本語/source-only.scene");
    Require(sourceOnly.Undo(world,selection,error,&document) && !document.IsDirty() && sourceOnly.Redo(world,selection,error,&document) && !document.IsDirty(),"Source-only replay dirtied clean Scene");
    World plain;const auto id=plain.CreateEntity("Plain");EntitySelection plainSelection;plainSelection.Replace(id);EntityHistory plainHistory;EntityDocumentState plainDocument;plainDocument.Opened(plain,L"plain.scene");const auto plainBefore=plain.Capture();const auto s=plainSelection.Capture();plain.Find(id)->name="Edited";plainDocument.Observe(plain);Require(plainHistory.Record(plainBefore,plain,s,s,error),error.c_str());
    Require(plainHistory.Undo(plain,plainSelection,error,&plainDocument) && !plainDocument.IsDirty() && plainHistory.Redo(plain,plainSelection,error,&plainDocument) && plainDocument.IsDirty(),"Scene-only replay document state wrong");
    auto legacyId=id;Require(plainHistory.Undo(plain,legacyId,error) && legacyId==id && plainHistory.Redo(plain,legacyId,error) && legacyId==id,"Legacy selection-only overload broken");
    Require(deniedReplayAllocations==0,"Post-effect publication allocation detected");
    std::cout<<"PASS: all "<<preparationCount<<" preparation allocation-failure positions preserve World/selection/history/document without invoking source Begin\n";
    std::cout<<"PASS: prepared replay document dirty under effect ownership without post-success allocation, pending/failure/exception/stale isolation, changed saved baseline/untitled/source-only/Scene-only and legacy overloads (contract provider, native tested separately)\n";return 0;
}catch(const std::exception& exception){denyReplayAllocations=false;countPreparationAllocations=false;std::cerr<<"FAIL: "<<exception.what()<<"\n";return 1;}}
