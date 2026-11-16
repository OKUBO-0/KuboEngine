#pragma once
#include "AnimatorControllerAuthoringIds.h"
#include <cmath>
#include <optional>

namespace Engine::Editor {
// Presentation state is scoped to one open document, never serialized as runtime data.
class AnimatorControllerCanvasState final {
public:
    using Id=AnimatorControllerAuthoringIds::Id;
    enum class Kind:Id {AnyNode=1,StateNode,Input,Output,AnyOutput,Link};
    struct Point {float x=0,y=0;bool operator==(const Point&) const = default;};
    static constexpr size_t MaximumPositions=32768;
    bool operator==(const AnimatorControllerCanvasState&) const = default;
    static Point Pixel(Point value) noexcept{return {std::trunc(value.x),std::trunc(value.y)};}
    size_t Bytes() const noexcept{return sizeof(*this)+positions_.size()*sizeof(decltype(positions_)::value_type);}
    void ApplyGeometry(const AnimatorControllerCanvasState& geometry){auto positions=geometry.positions_;positions_.swap(positions);}
    bool SameGeometry(const AnimatorControllerCanvasState& other,const AnimatorControllerAuthoringIds& ids) const {
        for(const auto& layer:ids.layers){const auto any=Key(Kind::AnyNode,layer.id);if(Pixel(Position(any,{}))!=Pixel(other.Position(any,{})))return false;
            for(size_t i=0;i<layer.states.size();++i){const auto node=Key(Kind::StateNode,layer.states[i]);const Point fallback{180.f+float(i%4)*210,float(i/4)*130};if(Pixel(Position(node,fallback))!=Pixel(other.Position(node,fallback)))return false;}}
        return true;
    }
    // Canonical live-node geometry only. Selection and retired-node retention
    // remain presentation state; history owns deleted geometry in its mementos.
    AnimatorControllerCanvasState ForGraph(const AnimatorControllerAuthoringIds& ids) const {
        AnimatorControllerCanvasState result;
        for(const auto& layer:ids.layers){const auto any=Key(Kind::AnyNode,layer.id);result.Store(any,Pixel(Position(any,{})));
            for(size_t i=0;i<layer.states.size();++i){const auto node=Key(Kind::StateNode,layer.states[i]);result.Store(node,Pixel(Position(node,{180.f+float(i%4)*210,float(i/4)*130})));}}
        return result;
    }
    static Id Key(Kind kind,Id id) {
        if(!id || id>AnimatorControllerAuthoringIds::MaximumId)throw std::invalid_argument("Invalid canvas authoring ID");
        return (static_cast<Id>(kind)<<48)|id;
    }
    static std::optional<size_t> Find(const std::vector<Id>& ids,Id key,Kind kind) noexcept {
        if((key>>48)!=static_cast<Id>(kind))return {};
        const auto id=key&AnimatorControllerAuthoringIds::MaximumId;
        const auto it=std::find(ids.begin(),ids.end(),id);
        if(it==ids.end())return {};return static_cast<size_t>(it-ids.begin());
    }
    void Reset() noexcept {positions_.clear();layer_=state_=0;}
    void Swap(AnimatorControllerCanvasState& other) noexcept {positions_.swap(other.positions_);std::swap(layer_,other.layer_);std::swap(state_,other.state_);}
    void Resolve(const AnimatorControllerAuthoringIds& ids,size_t& layer,size_t& state) noexcept {
        layer=0;for(size_t i=0;i<ids.layers.size();++i)if(ids.layers[i].id==layer_){layer=i;break;}
        if(ids.layers.empty()){state=0;layer_=state_=0;return;}
        layer_=ids.layers[layer].id;state=0;
        const auto& states=ids.layers[layer].states;
        for(size_t i=0;i<states.size();++i)if(states[i]==state_){state=i;break;}
        state_=states.empty()?0:states[state];
    }
    bool Select(const AnimatorControllerAuthoringIds& ids,size_t layer,size_t state) noexcept {
        const bool changed=layer_!=ids.layers[layer].id || state_!=ids.layers[layer].states[state];
        layer_=ids.layers[layer].id;state_=ids.layers[layer].states[state];return changed;
    }
    Point Position(Id key,Point fallback) const noexcept {
        for(const auto& value:positions_)if(value.first==key)return value.second;
        return fallback;
    }
    // Bounded FIFO retention includes removed nodes for Undo. Eviction only loses
    // presentation history; it can never reassign a position to a different ID.
    bool Store(Id key,Point point) {
        const auto kind=key>>48;
        if((kind!=static_cast<Id>(Kind::AnyNode) && kind!=static_cast<Id>(Kind::StateNode)) ||
           !(key&AnimatorControllerAuthoringIds::MaximumId) || !std::isfinite(point.x) || !std::isfinite(point.y) ||
           std::abs(point.x)>10000000 || std::abs(point.y)>10000000)return false;
        for(auto& value:positions_)if(value.first==key){value.second=point;return true;}
        // Allocate before evicting so allocation failure keeps previous layout.
        positions_.emplace_back(key,point);
        if(positions_.size()>MaximumPositions)positions_.erase(positions_.begin());
        return true;
    }
    size_t PositionCount() const noexcept{return positions_.size();}
private:
    std::vector<std::pair<Id,Point>> positions_;
    Id layer_=0,state_=0;
};
}
