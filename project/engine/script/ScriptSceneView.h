#pragma once
#include <cstdint>
#include <limits>
#include "../animation/AnimatorStateReader.h"

namespace Engine::Scene { class EntityWorld; }
namespace Engine::Scripting {
class ScriptAssetView;
// Runtime-thread scoped borrowed view. Never owns or retains a World lifetime.
// Lua checks its own active hook in addition to this host dispatch scope.
class ScriptSceneView final {
    friend class ScriptSceneCommands;
    friend class ScriptSceneStructure;
public:
    const Scene::EntityWorld* World() const noexcept {return world_;}
    const AnimationSystem::AnimatorStateReader* AnimatorReader() const noexcept {return animatorReader_;}
    class Scope {
    public:
        Scope(ScriptSceneView& view,const Scene::EntityWorld& world) noexcept
            :Scope(view,world,nullptr,nullptr,nullptr){}
        Scope(ScriptSceneView& view,const Scene::EntityWorld& world,const AnimationSystem::AnimatorStateReader* reader) noexcept:Scope(view,world,nullptr,nullptr,reader){}
        ~Scope(){view_.world_=previous_;view_.writeWorld_=previousWrite_;view_.scopeToken_=previousToken_;view_.assets_=previousAssets_;view_.animatorReader_=previousReader_;}
        Scope(const Scope&)=delete;
        Scope& operator=(const Scope&)=delete;
    protected:
        Scope(ScriptSceneView& view,const Scene::EntityWorld& world,Scene::EntityWorld* writable,const ScriptAssetView* assets,const AnimationSystem::AnimatorStateReader* reader) noexcept
            :view_(view),previous_(view.world_),previousWrite_(view.writeWorld_),previousToken_(view.scopeToken_),previousAssets_(view.assets_),previousReader_(view.animatorReader_){
            view_.world_=&world;view_.writeWorld_=writable;view_.assets_=assets;
            view_.animatorReader_=reader;
            // Exhaustion disables writes rather than recycling an expired token.
            view_.scopeToken_=view_.nextToken_==(std::numeric_limits<uint64_t>::max)() ? 0 : ++view_.nextToken_;
        }
    private:
        ScriptSceneView& view_;
        const Scene::EntityWorld* previous_;
        Scene::EntityWorld* previousWrite_;
        uint64_t previousToken_;
        const ScriptAssetView* previousAssets_;
        const AnimationSystem::AnimatorStateReader* previousReader_;
    };
    // Separate explicit write admission. A normal Scope stays read-only even
    // when constructed with a non-const World; no implicit const_cast writes.
    class WriteScope final : public Scope {
    public:
        WriteScope(ScriptSceneView& view,Scene::EntityWorld& world,const ScriptAssetView* assets=nullptr,const AnimationSystem::AnimatorStateReader* reader=nullptr) noexcept:Scope(view,world,&world,assets,reader){}
    };
private:
    const Scene::EntityWorld* world_=nullptr;
    Scene::EntityWorld* writeWorld_=nullptr;
    uint64_t scopeToken_=0,nextToken_=0;
    const ScriptAssetView* assets_=nullptr;
    const AnimationSystem::AnimatorStateReader* animatorReader_=nullptr;
};
}
