#include "engine/editor/ControllerDetailSelection.h"
#include "engine/editor/AnimatorControllerEdits.h"
#include <iostream>
static void Need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(){try{
    using namespace Engine::Editor;using namespace Engine::Editor::ControllerDetails;using namespace Engine::AnimationSystem::Controller;
    Graph graph;graph.parameters={{"Unused",Kind::Float,0,false},{"Middle",Kind::Float,1,false},{"Last",Kind::Float,2,false}};
    Layer base;base.name="Base";base.mode="override";base.states={{"Idle","default",1,true}};Transition edge;edge.to=0;edge.blendSeconds=.1f;base.transitions.push_back(edge);edge.blendSeconds=.2f;base.transitions.push_back(edge);graph.layers.push_back(base);
    Layer overlay=base;overlay.name="Overlay";graph.layers.push_back(overlay);std::string text,error;Need(AnimatorControllerSerialization::Encode(graph,text,error),error.c_str());AnimatorControllerDocument doc;Need(doc.Load(text,error),error.c_str());
    StableSelection parameter;parameter.Pick(doc.AuthoringIds().parameters,1);const auto parameterId=parameter.Identity();
    Need(AnimatorControllerEdits(doc).RemoveParameter(0,std::nullopt,error),error.c_str());Need(parameter.Resolve(doc.AuthoringIds().parameters)==0 && parameter.Identity()==parameterId,"Compaction selected a different parameter");
    Need(doc.Undo(error) && parameter.Resolve(doc.AuthoringIds().parameters)==1 && doc.Redo(error) && parameter.Resolve(doc.AuthoringIds().parameters)==0,"Parameter Undo/Redo changed surviving selection");
    auto renamed=doc.Definition().parameters[0];renamed.name="Renamed###日本語";Need(AnimatorControllerEdits(doc).ReplaceParameter(0,renamed,error) && parameter.Resolve(doc.AuthoringIds().parameters)==0 && parameter.Identity()==parameterId,"Rename lost identity");
    auto ids=doc.AuthoringIds();TransitionView transitions;auto& selected=transitions.For(ids.layers[0]);selected.Pick(ids.layers[0].transitions,0);const auto selectedId=selected.Identity();
    transitions.For(ids.layers[1]).Pick(ids.layers[1].transitions,1);Need(transitions.For(ids.layers[0]).Identity()==selectedId && transitions.For(ids.layers[1]).Resolve(ids.layers[1].transitions)==1,"Layer switch transferred/forgot selection");
    Need(AnimatorControllerEdits(doc).MoveTransition(0,0,1,error),error.c_str());Need(transitions.For(doc.AuthoringIds().layers[0]).Resolve(doc.AuthoringIds().layers[0].transitions)==1 && transitions.For(doc.AuthoringIds().layers[0]).Identity()==selectedId,"Priority move lost selected edge");
    Need(doc.Undo(error) && transitions.For(doc.AuthoringIds().layers[0]).Resolve(doc.AuthoringIds().layers[0].transitions)==0 && doc.Redo(error) && transitions.For(doc.AuthoringIds().layers[0]).Resolve(doc.AuthoringIds().layers[0].transitions)==1,"Priority history changed selected identity");
    Need(AnimatorControllerEdits(doc).RemoveTransition(0,0,error),error.c_str());Need(transitions.For(doc.AuthoringIds().layers[0]).Resolve(doc.AuthoringIds().layers[0].transitions)==0 && transitions.For(doc.AuthoringIds().layers[0]).Identity()==selectedId,"Edge compaction transferred selection");
    Need(AnimatorControllerEdits(doc).RemoveLayer(0,error),error.c_str());Need(transitions.For(doc.AuthoringIds().layers[0]).Resolve(doc.AuthoringIds().layers[0].transitions)==1,"Layer compaction transferred edge selection");
    Need(doc.Undo(error),error.c_str());Need(transitions.For(doc.AuthoringIds().layers[0]).Identity()==selectedId,"Layer Undo lost retained selection");
    Need(AnimatorControllerEdits(doc).RemoveParameter(0,std::nullopt,error),error.c_str());Need(parameter.Resolve(doc.AuthoringIds().parameters)==0 && parameter.Identity()!=parameterId,"Deleted selection retained an obsolete index");
    Need(AnimatorControllerEdits(doc).RemoveParameter(0,std::nullopt,error),error.c_str());Need(parameter.Resolve(doc.AuthoringIds().parameters)==0 && !parameter.Identity(),"Empty parameter selection retained ID");
    Need(AnimatorControllerEdits(doc).RemoveTransition(0,0,error),error.c_str());Need(!transitions.For(doc.AuthoringIds().layers[0]).Identity(),"Empty transition selection retained ID");
    auto generic=doc.Definition();generic.layers[1].weight=.5f;const auto oldLayer=doc.AuthoringIds().layers[1].id;Need(doc.Apply(generic,error),error.c_str());Need(doc.AuthoringIds().layers[1].id!=oldLayer && transitions.For(doc.AuthoringIds().layers[1]).Resolve(doc.AuthoringIds().layers[1].transitions)==0,"Unknown replacement guessed identity");
    StableSelection wide;std::vector<uint64_t> wideIds{1,AnimatorControllerAuthoringIds::MaximumId};wide.Pick(wideIds,1);Need(wide.Resolve(wideIds)==1 && wide.Identity()==wideIds[1],"Selection truncated 48-bit ID");bool refused=false;try{wide.Pick(wideIds,2);}catch(const std::out_of_range&){refused=true;}Need(refused && wide.Identity()==wideIds[1],"Invalid Pick mutated selection");
    AnimatorControllerAuthoringIds::Layer retained;retained.id=10000;retained.transitions={10001,10002};TransitionView bounded;bounded.For(retained).Pick(retained.transitions,1);
    for(uint64_t i=1;i<=64;++i){auto other=retained;other.id+=i*3;other.transitions={other.id+1,other.id+2};bounded.For(other).Pick(other.transitions,1);}
    Need(bounded.For(retained).Resolve(retained.transitions)==0,"Evicted layer guessed old compacted index");
    const auto original=doc;transitions.For(doc.AuthoringIds().layers[1]);Need(doc.HasSameState(original),"Selection read modified authoring graph/history");
    std::cout<<"PASS: stable parameter/transition selection across compaction/rename/priority/Undo+Redo/layer switch+removal, missing+empty fallback, unknown replacement, 48-bit IDs, bounded per-layer retention and read-only authoring isolation\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
