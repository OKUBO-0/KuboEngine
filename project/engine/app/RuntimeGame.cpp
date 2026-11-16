#include "RuntimeGame.h"
#include "engine/scene/entity/EntityScene.h"
#include "engine/scene/entity/RuntimeSceneTransition.h"
#include "engine/script/ScriptReference.h"
#include "SceneFactory.h"
#include "SceneManager.h"
#include "DirectXCommon.h"
#include "OffscreenRenderManager.h"
#include "SrvManager.h"
#include "WinApp.h"
#include "Logger.h"
#include "Input.h"
#include <fstream>
#include <stdexcept>
#include <cstdio>

namespace Engine::App {
bool RuntimeGame::ConfigureStartupScene(const std::filesystem::path& path, std::string& error)
{
    try {
        // Match existing first-run sample compatibility.
        if(path.empty()) {error.clear();return true;}
        const auto absolute=std::filesystem::absolute(path).lexically_normal();
        if (!std::filesystem::exists(absolute)) {scenePath_=absolute;generic_=false;error.clear();return true;}
        Scene::SceneValue document;
        if(!Scene::SceneDocument::Read(absolute,document,error)) return false;
        if (document.contains("entities")) {
            Scene::EntityWorld checked;
            if (!checked.Restore(document, error)) return false;
            world_ = std::move(checked); generic_ = true;
        } else { generic_ = false; }
        scenePath_ = absolute; error.clear(); return true;
    } catch (const std::exception& exception) { error = exception.what(); return false; }
}

void RuntimeGame::Initialize()
{
    physics_.ResetSimulation();lastPhysicsDiagnostic_.clear();
    scripts_.ResetInput();
    editorToolsEnabled_ = false;
    Base::Framework::Initialize();
    scripts_.SetAudioRequestsEnabled(true);
    scripts_.SetSceneTransitionsEnabled(generic_ && !assetRoot_.empty());
    winApp->SetCloseRequestDeferred(false);
    auto factory = std::make_unique<Scene::SceneFactory>(GetProjectRoot());
    if (!generic_ && !scenePath_.empty()) factory->SetGameplayScenePath(scenePath_);
    sceneFactory = std::move(factory);
    auto* scenes = Scene::SceneManager::GetInstance();
    scenes->SetSceneFactory(sceneFactory.get());
    if (!assetRoot_.empty()) {
        std::string error;
        if (!assets_.Scan(assetRoot_, error, Assets::AssetRegistry::MetadataPolicy::ReadOnly))
            Base::Logger::Log("Player asset scan failed: " + error, Base::Logger::Level::Error);
    }
    scripts_.SetAssetOwner(assets_,GetProjectRoot());
    if (generic_) scenes->SetNextScene(std::make_unique<Scene::EntityScene>(world_, *dxCommon, assets_, GetProjectRoot(),projectFile_));
    else scenes->ChangeScene("GAMEPLAY");
}

void RuntimeGame::Update()
{
    Base::Framework::Update();
    Scripting::ScriptRuntime::AnimatorReadScope animatorRead(scripts_,dynamic_cast<Scene::EntityScene*>(Scene::SceneManager::GetInstance()->GetCurrentScene()));
    scripts_.SetAnimatorRequestsEnabled(generic_ && dynamic_cast<Scene::EntityScene*>(Scene::SceneManager::GetInstance()->GetCurrentScene()));
    if (generic_ && frameDeltaTime_ > 0) {
        if (auto* scene=dynamic_cast<Scene::EntityScene*>(Scene::SceneManager::GetInstance()->GetCurrentScene())) {
            if (const auto request=scripts_.TakeReadySceneTransition(frameDeltaTime_)) {
                Scene::EntityWorld next;
                std::filesystem::path destination;
                std::string error;
                if (Scene::LoadRuntimeScene(*request,GetProjectRoot(),assetRoot_,assets_,next,destination,error)) {
                    scripts_.Shutdown(world_);
                    scriptAudio_.Clear();
                    entityAudio_.Clear();
                    physics_.ResetSimulation();
                    scene->ResetWorldRenderState();
                    world_.Swap(next);
                    scenePath_.swap(destination);
                    diagnosticLuaErrors_.clear();
                    lastScriptLoadError_.clear();
                } else Base::Logger::Log("Scene transition failed: "+error,Base::Logger::Level::Error);
            }
        }
        auto input=InputSystem::Input::GetInstance()->CaptureFrame();
        if(auto* scene=dynamic_cast<Scene::EntityScene*>(Scene::SceneManager::GetInstance()->GetCurrentScene())){
            const auto extent=GetGameOutputExtent();
            const auto rect=Base::GamePresentationRect({Base::WinApp::kClientWidth,Base::WinApp::kClientHeight},extent);
            const auto point=Base::GameOutputPoint({input.mouseX,input.mouseY},{float(rect.left),float(rect.top)},
                {float(rect.width),float(rect.height)},extent);
            if(auto request=scene->UpdateUIButtons(input,point))scripts_.QueueSceneTransition(std::move(*request));
        }
        scripts_.SetInputFrame(std::move(input));
        std::string error;
        Scripting::LoadScriptWorldAssets(scripts_, world_, GetProjectRoot(), assets_, error);
        if (!error.empty() && error != lastScriptLoadError_)
            Base::Logger::Log("Player script load failed: " + error, Base::Logger::Level::Error);
        lastScriptLoadError_ = std::move(error);
        auto scriptFrame=scripts_.BeginFrame();
        scripts_.Update(world_, frameDeltaTime_);
        physics_.Advance(world_,frameDeltaTime_,[this](float dt){scripts_.FixedUpdate(world_,dt);},
            [this](std::span<const Physics::TriggerEvent> events){scripts_.DispatchTriggers(world_,events);});
        if(diagnosticOutput_)for(const auto& entity:world_.GetEntities()) {
            const auto fault=scripts_.GetLastError(entity.id);
            if(fault.empty()){diagnosticLuaErrors_.erase(entity.id);continue;}
            const auto previous=diagnosticLuaErrors_.find(entity.id);
            if(previous!=diagnosticLuaErrors_.end() && previous->second==fault)continue;
            if(previous==diagnosticLuaErrors_.end() && diagnosticLuaErrors_.size()>=256)continue;
            diagnosticLuaErrors_[entity.id]=fault;
            std::fprintf(stdout,"KUBO_LUA_FAULT entity=%llu error=%s\n",static_cast<unsigned long long>(entity.id),fault.c_str());std::fflush(stdout);
        }
        scriptAudio_.Synchronize(scripts_,true,false,[](uint64_t id,const std::string& error) {
            Base::Logger::Log("Player audio Entity "+std::to_string(id)+": "+error,Base::Logger::Level::Error);
        });
    }
    else if(generic_)physics_.Advance(world_,0);
    if(generic_) {
        const auto& physicsDiagnostic=physics_.GetDiagnostic();
        if(!physicsDiagnostic.empty() && physicsDiagnostic!=lastPhysicsDiagnostic_)
            Base::Logger::Log("Player physics: "+physicsDiagnostic,Base::Logger::Level::Error);
        lastPhysicsDiagnostic_=physicsDiagnostic;
    }
    if (auto* scene = dynamic_cast<Scene::EntityScene*>(Scene::SceneManager::GetInstance()->GetCurrentScene())) {
        entityAudio_.Synchronize(world_,assets_,GetProjectRoot(),generic_,false,[](uint64_t id,const std::string& error) {
            Base::Logger::Log("Player AudioSource Entity "+std::to_string(id)+": "+error,Base::Logger::Level::Error);
        });
        scene->SetSceneAspectRatio(GetGameOutputExtent().Aspect());
        scene->SetUIOutputExtent(GetGameOutputExtent());
        const auto animatorRequests=scripts_.TakeAnimatorRequests();
        if(animatorRequests.count){scene->SynchronizeModels(0);scene->ApplyAnimatorRequests(animatorRequests);}
        scene->SynchronizeModels(frameDeltaTime_);
        scene->UpdateParticles(frameDeltaTime_,generic_,false);
    }
}

void RuntimeGame::Draw()
{
    dxCommon->PrepareForFrame(); dxCommon->BeginGpuFrameTiming();
    offscreenRenderManager->Begin(); srvManager->PreDraw();
    Scene::SceneManager::GetInstance()->Draw(); offscreenRenderManager->End();
    dxCommon->Begin(); offscreenRenderManager->Draw(); dxCommon->End();
}

void RuntimeGame::Finalize()
{
    if(runtimeFinalizing_)return;
    if(scripts_.IsDispatchActive())throw std::logic_error("Cannot finalize Player during script dispatch");
    runtimeFinalizing_=true;
    struct Guard {bool& flag;~Guard(){flag=false;}} guard{runtimeFinalizing_};
    // Keep the complete host/core alive and stop before callbacks on a wait
    // failure. Retire recorded commands before hooks can release their owners.
    if(dxCommon)dxCommon->PrepareForShutdown();
    {Scripting::ScriptRuntime::AnimatorReadScope animatorRead(scripts_,dynamic_cast<Scene::EntityScene*>(Scene::SceneManager::GetInstance()->GetCurrentScene()));scripts_.Shutdown(world_);}
    diagnosticLuaErrors_.clear();
    scriptAudio_.Clear();
    entityAudio_.Clear();
    Base::Framework::Finalize();
}
}
