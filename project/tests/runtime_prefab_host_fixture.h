#pragma once
#include "engine/scene/entity/EntityWorld.h"
#include <filesystem>
#include <memory>
struct ID3D12Device;
class AnimatorHostFixture;
namespace Engine::AudioSystem {class EntityAudioHost;class ScriptAudioHost;}
class RuntimePrefabHostFixture final {
    std::unique_ptr<AnimatorHostFixture> scaffold_;
    std::filesystem::path root_;std::string baseline_,prefabBytes_,heavyPrefabBytes_,sceneBytes_;int frames_=0;bool progressed_=false;
public:
    static constexpr Engine::Scene::EntityId Driver=1,Fault=2,SpawnedRoot=4;
    RuntimePrefabHostFixture();~RuntimePrefabHostFixture();
    void Prepare(const char* role);void CaptureGpu(ID3D12Device* device);void VerifyGpu() const;
    void VerifySources() const;
    void Observe(const Engine::Scene::EntityWorld& world,const Engine::AudioSystem::EntityAudioHost& audio,
        const Engine::AudioSystem::ScriptAudioHost& scriptAudio,const std::string& driverError,const std::string& faultError,const std::string& rootError);
    const std::filesystem::path& Root() const{return root_;}
    const std::string& Baseline() const{return baseline_;}
    bool Done() const{return frames_>=18 && progressed_;}
};
