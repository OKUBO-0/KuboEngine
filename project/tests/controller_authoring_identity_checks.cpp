#include "engine/editor/AnimatorControllerEdits.h"
#include <iostream>
#include <cstdlib>
#include <new>
static long failAfter=-1;static bool counting=false;static size_t allocations=0;
void* operator new(size_t size){if(counting)++allocations;if(failAfter==0){failAfter=-1;throw std::bad_alloc();}if(failAfter>0)--failAfter;if(auto* p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept{std::free(p);}void operator delete(void* p,size_t) noexcept{std::free(p);}
static void Need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
int main(){try{
    using namespace Engine::Editor;using namespace Engine::AnimationSystem::Controller;
    Graph graph;graph.parameters={{"a",Kind::Float,0,false},{"b",Kind::Bool,0,true}};
    Layer layer;layer.name="Base";layer.mode="override";layer.states={{"Idle","default",1,true},{"Unused","default",1,true},{"Run","default",2,true}};
    Transition edge;edge.to=2;layer.transitions.push_back(edge);edge.from=0;layer.transitions.push_back(edge);graph.layers.push_back(layer);
    std::string text,error;Need(AnimatorControllerSerialization::Encode(graph,text,error),error.c_str());
    AnimatorControllerDocument doc;Need(doc.Load(text,error),error.c_str());const auto initial=doc.AuthoringIds();
    auto state=doc.Definition().layers[0].states[1];state.name="Renamed ### 日本語";
    Need(AnimatorControllerEdits(doc).ReplaceState(0,1,state,error) && doc.AuthoringIds()==initial,"Typed rename changed identity");
    Need(doc.HistoryBytes()>=text.size()+doc.Text().size()+initial.Bytes()*2,"Identity snapshots escaped logical history budget");
    Need(doc.Undo(error) && doc.AuthoringIds()==initial && doc.Text()==text && doc.Redo(error) && doc.AuthoringIds()==initial,"Rename Undo/Redo identity changed");
    Need(AnimatorControllerEdits(doc).RemoveState(0,1,std::nullopt,error),error.c_str());
    Need(doc.AuthoringIds().layers[0].states==std::vector<uint64_t>{initial.layers[0].states[0],initial.layers[0].states[2]} && doc.AuthoringIds().layers[0].transitions==initial.layers[0].transitions,"Deletion compacted identity instead of indices");
    Need(doc.Undo(error) && doc.AuthoringIds()==initial,"Undo deletion did not restore identity");
    State added{"First branch","default",1,true};Need(AnimatorControllerEdits(doc).AddState(0,added,error),error.c_str());const auto retired=doc.AuthoringIds().layers[0].states.back();
    Need(doc.Undo(error),error.c_str());added.name="New branch";Need(AnimatorControllerEdits(doc).AddState(0,added,error) && doc.AuthoringIds().layers[0].states.back()>retired && doc.RedoCount()==0,"Undo rewound identity counter/reused abandoned ID");
    const auto beforeMove=doc.AuthoringIds();Need(AnimatorControllerEdits(doc).MoveTransition(0,0,1,error),error.c_str());
    Need(doc.AuthoringIds().layers[0].transitions[1]==beforeMove.layers[0].transitions[0] && doc.AuthoringIds().layers[0].transitions[0]==beforeMove.layers[0].transitions[1],"Priority movement did not carry edge ID");
    Need(doc.Undo(error) && doc.AuthoringIds()==beforeMove && doc.Redo(error),"Priority history lost IDs");
    const auto beforeParameter=doc.AuthoringIds();Need(AnimatorControllerEdits(doc).RemoveParameter(0,std::nullopt,error) && doc.AuthoringIds().parameters==std::vector<uint64_t>{beforeParameter.parameters[1]},"Parameter compaction changed survivor ID");
    auto changed=doc.Definition().layers[0];changed.weight=.5f;changed.name="Layer renamed";const auto beforeProperties=doc.AuthoringIds();
    Need(AnimatorControllerEdits(doc).ReplaceLayer(0,changed,error) && doc.AuthoringIds()==beforeProperties,"Layer properties retired child identities");
    changed.states[0].name="Whole layer replacement";Need(AnimatorControllerEdits(doc).ReplaceLayer(0,changed,error) && doc.AuthoringIds().layers[0].id==beforeProperties.layers[0].id && doc.AuthoringIds().layers[0].states[0]!=beforeProperties.layers[0].states[0],"Unknown layer children silently reused identities");
    const auto beforeLayer=doc.AuthoringIds();Layer extra;extra.name="Extra";extra.mode="additive";extra.states={{"Overlay","default",1,true}};
    Need(AnimatorControllerEdits(doc).AddLayer(extra,error),error.c_str());const auto overlay=doc.AuthoringIds().layers[1];
    Need(AnimatorControllerEdits(doc).RemoveLayer(0,error) && doc.AuthoringIds().layers[0]==overlay && doc.Undo(error) && doc.AuthoringIds().layers[0]==beforeLayer.layers[0],"Layer removal/Undo lost survivor identity");
    const auto held=doc;Need(!AnimatorControllerEdits(doc).SetEntry(0,999,error) && doc.HasSameState(held),"Rejected edit changed identity/history/counter");
    auto alias=doc.AuthoringIds();alias.layers[0].states[0]=alias.layers[0].id;bool rejected=false;try{alias.Validate(doc.Definition(),AnimatorControllerAuthoringIds::MaximumId+1);}catch(const std::exception&){rejected=true;}Need(rejected,"Aliased identity accepted");
    auto next=AnimatorControllerAuthoringIds::MaximumId;Need(AnimatorControllerAuthoringIds::Take(next)==AnimatorControllerAuthoringIds::MaximumId,"Last identity unavailable");const auto exhausted=next;rejected=false;try{AnimatorControllerAuthoringIds::Take(next);}catch(const std::overflow_error&){rejected=true;}Need(rejected && next==exhausted,"Identity overflow wrapped/mutated counter");
    const auto beforeGeneric=doc.AuthoringIds();auto generic=doc.Definition();generic.layers[0].weight=.75f;Need(doc.Apply(generic,error) && doc.AuthoringIds().layers[0].id!=beforeGeneric.layers[0].id,"Unknown whole graph inferred identity");
    const auto unchanged=doc;Need(doc.Apply(doc.Definition(),error) && doc.HasSameState(unchanged),"No-op recreated identities/changed history");
    Graph decoded;Need(TextCodec::Decode(doc.Text(),decoded,error) && doc.Text().find("authoring")==std::string::npos,"Editor IDs leaked into runtime schema");
    added.name="Allocation guarded";auto observed=doc;allocations=0;counting=true;const bool accepted=AnimatorControllerEdits(observed).AddState(0,added,error);counting=false;Need(accepted,"Counted identity edit failed");const auto positions=allocations;
    for(size_t i=0;i<positions;++i){auto tested=doc;failAfter=static_cast<long>(i);const bool result=AnimatorControllerEdits(tested).AddState(0,added,error);failAfter=-1;Need(!result && tested.HasSameState(doc),"Allocation failure published graph/identity/history/counter");}
    std::cout<<"AUTHORING_ID_ALLOCATION_POSITIONS="<<positions<<'\n';
    std::cout<<"PASS: editor-only stable typed IDs/rename/property preservation/structural survivor compaction/priority movement/Undo+Redo/no branch reuse/conservative unknown graph+layer replacement/identity byte budget/alias+overflow refusal/allocation isolation (no canvas/layout persistence)\n";return 0;
}catch(const std::exception& exception){failAfter=-1;std::cerr<<exception.what()<<'\n';return 1;}}
