#pragma once
#include "../scene/entity/EntityWorld.h"
#include <string_view>
namespace Engine::AnimationSystem {
struct AnimatorPlaybackState {bool ready=false,playing=false;float time=0;};
struct ControllerLayerState {bool ready=false;float time=0,weight=0;double progress=0,normalized=0;std::string_view state;};
// Runtime-thread borrowed capability. Source/World must outlive every scope
// using it; never retained by Lua or invoked outside an active hook.
class AnimatorStateReader {
public:
    virtual ~AnimatorStateReader()=default;
    virtual const Scene::EntityWorld* OwnerWorld() const noexcept=0;
    virtual AnimatorPlaybackState ReadAnimatorState(const Scene::EntityRecord& candidate,const AnimatorComponent& settings) const noexcept=0;
    // Borrowed immutable state name; caller copies it before the hook/scope ends.
    virtual std::string_view ReadControllerState(const Scene::EntityRecord&,const AnimatorComponent&) const noexcept {return {};}
    virtual ControllerLayerState ReadControllerLayer(const Scene::EntityRecord&,const AnimatorComponent&,size_t) const noexcept {return {};}
};
}
