#pragma once
#include "Framework.h"
#include "engine/scene/entity/EntityWorld.h"
#include "engine/assets/AssetRegistry.h"
#include "engine/physics/PhysicsWorld.h"
#include "engine/script/ScriptRuntime.h"
#include "engine/audio/ScriptAudioHost.h"
#include "engine/audio/EntityAudioHost.h"
#include <filesystem>
#include <unordered_map>

namespace Engine::App {
// Player execution owns only runtime state: no editor history, jobs or IPC.
class RuntimeGame : public Base::Framework {
public:
    bool ConfigureStartupScene(const std::filesystem::path& path, std::string& error);
    void SetProjectFile(std::filesystem::path path) {projectFile_=path.empty()?std::filesystem::path{}:std::filesystem::absolute(path).lexically_normal();}
    void SetProjectAssetRoot(std::filesystem::path path) { assetRoot_ = std::move(path); }
    bool ConfigureLuaResourceLimits(Scripting::LuaResourceLimits limits,std::string& error) { return scripts_.ConfigureLuaResourceLimits(limits,error); }
    Scripting::LuaResourceLimits GetLuaResourceLimits() const noexcept { return scripts_.GetLuaResourceLimits(); }
    Scripting::LuaResourceUsage GetLuaResourceUsage() const noexcept { return scripts_.GetLuaResourceUsage(); }
    std::string GetLuaError(Scene::EntityId id) const { return scripts_.GetLastError(id); }
    Physics::PhysicsAdvanceUsage GetPhysicsAdvanceUsage() const noexcept {return physics_.GetAdvanceUsage();}
    InputSystem::FixedInputUsage GetFixedInputUsage() const noexcept {return scripts_.GetFixedInputUsage();}
    void SetDiagnosticOutput(bool enabled) {diagnosticOutput_=enabled;}
    void Initialize() override;
    void Update() override;
    void Draw() override;
    void Finalize() override;
    const Scene::EntityWorld& GetWorld() const { return world_; }
    const AudioSystem::ScriptAudioHost& GetScriptAudioHost() const {return scriptAudio_;}
    const AudioSystem::EntityAudioHost& GetEntityAudioHost() const {return entityAudio_;}
private:
    Scene::EntityWorld world_;
    Assets::AssetRegistry assets_;
    Physics::PhysicsWorld physics_;
    Scripting::ScriptRuntime scripts_;
    AudioSystem::ScriptAudioHost scriptAudio_;
    AudioSystem::EntityAudioHost entityAudio_;
    std::filesystem::path scenePath_, assetRoot_,projectFile_;
    std::string lastScriptLoadError_;
    std::string lastPhysicsDiagnostic_;
    bool generic_ = false;
    bool diagnosticOutput_=false;
    std::unordered_map<Scene::EntityId,std::string> diagnosticLuaErrors_;
    // Host-thread teardown admission; callbacks must not retire base services
    // recursively while the outer script dispatcher still owns live hooks.
    bool runtimeFinalizing_=false;
};
}
