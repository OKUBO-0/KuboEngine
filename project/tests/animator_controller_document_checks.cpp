#include "engine/editor/AnimatorControllerDocument.h"
#include <iostream>
#include <cstdlib>
#include <new>
static long failAfter=-1;static bool count=false;static size_t allocations=0;
void* operator new(size_t size){if(count)++allocations;if(failAfter==0){failAfter=-1;throw std::bad_alloc();}if(failAfter>0)--failAfter;if(auto* p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept{std::free(p);}void operator delete(void* p,size_t) noexcept{std::free(p);}
static void Need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
int main(){try {
    using namespace Engine::Editor;using namespace Engine::AnimationSystem::Controller;
    const char* source=R"({"format":"kubo-animator-controller-1","parameters":[{"name":"speed","type":"float","default":0},{"name":"go","type":"trigger","default":false}],"layers":[{"name":"Base","mode":"override","weight":1,"mask":[],"entry":"Idle","states":[{"name":"Idle","clip":"default","speed":1,"loop":true},{"name":"Run","clip":"default","speed":2,"loop":true}],"transitions":[{"from":null,"to":"Run","blendSeconds":0.2,"exitTime":0.75,"reenter":true,"conditions":[{"parameter":"speed","op":"gt","value":0.5},{"parameter":"go","op":"set","value":true}]}]}]})";
    AnimatorControllerDocument doc;std::string error;
    Need(doc.Load(source,error) && !doc.IsDirty() && doc.UndoCount()==0,"Document load failed");
    const auto original=doc.Text();auto graph=doc.Definition();graph.layers[0].states[1].name="走る###state";
    Need(doc.Apply(graph,error) && doc.IsDirty() && doc.UndoCount()==1,"Rename authoring failed");
    Graph round;Need(TextCodec::Decode(doc.Text(),round,error) && round.layers[0].transitions[0].to==1 && round.layers[0].states[1].name=="走る###state" && !round.layers[0].transitions[0].from && *round.layers[0].transitions[0].exitTime==0.75f,"Typed references/AnyState/UTF did not roundtrip");
    AnimatorControllerDocument::PreparedSave save;Need(doc.PrepareSave(save,error) && doc.CommitSaved(save) && !doc.IsDirty() && !doc.CommitSaved(save),"Prepared baseline receipt commit failed");
    Need(doc.Undo(error) && doc.Text()==original && doc.IsDirty() && doc.Redo(error) && !doc.IsDirty(),"Save baseline Undo/Redo failed");
    Need(doc.PrepareSave(save,error),"Save receipt preparation failed");const auto saved=doc.Text();
    graph=doc.Definition();graph.layers[0].weight=0.5f;Need(doc.Apply(graph,error) && !doc.CommitSaved(save),"Stale save receipt cleared edits");
    Need(doc.Undo(error) && doc.Text()==saved && !doc.CommitSaved(save),"Undo ABA reused an old receipt");
    const auto redoCount=doc.RedoCount(),rev=doc.Revision();Need(doc.Apply(doc.Definition(),error) && doc.RedoCount()==redoCount && doc.Revision()==rev,"No-op destroyed redo/epoch");
    graph=doc.Definition();graph.layers[0].states[0].speed=3;Need(doc.Apply(graph,error) && doc.RedoCount()==0,"Real branching edit retained redo");
    const auto held=doc.Text();const auto heldRevision=doc.Revision(),heldUndo=doc.UndoCount();
    const auto reject=[&](const Graph& candidate){std::string output="retained";Need(!AnimatorControllerSerialization::Encode(candidate,output,error) && output=="retained","Invalid encoder replaced output");Need(!doc.Apply(candidate,error) && doc.Text()==held && doc.Revision()==heldRevision && doc.UndoCount()==heldUndo,"Invalid edit changed live document");};
    auto bad=graph;bad.layers[0].entry=99;reject(bad);bad=graph;bad.layers[0].transitions[0].to=99;reject(bad);
    bad=graph;bad.layers[0].transitions[0].from=99;reject(bad);bad=graph;bad.layers[0].transitions[0].conditions[0].parameter=99;reject(bad);
    bad=graph;bad.parameters[0].kind=static_cast<Kind>(99);reject(bad);bad=graph;bad.parameters[1].flag=true;reject(bad);
    bad=graph;bad.layers[0].states[1].name=bad.layers[0].states[0].name;reject(bad);bad=graph;bad.layers[0].states[1].name=std::string(129,'x');reject(bad);
    bad=graph;bad.layers[0].states[1].name=std::string("\xff",1);reject(bad);bad=graph;bad.layers[0].states[1].clip=std::string(257,'x');reject(bad);
    bad=graph;bad.layers[0].weight=std::numeric_limits<float>::quiet_NaN();reject(bad);
    bad=graph;bad.layers[0].mask={"Hand","Hand"};reject(bad);
    bad=graph;bad.layers[0].states.clear();reject(bad);bad=graph;bad.layers.assign(5,graph.layers[0]);reject(bad);
    bad=graph;bad.layers[0].transitions[0].conditions[0].op="invalid";reject(bad);
    auto layered=graph;layered.layers.assign(4,graph.layers[0]);
    for(size_t i=0;i<4;++i){layered.layers[i].name="Layer"+std::to_string(i);layered.layers[i].mode=i%2?"additive":"override";layered.layers[i].mask={"手###bone"};}
    std::string layerText;Need(AnimatorControllerSerialization::Encode(layered,layerText,error) && TextCodec::Decode(layerText,round,error) && round.layers.size()==4 && round.layers[1].mode=="additive" && round.layers[1].mask[0]=="手###bone","Four-layer typed authoring roundtrip failed");
    auto precise=graph;precise.parameters[0].number=std::nextafter(0.5,1.0);precise.layers[0].transitions[0].conditions[0].number=std::nextafter(0.5,0.0);
    precise.layers[0].states[0].clip=std::string(256,'x');
    precise.parameters.push_back({"ready",Kind::Bool,0,true});
    Need(AnimatorControllerSerialization::Encode(precise,layerText,error) && TextCodec::Decode(layerText,round,error) && round.parameters[0].number==precise.parameters[0].number && round.layers[0].transitions[0].conditions[0].number==precise.layers[0].transitions[0].conditions[0].number && round.layers[0].states[0].clip.size()==256 && round.parameters[2].flag,"Typed numeric/bool/clip256 authoring precision lost");
    bad=graph;bad.layers.assign(2,graph.layers[0]);for(size_t i=0;i<2;++i){auto& layer=bad.layers[i];layer.name="NodeBudget"+std::to_string(i);const auto edgeTemplate=layer.transitions[0];layer.transitions.assign(256,edgeTemplate);for(auto& edge:layer.transitions){const auto conditionTemplate=edge.conditions[0];edge.conditions.assign(8,conditionTemplate);}}reject(bad);
    bad=layered;for(auto& layer:bad.layers){layer.mask.clear();for(int i=0;i<256;++i)layer.mask.push_back(std::string(120,'\1')+std::to_string(i));}reject(bad);
    Need(!doc.Load("{}",error) && doc.Text()==held && doc.UndoCount()==heldUndo,"Failed load cleared draft/history");
    AnimatorControllerDocument budget(2);Need(budget.Load(source,error),"Budget load failed");
    for(int i=0;i<4;++i){auto edit=budget.Definition();edit.layers[0].weight=float(i+1)/10;Need(budget.Apply(edit,error),"Budget edit failed");}
    Need(budget.UndoCount()==2 && budget.HistoryBytes()<=AnimatorControllerDocument::MaximumHistoryBytes && budget.Undo(error) && budget.Undo(error) && !budget.Undo(error),"Bounded history eviction failed");
    AnimatorControllerDocument tiny(2,1);Need(tiny.Load(source,error) && !tiny.Apply(graph,error) && !tiny.IsDirty() && tiny.UndoCount()==0,"Over-budget command was partially committed");
    AnimatorControllerDocument fresh;Need(fresh.Load(source,error,true) && fresh.IsUntitled() && fresh.IsDirty() && fresh.PrepareSave(save,error),"Untitled draft failed");
    Need(!doc.CommitSaved(save) && fresh.CommitSaved(save) && !fresh.IsUntitled() && !fresh.IsDirty(),"Foreign receipt/untitled save failed");
    // Sweep every ordinary allocation in a real edit, keeping live graph/text/
    // history/saved baseline unchanged on failure.
    auto edit=doc.Definition();edit.layers[0].weight=0.33f;
    auto observed=doc;allocations=0;count=true;const bool applied=observed.Apply(edit,error);count=false;Need(applied,"Counted edit failed");const auto positions=allocations;
    for(size_t i=0;i<positions;++i){auto tested=doc;failAfter=static_cast<long>(i);const bool accepted=tested.Apply(edit,error);failAfter=-1;
        Need(!accepted && tested.HasSameState(doc),"Allocation failure published partial document");}
    const auto sweep=[&](const char* operation,auto action){auto measured=doc;allocations=0;count=true;const bool success=action(measured);count=false;Need(success,"Counted operation failed");const auto total=allocations;
        for(size_t i=0;i<total;++i){auto tested=doc;failAfter=static_cast<long>(i);const bool accepted=action(tested);failAfter=-1;Need(!accepted && tested.HasSameState(doc),"Load/history allocation failure changed document");}
        std::cout<<"CONTROLLER_DOCUMENT_ALLOCATION_OPERATION="<<operation<<" POSITIONS="<<total<<'\n';
    };
    sweep("load",[&](auto& tested){return tested.Load(source,error);});
    sweep("undo",[&](auto& tested){return tested.Undo(error);});
    Need(doc.Undo(error),"Redo allocation setup failed");sweep("redo",[&](auto& tested){return tested.Redo(error);});
    AnimatorControllerDocument::PreparedSave retained;Need(doc.PrepareSave(retained,error),"Receipt allocation setup failed");const auto receiptBytes=retained.Text();
    failAfter=0;const bool prepared=doc.PrepareSave(retained,error);failAfter=-1;
    Need(!prepared && retained.Text()==receiptBytes && doc.CommitSaved(retained),"Failed save preparation destroyed an earlier receipt");
    std::cout<<"CONTROLLER_DOCUMENT_EDIT_ALLOCATION_POSITIONS="<<positions<<'\n';
    std::cout<<"PASS: Controller authoring typed encoder/reference+UTF+schema bounds/private edit/Undo+Redo/dirty/save receipts/no-op+branch/history budget/allocation isolation (no file or GUI publication)\n";return 0;
}catch(const std::exception& exception){count=false;failAfter=-1;std::cerr<<exception.what()<<'\n';return 1;}}
