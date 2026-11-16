#pragma once
#include "AnimatorControllerDocument.h"
#include <utility>

namespace Engine::Editor {
// One UI snapshot's commands. After a real mutation, reconstruct this capability
// from the new document before processing more indices. No file/runtime writes.
class AnimatorControllerEdits final {
    using Graph=AnimationSystem::Controller::Graph;
    using Layer=AnimationSystem::Controller::Layer;
    using State=AnimationSystem::Controller::State;
    using Parameter=AnimationSystem::Controller::Parameter;
    using Transition=AnimationSystem::Controller::Transition;
    using Ids=AnimatorControllerAuthoringIds;
    AnimatorControllerDocument& document_;uint64_t revision_;
    static void Require(bool value,const char* why){if(!value)throw std::invalid_argument(why);}
    static bool SameChildren(const Layer& a,const Layer& b) {
        if(a.states.size()!=b.states.size() || a.transitions.size()!=b.transitions.size())return false;
        for(size_t i=0;i<a.states.size();++i){const auto& x=a.states[i];const auto& y=b.states[i];if(x.name!=y.name || x.clip!=y.clip || x.speed!=y.speed || x.loop!=y.loop)return false;}
        for(size_t i=0;i<a.transitions.size();++i){const auto& x=a.transitions[i];const auto& y=b.transitions[i];
            if(x.from!=y.from || x.to!=y.to || x.blendSeconds!=y.blendSeconds || x.exitTime!=y.exitTime || x.reenter!=y.reenter || x.conditions.size()!=y.conditions.size())return false;
            for(size_t j=0;j<x.conditions.size();++j){const auto& c=x.conditions[j];const auto& d=y.conditions[j];if(c.parameter!=d.parameter || c.op!=d.op || c.number!=d.number || c.flag!=d.flag)return false;}
        }return true;
    }
    template<class Change,class IdentityChange> bool Edit(Change&& change,IdentityChange&& identityChange,std::string& error) {
        try {
            if(!document_.Loaded() || document_.Revision()!=revision_){error="Controller view stale or unavailable; refresh before editing";return false;}
            auto graph=document_.Definition();auto ids=document_.ids_;auto next=document_.nextId_;
            change(graph);identityChange(ids,next);
            if(document_.Revision()!=revision_){error="Controller changed while preparing edit";return false;}
            return document_.ApplyIdentified(graph,std::move(ids),next,error);
        }catch(const std::exception& exception){error=exception.what();return false;}
    }
    template<class Change> bool Edit(Change&& change,std::string& error){return Edit(std::forward<Change>(change),[](Ids&,uint64_t&){},error);}
public:
    explicit AnimatorControllerEdits(AnimatorControllerDocument& document) noexcept:document_(document),revision_(document.Revision()){}
    bool New(std::string& error) {
        try {
            if(document_.Revision()!=revision_ || document_.IsDirty()){error="Resolve unsaved Controller changes before New";return false;}
            Graph graph;Layer layer;layer.name="Base";layer.mode="override";layer.states.push_back({"Idle","default",1,true});graph.layers.push_back(std::move(layer));
            std::string text;if(!AnimatorControllerSerialization::Encode(graph,text,error))return false;
            return document_.Load(text,error,true);
        }catch(const std::exception& exception){error=exception.what();return false;}
    }
    bool AddLayer(const Layer& layer,std::string& error){return Edit([&](Graph& graph){Require(graph.layers.size()<4,"Maximum Controller layers reached");graph.layers.push_back(layer);},[&](Ids& ids,uint64_t& next){ids.layers.push_back(Ids::NewLayer(layer,next));},error);}
    bool ReplaceLayer(size_t index,const Layer& layer,std::string& error){return Edit([&](Graph& graph){graph.layers.at(index)=layer;},[&](Ids& ids,uint64_t& next){if(!SameChildren(document_.Definition().layers.at(index),layer))ids.layers.at(index)=Ids::NewLayer(layer,next,ids.layers.at(index).id);},error);}
    bool RemoveLayer(size_t index,std::string& error){return Edit([&](Graph& graph){Require(index<graph.layers.size() && graph.layers.size()>1,"Cannot remove last/missing Controller layer");graph.layers.erase(graph.layers.begin()+index);},[&](Ids& ids,uint64_t&){ids.layers.erase(ids.layers.begin()+index);},error);}
    bool AddState(size_t layer,const State& state,std::string& error){return Edit([&](Graph& graph){auto& states=graph.layers.at(layer).states;Require(states.size()<64,"Maximum states reached");states.push_back(state);},[&](Ids& ids,uint64_t& next){ids.layers.at(layer).states.push_back(Ids::Take(next));},error);}
    bool ReplaceState(size_t layer,size_t index,const State& state,std::string& error){return Edit([&](Graph& graph){graph.layers.at(layer).states.at(index)=state;},error);}
    bool SetEntry(size_t layer,size_t state,std::string& error){return Edit([&](Graph& graph){auto& target=graph.layers.at(layer);Require(state<target.states.size(),"Entry state missing");target.entry=state;},error);}
    // References are never silently dropped. A chosen replacement retargets
    // them before index compaction; any resulting invalid self edge is refused.
    bool RemoveState(size_t layer,size_t state,std::optional<size_t> replacement,std::string& error){return Edit([&](Graph& graph){
        auto& target=graph.layers.at(layer);Require(state<target.states.size() && target.states.size()>1,"Cannot remove last/missing state");
        Require(!replacement || (*replacement<target.states.size() && *replacement!=state),"Invalid replacement state");
        bool referenced=target.entry==state;for(const auto& edge:target.transitions)referenced=referenced || edge.to==state || edge.from==state;
        Require(!referenced || replacement.has_value(),"State referenced; choose an explicit replacement first");
        if(target.entry==state)target.entry=*replacement;
        for(auto& edge:target.transitions){if(edge.to==state)edge.to=*replacement;if(edge.from==state)edge.from=*replacement;}
        target.states.erase(target.states.begin()+state);if(target.entry>state)--target.entry;
        for(auto& edge:target.transitions){if(edge.to>state)--edge.to;if(edge.from && *edge.from>state)--*edge.from;}
    },[&](Ids& ids,uint64_t&){auto& states=ids.layers.at(layer).states;states.erase(states.begin()+state);},error);}
    bool AddParameter(const Parameter& parameter,std::string& error){return Edit([&](Graph& graph){Require(graph.parameters.size()<32,"Maximum parameters reached");graph.parameters.push_back(parameter);},[&](Ids& ids,uint64_t& next){ids.parameters.push_back(Ids::Take(next));},error);}
    bool ReplaceParameter(size_t index,const Parameter& parameter,std::string& error){return Edit([&](Graph& graph){
        if(graph.parameters.at(index).kind!=parameter.kind)
            for(const auto& layer:graph.layers)for(const auto& edge:layer.transitions)for(const auto& condition:edge.conditions)
                Require(condition.parameter!=index,"Referenced parameter kind cannot change; remove or explicitly edit its conditions first");
        graph.parameters.at(index)=parameter;
    },error);}
    bool RemoveParameter(size_t index,std::optional<size_t> replacement,std::string& error){return Edit([&](Graph& graph){
        Require(index<graph.parameters.size(),"Parameter missing");
        Require(!replacement || (*replacement<graph.parameters.size() && *replacement!=index && graph.parameters[*replacement].kind==graph.parameters[index].kind),"Replacement parameter must have the same kind");
        bool referenced=false;for(const auto& layer:graph.layers)for(const auto& edge:layer.transitions)for(const auto& condition:edge.conditions)referenced=referenced || condition.parameter==index;
        Require(!referenced || replacement.has_value(),"Parameter referenced; choose an explicit same-kind replacement first");
        for(auto& layer:graph.layers)for(auto& edge:layer.transitions)for(auto& condition:edge.conditions){if(condition.parameter==index)condition.parameter=*replacement;if(condition.parameter>index)--condition.parameter;}
        graph.parameters.erase(graph.parameters.begin()+index);
    },[&](Ids& ids,uint64_t&){ids.parameters.erase(ids.parameters.begin()+index);},error);}
    bool AddTransition(size_t layer,const Transition& edge,std::string& error){return Edit([&](Graph& graph){auto& edges=graph.layers.at(layer).transitions;Require(edges.size()<256,"Maximum layer transitions reached");edges.push_back(edge);},[&](Ids& ids,uint64_t& next){ids.layers.at(layer).transitions.push_back(Ids::Take(next));},error);}
    bool ReplaceTransition(size_t layer,size_t index,const Transition& edge,std::string& error){return Edit([&](Graph& graph){graph.layers.at(layer).transitions.at(index)=edge;},error);}
    bool RemoveTransition(size_t layer,size_t index,std::string& error){return Edit([&](Graph& graph){auto& edges=graph.layers.at(layer).transitions;Require(index<edges.size(),"Transition missing");edges.erase(edges.begin()+index);},[&](Ids& ids,uint64_t&){auto& edges=ids.layers.at(layer).transitions;edges.erase(edges.begin()+index);},error);}
    bool MoveTransition(size_t layer,size_t from,size_t to,std::string& error){return Edit([&](Graph& graph){
        auto& edges=graph.layers.at(layer).transitions;Require(from<edges.size() && to<edges.size(),"Transition priority index missing");
        if(from<to)std::rotate(edges.begin()+from,edges.begin()+from+1,edges.begin()+to+1);
        else if(from>to)std::rotate(edges.begin()+to,edges.begin()+from,edges.begin()+from+1);
    },[&](Ids& ids,uint64_t&){auto& edges=ids.layers.at(layer).transitions;if(from<to)std::rotate(edges.begin()+from,edges.begin()+from+1,edges.begin()+to+1);else if(from>to)std::rotate(edges.begin()+to,edges.begin()+from,edges.begin()+from+1);},error);}
};
}
