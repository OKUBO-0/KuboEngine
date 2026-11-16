#include "engine/editor/AnimatorControllerEdits.h"
#include <iostream>
#include <cstdlib>
#include <new>
static long failAfter=-1;static bool counting=false;static size_t allocations=0;
void* operator new(size_t bytes){if(counting)++allocations;if(failAfter==0){failAfter=-1;throw std::bad_alloc();}if(failAfter>0)--failAfter;if(auto p=std::malloc(bytes?bytes:1))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept{std::free(p);}void operator delete(void* p,size_t) noexcept{std::free(p);}
static void Need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
int main(){try {
    using namespace Engine::Editor;using namespace Engine::AnimationSystem::Controller;
    AnimatorControllerDocument doc;std::string error;
    Need(AnimatorControllerEdits(doc).New(error) && doc.IsDirty() && doc.IsUntitled() && doc.Definition().layers[0].states[0].name=="Idle","New Controller failed");
    auto unchanged=doc;Need(!AnimatorControllerEdits(doc).New(error) && doc.HasSameState(unchanged),"New dropped dirty draft");
    Need(AnimatorControllerEdits(doc).AddState(0,{"Middle","default",1,true},error) && AnimatorControllerEdits(doc).AddState(0,{"Run","default",2,true},error),"Add states failed");
    Need(AnimatorControllerEdits(doc).AddParameter({"unused",Kind::Bool,0,false},error) && AnimatorControllerEdits(doc).AddParameter({"speed",Kind::Float,0,false},error) && AnimatorControllerEdits(doc).AddParameter({"replacement",Kind::Float,0,false},error),"Add parameters failed");
    Transition edge;edge.from=0;edge.to=2;edge.conditions.push_back({1,"gt",0.5,false});
    Need(AnimatorControllerEdits(doc).AddTransition(0,edge,error),"Add edge failed");
    AnimatorControllerEdits stale(doc);auto state=doc.Definition().layers[0].states[2];state.name="走る###node";
    Need(AnimatorControllerEdits(doc).ReplaceState(0,2,state,error),"Rename state failed");unchanged=doc;
    Need(!stale.SetEntry(0,2,error) && doc.HasSameState(unchanged),"Stale UI indices edited new graph");
    Need(AnimatorControllerEdits(doc).RemoveState(0,1,{},error) && doc.Definition().layers[0].transitions[0].to==1 && doc.Definition().layers[0].states[1].name==state.name,"Unused state index compaction broke edge");
    unchanged=doc;Need(!AnimatorControllerEdits(doc).RemoveState(0,1,{},error) && doc.HasSameState(unchanged),"Referenced state silently removed");
    Need(AnimatorControllerEdits(doc).RemoveParameter(0,{},error) && doc.Definition().layers[0].transitions[0].conditions[0].parameter==0,"Unused parameter compaction broke condition");
    unchanged=doc;Need(!AnimatorControllerEdits(doc).RemoveParameter(0,{},error) && doc.HasSameState(unchanged),"Referenced parameter silently removed");
    Need(AnimatorControllerEdits(doc).RemoveParameter(0,1,error) && doc.Definition().parameters[0].name=="replacement" && doc.Definition().layers[0].transitions[0].conditions[0].parameter==0,"Same-kind parameter retarget failed");
    Need(AnimatorControllerEdits(doc).AddParameter({"different",Kind::Bool,0,false},error),"Replacement mismatch setup failed");unchanged=doc;
    Need(!AnimatorControllerEdits(doc).RemoveParameter(0,1,error) && doc.HasSameState(unchanged),"Kind-changing retarget accepted");
    auto self=doc.Definition().layers[0].transitions[0];self.reenter=false;
    unchanged=doc;Need(!AnimatorControllerEdits(doc).RemoveState(0,1,0,error) && doc.HasSameState(unchanged),"Retarget created implicit self-transition");
    self.reenter=true;Need(AnimatorControllerEdits(doc).ReplaceTransition(0,0,self,error) && AnimatorControllerEdits(doc).RemoveState(0,1,0,error),"Explicit reentry retarget refused");
    Need(doc.Definition().layers[0].states.size()==1 && doc.Definition().layers[0].transitions[0].to==0 && *doc.Definition().layers[0].transitions[0].from==0,"State retarget indices corrupt");
    Need(doc.Undo(error) && doc.Definition().layers[0].states.size()==2 && doc.Redo(error) && doc.Definition().layers[0].states.size()==1,"Structural Undo/Redo failed");
    unchanged=doc;Need(!AnimatorControllerEdits(doc).RemoveState(0,0,{},error) && !AnimatorControllerEdits(doc).RemoveLayer(0,error) && doc.HasSameState(unchanged),"Last state/layer removed");
    auto overlay=doc.Definition().layers[0];overlay.name="Upper";overlay.mode="additive";overlay.weight=0.5f;overlay.mask={"Hand"};
    Need(AnimatorControllerEdits(doc).AddLayer(overlay,error),"Add layer failed");
    Transition any=self;any.from.reset();any.to=0;any.blendSeconds=0.5f;
    Need(AnimatorControllerEdits(doc).AddTransition(0,any,error) && AnimatorControllerEdits(doc).MoveTransition(0,1,0,error) && !doc.Definition().layers[0].transitions[0].from && doc.Definition().layers[0].transitions[1].from,"Priority/AnyState edit failed");
    Need(AnimatorControllerEdits(doc).RemoveTransition(0,1,error) && AnimatorControllerEdits(doc).RemoveLayer(1,error),"Remove edge/layer failed");
    const auto beforeNoop=doc.Revision();Need(AnimatorControllerEdits(doc).MoveTransition(0,0,0,error) && doc.Revision()==beforeNoop,"No-op priority created history");
    unchanged=doc;Need(!AnimatorControllerEdits(doc).SetEntry(0,99,error) && !AnimatorControllerEdits(doc).RemoveTransition(0,99,error) && !AnimatorControllerEdits(doc).AddState(99,state,error) && doc.HasSameState(unchanged),"Bad indices changed document");
    auto badParameter=doc.Definition().parameters[0];badParameter.kind=Kind::Bool;unchanged=doc;
    Need(!AnimatorControllerEdits(doc).ReplaceParameter(0,badParameter,error) && doc.HasSameState(unchanged),"Parameter kind edit invalidated conditions");
    // Each operation runs on a private typed copy before document/history commit.
    const auto sweep=[&](const char* label,auto operation){auto counted=doc;allocations=0;counting=true;const bool success=operation(counted);counting=false;Need(success,"Counted structural operation failed");const auto total=allocations;
        for(size_t i=0;i<total;++i){auto tested=doc;failAfter=static_cast<long>(i);const bool accepted=operation(tested);failAfter=-1;Need(!accepted && tested.HasSameState(doc),"Structural allocation failure published changes");}
        std::cout<<"CONTROLLER_EDIT_ALLOCATION_OPERATION="<<label<<" POSITIONS="<<total<<'\n';
    };
    sweep("add-layer",[&](auto& target){return AnimatorControllerEdits(target).AddLayer(overlay,error);});
    sweep("add-state",[&](auto& target){return AnimatorControllerEdits(target).AddState(0,state,error);});
    sweep("add-parameter",[&](auto& target){return AnimatorControllerEdits(target).AddParameter({"new",Kind::Trigger,0,false},error);});
    sweep("add-edge",[&](auto& target){return AnimatorControllerEdits(target).AddTransition(0,any,error);});
    Need(AnimatorControllerEdits(doc).AddState(0,{"Else","default",1,true},error) && AnimatorControllerEdits(doc).AddParameter({"safe",Kind::Float,0,false},error),"Removal sweep setup failed");
    auto retargeted=doc;Need(AnimatorControllerEdits(retargeted).RemoveState(0,0,1,error) && retargeted.Definition().layers[0].entry==0 && retargeted.Definition().layers[0].states[0].name=="Else" && retargeted.Definition().layers[0].transitions[0].to==0,"Entry/target replacement compaction failed");
    Need(AnimatorControllerEdits(retargeted).RemoveParameter(0,2,error) && retargeted.Definition().layers[0].transitions[0].conditions[0].parameter==1 && retargeted.Definition().parameters[1].name=="safe","Higher-index parameter replacement compaction failed");
    sweep("remove-entry-state",[&](auto& target){return AnimatorControllerEdits(target).RemoveState(0,0,1,error);});
    sweep("remove-parameter",[&](auto& target){return AnimatorControllerEdits(target).RemoveParameter(0,2,error);});
    auto layerChanged=doc.Definition().layers[0];layerChanged.weight=0.4f;
    sweep("replace-layer",[&](auto& target){return AnimatorControllerEdits(target).ReplaceLayer(0,layerChanged,error);});
    sweep("remove-edge",[&](auto& target){return AnimatorControllerEdits(target).RemoveTransition(0,0,error);});
    std::cout<<"PASS: Controller structural edit private candidate/stale UI revision/state+parameter reference compaction/explicit retarget/last-item+kind+self refusal/rename/priority/AnyState/Undo+Redo/allocation isolation (no node GUI or file write)\n";return 0;
}catch(const std::exception& exception){counting=false;failAfter=-1;std::cerr<<exception.what()<<'\n';return 1;}}
