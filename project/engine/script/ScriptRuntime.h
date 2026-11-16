#pragma once

#include "ScriptComponent.h"
#include "../scene/entity/EntityWorld.h"

#include <functional>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <stdexcept>
#include <span>
#include <memory>
#include "../input/InputFrame.h"
#include "../input/FixedInputBuffer.h"
#include "ScriptAudioRequests.h"
#include "ScriptAnimatorRequests.h"
#include "LuaResourceBudget.h"
#include "ScriptSceneView.h"
#include "ScriptAssetView.h"

namespace Engine::Physics { struct TriggerEvent; }
namespace Engine::Scripting {

class LuaScriptError final : public std::runtime_error { public: using std::runtime_error::runtime_error; };

struct ScriptCallbacks {
    std::function<void(Scene::EntityRecord&)> onCreate;
    std::function<void(Scene::EntityRecord&, float)> onUpdate;
    std::function<void(Scene::EntityRecord&)> onDestroy;
    std::function<void(Scene::EntityRecord&, float)> onFixedUpdate;
    std::function<void(Scene::EntityRecord&, Scene::EntityId)> onTriggerEnter;
    std::function<void(Scene::EntityRecord&, Scene::EntityId)> onTriggerExit;
};

/// @brief Host-agnostic script lifecycle dispatcher.
/// @details Supports native callbacks, legacy command assets and isolated Lua VMs.
class ScriptRuntime final {
public:
    ScriptRuntime() = default;
    explicit ScriptRuntime(LuaResourceLimits limits) : luaBudget_(std::make_shared<LuaResourceBudget>(limits)) {}
    LuaResourceLimits GetLuaResourceLimits() const noexcept { return luaBudget_->Limits(); }
    LuaResourceUsage GetLuaResourceUsage() const noexcept { return luaBudget_->Usage(); }
    // Startup/empty-runtime configuration only; never revoke live VM ownership.
    bool ConfigureLuaResourceLimits(LuaResourceLimits limits, std::string& error);
    // Registry owner must outlive every dispatch/Shutdown. Reconfigure only
    // outside callbacks; registry writes are host-owned and runtime-thread only.
    void SetAssetOwner(const Assets::AssetRegistry& registry,const std::filesystem::path& projectRoot) {
        if(sceneView_->World())throw std::logic_error("Cannot replace Script asset owner during dispatch");
        auto next=std::make_unique<ScriptAssetView>(registry,projectRoot);assetOwner_.swap(next);
    }
    bool Register(std::string assetPath, ScriptCallbacks callbacks);
    bool LoadAsset(const std::string& assetPath, const std::filesystem::path& assetRoot, std::string& error, bool retryFailure = true);
    bool LoadResolvedAsset(const std::string& registrationKey, const std::filesystem::path& source, std::string& error, bool retryFailure = true);
    bool LoadWorldAssets(Scene::EntityWorld& world, const std::filesystem::path& assetRoot, std::string& error);
    void Unregister(const std::string& assetPath);
    void Clear();

