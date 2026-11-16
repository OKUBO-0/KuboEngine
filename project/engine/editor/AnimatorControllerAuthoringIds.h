#pragma once
#include "engine/animation/AnimatorControllerGraph.h"
#include <cstdint>

namespace Engine::Editor {
// Editor-only identities. Runtime graph schema/references remain unchanged.
// A document owns the monotonically increasing counter; Undo does not rewind it.
struct AnimatorControllerAuthoringIds {
    using Id=uint64_t;
    struct Layer {
        Id id=0;
        std::vector<Id> states,transitions;
        bool operator==(const Layer&) const = default;
    };
    std::vector<Id> parameters;
    std::vector<Layer> layers;
    static constexpr Id MaximumId=(Id{1}<<48)-1;
    bool operator==(const AnimatorControllerAuthoringIds&) const = default;
    void Swap(AnimatorControllerAuthoringIds& other) noexcept{parameters.swap(other.parameters);layers.swap(other.layers);}
    size_t Bytes() const noexcept {
        size_t bytes=sizeof(*this)+parameters.size()*sizeof(Id)+layers.size()*sizeof(Layer);
        for(const auto& layer:layers)bytes+=(layer.states.size()+layer.transitions.size())*sizeof(Id);
        return bytes;
    }
    static Id Take(Id& next){if(!next || next>MaximumId)throw std::overflow_error("Controller authoring identity exhausted");return next++;}
    static Layer NewLayer(const AnimationSystem::Controller::Layer& layer,Id& next,Id retained=0){
        if(layer.states.empty() || layer.states.size()>64 || layer.transitions.size()>256)throw std::invalid_argument("Controller identity layer bounds");
        Layer result;result.id=retained?retained:Take(next);
        for(size_t i=0;i<layer.states.size();++i)result.states.push_back(Take(next));
        for(size_t i=0;i<layer.transitions.size();++i)result.transitions.push_back(Take(next));
        return result;
    }
    static AnimatorControllerAuthoringIds Fresh(const AnimationSystem::Controller::Graph& graph,Id& next){
        AnimatorControllerAuthoringIds result;
        for(size_t i=0;i<graph.parameters.size();++i)result.parameters.push_back(Take(next));
        for(const auto& layer:graph.layers)result.layers.push_back(NewLayer(layer,next));
        return result;
    }
    void Validate(const AnimationSystem::Controller::Graph& graph,Id next) const {
        if(!next || next>MaximumId+1 || parameters.size()!=graph.parameters.size() || layers.size()!=graph.layers.size())
            throw std::invalid_argument("Controller authoring identity shape/counter mismatch");
        std::vector<Id> all=parameters;
        for(size_t i=0;i<layers.size();++i){
            const auto& layer=layers[i];
            if(layer.states.size()!=graph.layers[i].states.size() || layer.transitions.size()!=graph.layers[i].transitions.size())
                throw std::invalid_argument("Controller layer identity shape mismatch");
            all.push_back(layer.id);all.insert(all.end(),layer.states.begin(),layer.states.end());all.insert(all.end(),layer.transitions.begin(),layer.transitions.end());
        }
        std::sort(all.begin(),all.end());
        if((!all.empty() && (!all.front() || all.back()>=next)) || std::adjacent_find(all.begin(),all.end())!=all.end())
            throw std::invalid_argument("Controller authoring identity alias/range mismatch");
    }
};
}
