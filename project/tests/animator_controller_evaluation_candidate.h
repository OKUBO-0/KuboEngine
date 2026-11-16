#pragma once
#include "animator_controller_candidate.h"
// Private decision experiment: no clock, asset owner, pose or production binding.
namespace AnimatorControllerCandidate {
struct Instance {
    std::vector<Parameter> values;
    std::vector<size_t> active;
    explicit Instance(const Graph& graph):values(graph.parameters) {
        for(const auto& layer:graph.layers)active.push_back(layer.entry);
    }
};
class Evaluation final {
    static bool ConditionMatches(const Condition& c,const Parameter& p) noexcept {
        if(p.kind==Kind::Trigger)return c.op=="set" && c.flag && p.flag;
        if(p.kind==Kind::Bool)return c.op=="eq" ? p.flag==c.flag : c.op=="ne" && p.flag!=c.flag;
        if(!std::isfinite(p.number))return false;
        if(c.op=="eq")return p.number==c.number;
        if(c.op=="ne")return p.number!=c.number;
        if(c.op=="lt")return p.number<c.number;
        if(c.op=="le")return p.number<=c.number;
        if(c.op=="gt")return p.number>c.number;
        return c.op=="ge" && p.number>=c.number;
    }
public:
    // Caller supplies the REAL owner's unwrapped normalized interval. No delta
    // prediction here. First authored match wins; one transition per call.
    static std::optional<size_t> Select(const Graph& graph,const Instance& instance,
        size_t layerIndex,double previous,double current) noexcept {
        if(!std::isfinite(previous) || !std::isfinite(current) || previous<0 || current<previous ||
           current>1000000000.0 || graph.layers.size()!=instance.active.size() ||
           graph.parameters.size()!=instance.values.size() || layerIndex>=graph.layers.size())return {};
        for(size_t i=0;i<graph.parameters.size();++i)
            if(graph.parameters[i].kind!=instance.values[i].kind || graph.parameters[i].name!=instance.values[i].name)return {};
        const auto& layer=graph.layers[layerIndex];const auto active=instance.active[layerIndex];
        if(active>=layer.states.size())return {};
        for(size_t i=0;i<layer.transitions.size();++i) {
            const auto& t=layer.transitions[i];
            if(t.to>=layer.states.size() || (t.from && *t.from!=active) || (t.to==active && !t.reenter))continue;
            if(t.exitTime) {
                if(current==previous)continue;
                const double threshold=*t.exitTime;
                double boundary=threshold;
                if(layer.states[active].loop) {
                    boundary=std::floor(previous)+threshold;
                    if(boundary<previous)boundary+=1;
                }
                if(boundary<previous || boundary>current)continue;
            }
            bool matches=true;
            for(const auto& c:t.conditions) {
                if(c.parameter>=instance.values.size() || !ConditionMatches(c,instance.values[c.parameter])){matches=false;break;}
            }
            if(matches)return i;
        }
        return {};
    }
    // Admission must be supplied by the future real clip/owner/queue boundary.
    // Re-evaluate before a non-allocating commit; refusal consumes no trigger.
    static bool Commit(const Graph& graph,Instance& instance,size_t layerIndex,
        size_t selected,double previous,double current,bool admitted) noexcept {
        if(!admitted)return false;
        const auto now=Select(graph,instance,layerIndex,previous,current);
        if(!now || *now!=selected)return false;
        const auto& transition=graph.layers[layerIndex].transitions[selected];
        instance.active[layerIndex]=transition.to;
        for(const auto& c:transition.conditions)
            if(instance.values[c.parameter].kind==Kind::Trigger)instance.values[c.parameter].flag=false;
        return true;
    }
};
}