    void Update(Scene::EntityWorld& world, float deltaSeconds);
    void FixedUpdate(Scene::EntityWorld& world,float fixedSeconds);
    void DispatchTriggers(Scene::EntityWorld& world,std::span<const Physics::TriggerEvent> events);
    // Runtime-thread only, including Create/Update/Fixed/Destroy callbacks.
    // Hosts must refuse teardown before retiring GPU/UI/jobs during dispatch.
    bool IsDispatchActive() const noexcept { return sceneView_->World()!=nullptr; }
    // Source must outlive the scope; runtime-thread only, no binding replacement
    // inside callbacks. Nested outside-dispatch scopes restore the previous source.
    class AnimatorReadScope final {
    public:
        AnimatorReadScope(ScriptRuntime& runtime,const AnimationSystem::AnimatorStateReader* source):runtime_(runtime),previous_(runtime.animatorReader_){
            if(runtime.IsDispatchActive())throw std::logic_error("Cannot replace Animator reader during dispatch");
            runtime.animatorReader_=source;
        }
        ~AnimatorReadScope(){runtime_.animatorReader_=previous_;}
        AnimatorReadScope(const AnimatorReadScope&)=delete;
        AnimatorReadScope& operator=(const AnimatorReadScope&)=delete;
    private:
        ScriptRuntime& runtime_;const AnimationSystem::AnimatorStateReader* previous_;
    };
    // Keep this scope alive around Update and every fixed tick of one host frame.
    // Must not outlive this runtime. Nested dispatcher scopes share its budget.
    LuaResourceBudget::FrameScope BeginFrame() noexcept {return LuaResourceBudget::FrameScope(*luaBudget_);}
    // Runtime-thread only. Drain every instance without a temporary ID list;
    // preserve the first native/diagnostic exception until retirement completes.
    // Recursive Shutdown is a no-op; calling from active dispatch is refused.
    // Registrations remain available for a later session.
    void Shutdown(Scene::EntityWorld& world);
    // Call once per host sample. Unavailable input flushes queued transitions.
    void SetInputFrame(InputSystem::InputFrame frame) { fixedInput_.Push(frame); inputFrame_->Swap(frame); }
    void ResetInput() noexcept { inputFrame_->Reset();fixedInput_.Reset(); }
    InputSystem::FixedInputUsage GetFixedInputUsage() const noexcept {return fixedInput_.Usage();}
    void SetAudioRequestsEnabled(bool enabled) { audioRequests_->SetEnabled(enabled); }
    std::vector<ScriptAudioRequest> TakeAudioRequests() { return audioRequests_->Take(); }
    void SetAnimatorRequestsEnabled(bool enabled) noexcept {animatorRequests_->SetEnabled(enabled);}
    ScriptAnimatorRequests::Pending TakeAnimatorRequests() noexcept {return animatorRequests_->Take();}

    bool Contains(const std::string& assetPath) const;
    size_t GetRegisteredCount() const { return scripts_.size(); }
    size_t GetRunningCount() const;
    std::string GetLastError(Scene::EntityId id) const;
    // Last source-load attempt, not a live reference validation or hook fault.
    std::string GetAssetLoadError(const std::string& registrationKey) const;
    static std::string DescribeLoadFailure(Scene::EntityId id,const std::string& reference,const std::string& error);
    ScriptProperties GetPropertyDefaults(const std::string& registrationKey) const;

private:
    void Dispatch(Scene::EntityWorld& world,float deltaSeconds,bool fixed);
    struct RegisteredScript {
        std::function<ScriptCallbacks()> factory;
        uint64_t revision = 0;
        ScriptProperties defaults;
        bool lua = false;
    };
    struct Instance {
        std::string assetPath;
        uint64_t revision = 0;
        ScriptCallbacks callbacks;
        Scene::EntityRecord lastRecord;
        bool faulted = false;
    };
    struct FailedLoad {
        std::filesystem::path source;
        uintmax_t size = 0;
        std::filesystem::file_time_type modified;
        std::string error;
    };
    // One runtime owns instances for one world. Registration changes are reconciled
    // by the next Update/Shutdown, retaining old callbacks until onDestroy runs.
    bool CanRun(const Scene::EntityRecord* entity) const;
    void StopInstance(Scene::EntityWorld& world, Scene::EntityId id);
    void RecordFault(Scene::EntityId id, const std::string& error);
    std::unordered_map<std::string, RegisteredScript> scripts_;
    std::unordered_map<Scene::EntityId, Instance> instances_;
    std::unordered_map<Scene::EntityId, std::string> errors_;
    std::unordered_map<std::string, FailedLoad> failedLoads_;
    uint64_t nextRevision_ = 1;
    Scene::EntityId nextLuaStart_ = 0;
    Scene::EntityId nextFixedLuaStart_ = 0;
    std::shared_ptr<InputSystem::InputFrame> inputFrame_ = std::make_shared<InputSystem::InputFrame>();
    InputSystem::FixedInputBuffer fixedInput_;
    std::shared_ptr<ScriptAudioRequests> audioRequests_ = std::make_shared<ScriptAudioRequests>();
    std::shared_ptr<ScriptAnimatorRequests> animatorRequests_ = std::make_shared<ScriptAnimatorRequests>();
    std::shared_ptr<LuaResourceBudget> luaBudget_ = std::make_shared<LuaResourceBudget>();
    std::shared_ptr<ScriptSceneView> sceneView_ = std::make_shared<ScriptSceneView>();
    const AnimationSystem::AnimatorStateReader* animatorReader_=nullptr;
    std::unique_ptr<ScriptAssetView> assetOwner_;
    bool shuttingDown_ = false;
};

} // namespace Engine::Scripting
