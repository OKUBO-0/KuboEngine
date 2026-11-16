#pragma once
#include "AnimatorControllerText.h"
#include <memory>
namespace Engine::AnimationSystem::Controller {
// Immutable owned graph + per-instance parameters/state. The render owner supplies
// its actual unwrapped interval; this class never predicts or advances a clock.
class Runtime final {
    std::shared_ptr<const Graph> graph_;
    std::vector<Parameter> values_;
    std::vector<size_t> active_;
    bool admitting_=false;
    explicit Runtime(Graph graph):graph_(std::make_shared<const Graph>(std::move(graph))),values_(graph_->parameters) {
        for(const auto& layer:graph_->layers)active_.push_back(layer.entry);
    }
    Parameter* Find(std::string_view name) noexcept {
        if(admitting_)return nullptr;
        for(auto& value:values_)if(value.name==name)return &value;return nullptr;
    }
    static bool Matches(const Condition& c,const Parameter& p) noexcept {
        if(p.kind==Kind::Trigger)return p.flag;
        if(p.kind==Kind::Bool)return c.op=="eq" ? p.flag==c.flag : p.flag!=c.flag;
        if(c.op=="eq")return p.number==c.number;if(c.op=="ne")return p.number!=c.number;
        if(c.op=="lt")return p.number<c.number;if(c.op=="le")return p.number<=c.number;
        if(c.op=="gt")return p.number>c.number;return p.number>=c.number;
    }
    std::optional<size_t> Select(size_t layer,double previous,double current) const noexcept {
        if(layer>=active_.size() || !std::isfinite(previous) || !std::isfinite(current) ||
           previous<0 || current<previous || current>1e9)return {};
        const auto& definition=graph_->layers[layer];const auto active=active_[layer];
        for(size_t i=0;i<definition.transitions.size();++i) {
            const auto& transition=definition.transitions[i];
            if((transition.from && *transition.from!=active) || (transition.to==active && !transition.reenter))continue;
            if(transition.exitTime) {
                if(current==previous)continue;
                double boundary=*transition.exitTime;
                if(definition.states[active].loop){boundary+=std::floor(previous);if(boundary<previous)boundary+=1;}
                if(boundary<previous || boundary>current)continue;
            }
            bool matches=true;for(const auto& c:transition.conditions)if(!Matches(c,values_[c.parameter])){matches=false;break;}
            if(matches)return i;
        }
        return {};
    }
public:
    static std::unique_ptr<Runtime> FromText(std::string_view text) {
        Graph graph;std::string error;if(!TextCodec::Decode(text,graph,error))return {};
        return std::unique_ptr<Runtime>(new Runtime(std::move(graph)));
    }
    const Graph& Definition() const noexcept {return *graph_;}
    std::unique_ptr<Runtime> Clone() const {
        if(admitting_)return {};
        return std::unique_ptr<Runtime>(new Runtime(*this));
    }
    const State* ActiveState(size_t layer=0) const noexcept {
        return layer<active_.size()?&graph_->layers[layer].states[active_[layer]]:nullptr;
    }
    bool SetFloat(std::string_view name,double value) noexcept {
        auto* p=Find(name);if(!p || p->kind!=Kind::Float || !std::isfinite(value) || std::abs(value)>(std::numeric_limits<float>::max)())return false;
        p->number=value;return true;
    }
    bool SetBool(std::string_view name,bool value) noexcept {
        auto* p=Find(name);if(!p || p->kind!=Kind::Bool)return false;p->flag=value;return true;
    }
    bool SetTrigger(std::string_view name) noexcept {
        auto* p=Find(name);if(!p || p->kind!=Kind::Trigger)return false;p->flag=true;return true;
    }
    // Explicit reentry, independent of authored conditions/exit gates. Parameters,
    // including pending triggers, remain unchanged. The owner admits real poses.
    template<class Admission> bool ForceState(size_t layer,std::string_view name,float blendSeconds,Admission&& admit) {
        if(admitting_ || layer>=active_.size() || !Engine::Scripting::ValidScriptText(name,128,true) ||
           !std::isfinite(blendSeconds) || blendSeconds<0 || blendSeconds>60)return false;
        const auto& states=graph_->layers[layer].states;
        size_t target=states.size();for(size_t i=0;i<states.size();++i)if(states[i].name==name){target=i;break;}
        if(target==states.size())return false;
        Transition transition;transition.from=active_[layer];transition.to=target;
        transition.blendSeconds=blendSeconds;transition.reenter=true;
        struct Guard {bool& flag;explicit Guard(bool& f):flag(f){flag=true;}~Guard(){flag=false;}} guard(admitting_);
        if(!admit(states[target],transition))return false;
        active_[layer]=target;return true;
    }
    template<class Admission> bool Advance(size_t layer,double previous,double current,Admission&& admit) {
        if(admitting_)return false;const auto selected=Select(layer,previous,current);if(!selected)return false;
        const auto& transition=graph_->layers[layer].transitions[*selected];
        struct Guard {bool& flag;explicit Guard(bool& f):flag(f){flag=true;}~Guard(){flag=false;}} guard(admitting_);
        // Admission may reject/throw. Reentrant writes to this runtime are refused.
        if(!admit(graph_->layers[layer].states[transition.to],transition))return false;
        active_[layer]=transition.to;
        for(const auto& c:transition.conditions)if(values_[c.parameter].kind==Kind::Trigger)values_[c.parameter].flag=false;
        return true; // one authored-priority transition, no internal cycle iteration
    }
};
}
