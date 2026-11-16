#include "EntityScene.h"
#include "Object3D.h"
#include "Object3DCommon.h"
#include "ModelManager.h"
#include "CameraManager.h"
#include "Logger.h"
#include "DirectXCommon.h"
#include "TextureManager.h"
#include "Sprite.h"
#include "UITextRaster.h"
#include "../../base/EngineResourcePath.h"
#include <filesystem>
#include <cmath>
#include <stdexcept>
#include <type_traits>
#include "../../assets/ModelReference.h"
#include "../../base/AssetPath.h"
#include "../../assets/AnimatorControllerAsset.h"
#include "../../base/ProjectManifestSelection.h"

namespace {
bool ApplyAnimatorSettings(Engine::Graphics3D::Object3D& object,const Engine::AnimationSystem::AnimatorBinding& binding) {
    if(!binding.settings.controllerGuid.empty()) {
        if(!object.SetControllerPlaybackRate(binding.settings.speed))return false;
        return binding.playing || object.SetControllerPlaybackRate(0);
    }
    object.SetAnimationSpeed(binding.EffectiveSpeed());object.SetAnimationLoop(binding.settings.loop);return true;
}
void BindRequestedAnimator(Engine::Graphics3D::Object3D& object,const Engine::AnimationSystem::AnimatorComponent* requested,
    const Engine::Assets::AssetRegistry& assets,const std::filesystem::path& root,const std::filesystem::path& selectedFile) {
    if(!requested)return;
    if(requested->controllerGuid.empty()){object.SetAnimationClip(requested->clip);return;}
    Engine::Base::ProjectContext project;std::string error;
    if(!Engine::Base::OpenProjectManifestAtRoot(root,selectedFile,project,error) ||
        !object.BindAnimationControllerAsset(project,assets,requested->controllerGuid,error))
        throw std::runtime_error("Entity Controller admission failed: "+error);
}
}

namespace Engine::Scene {
EntityScene::EntityScene(EntityWorld& world, Base::DirectXCommon& graphics, const Assets::AssetRegistry& assets,
    const std::filesystem::path& projectRoot)
    : EntityScene(world,graphics,assets,projectRoot,{}) {}
EntityScene::EntityScene(EntityWorld& world, Base::DirectXCommon& graphics, const Assets::AssetRegistry& assets,
    const std::filesystem::path& projectRoot,const std::filesystem::path& projectFile)
    : world_(world), graphics_(graphics), assets_(assets), projectPaths_(projectRoot),
      projectFile_(projectFile.empty()?std::filesystem::path{}:(projectFile.is_absolute()?projectFile:projectPaths_.Root()/projectFile).lexically_normal()) {}
EntityScene::~EntityScene() { Finalize(); }
void EntityScene::Initialize()
{
    environmentPath_ = Base::PathToUtf8(Base::ResolveEngineResource("Resources/textures/skybox/test.dds"));
    Base::TextureManager::GetInstance()->LoadTexture(environmentPath_);
    auto* manager = CameraSystem::CameraManager::GetInstance();
    previousCameraName_ = manager->GetActiveCameraName();
    authoredCamera_ = std::make_unique<CameraSystem::Camera>();
    manager->AddCamera("__entity_scene_camera", authoredCamera_.get());
}
void EntityScene::Finalize()
{
    auto* manager = CameraSystem::CameraManager::GetInstance();
    manager->DetachCamera("__entity_scene_editor_camera", registeredEditorCamera_, previousCameraName_);
    registeredEditorCamera_ = nullptr;
    manager->DetachCamera("__entity_scene_camera", authoredCamera_.get(), previousCameraName_);
    authoredCamera_.reset();
    ResetWorldRenderState();
}
void EntityScene::ResetWorldRenderState()
{
    FinalizeParticles();
    if (!models_.empty() || !sprites_.empty() || !texts_.empty()) graphics_.WaitForAllFrames();
    models_.clear();
    sprites_.clear();spriteFrame_.items.clear();editorSpriteFrame_.items.clear();
    texts_.clear();textRasterizer_.reset();retiredTextTextures_=0;
    CancelUIButtons();
    reportedModelErrors_.clear();
    reportedSpriteErrors_.clear();
    lightingFrame_ = {};
    lightingResult_ = RenderLightResult::Success;
    spriteResult_ = RenderSpriteResult::Success;
    reportedSkippedLights_ = 0;
    Graphics3D::ModelManager::GetInstance()->CollectUnusedModels();
}
void EntityScene::Update()
{
    if (auto* camera = CameraSystem::CameraManager::GetInstance()->GetActiveCamera()) camera->Update();
}
void EntityScene::SynchronizeModels(float deltaSeconds)
{
    SynchronizeSprites();
    const auto lighting=BuildRenderLightFrame(world_,lightingFrame_);
    if(lighting!=lightingResult_ || (lighting==RenderLightResult::Success && reportedSkippedLights_!=lightingFrame_.skippedInvalidFrames)) {
        if(lighting!=RenderLightResult::Success)Base::Logger::Log("Entity Light frame refused (invalid World/component, capacity/work limit or allocation); retaining previous complete frame",Base::Logger::Level::Error);
        else if(lightingFrame_.skippedInvalidFrames)Base::Logger::Log("Entity Light frame skipped broken/cyclic/singular/nonfinite hierarchy",Base::Logger::Level::Error);
    }
    lightingResult_=lighting;reportedSkippedLights_=lightingFrame_.skippedInvalidFrames;
    // Run after simulation/Inspector changes and before uploading model constants.
    SynchronizeCamera();
    const auto resolve = [&](const ModelComponent& model) {
        std::string error;
        const auto path = Assets::ResolveModelReference(model, assets_, projectPaths_.Root(), error);
        std::string resolved;
        try { resolved = Base::PathToUtf8(path); }
        catch (const std::exception& exception) { error = exception.what(); }
        if (!error.empty() && reportedModelErrors_.size() < 256 && reportedModelErrors_.insert(error).second)
            Base::Logger::Log("Entity model reference failed: " + error);
        return resolved;
    };
    for (auto it = models_.begin(); it != models_.end();) {
        const auto* entity = world_.Find(it->first);
        if (!entity || !entity->hasModel || it->second.guid != entity->model.assetGuid || it->second.asset != resolve(entity->model)) {
            graphics_.WaitForAllFrames();
            it = models_.erase(it);
        } else ++it;
    }
    for (const auto& entity : world_.GetEntities()) {
        if (!entity.active || !entity.hasModel) continue;
        const auto resolved = resolve(entity.model);
        if (resolved.empty()) continue;
        auto& entry = models_[entity.id];
        const auto* record = assets_.FindByPath(Base::PathFromUtf8(resolved));
        const auto revision = record && record->type == Assets::AssetType::Model ? record->importFingerprint : std::string{};
        if (entry.revision.NeedsReload(revision, entry.object != nullptr)) {
            const auto path = Base::PathFromUtf8(resolved);
            if (!std::filesystem::is_regular_file(path)) continue;
            try {
                RenderEntry prepared;
                prepared.asset = resolved; prepared.guid = entity.model.assetGuid;
                prepared.authoredAsset = entity.model.assetPath;
                prepared.revision.Commit(revision);
                auto candidate = Graphics3D::Object3D::Create(Graphics3D::Object3DCommon::GetInstance(),
                    Base::PathToUtf8(path.filename()), Base::PathToUtf8(std::filesystem::absolute(path).parent_path()), entity.model.assetGuid, revision);
                candidate->SetLighting(false);
                candidate->SetFrustumCullingEnabled(false);
                candidate->SetEnvironmentReflectionStrength(0.0f);
                candidate->SetSkyboxFilePath(environmentPath_);
                const auto* requested = entity.hasAnimator ? &entity.animator : nullptr;
                prepared.animatorReady = entry.animator.Prepare(requested,
                    !requested || !requested->controllerGuid.empty() || candidate->HasAnimationClip(requested->clip), true, prepared.animator);
                if (prepared.animatorReady) {
                    BindRequestedAnimator(*candidate,requested,assets_,projectPaths_.Root(),projectFile_);
                    if(!ApplyAnimatorSettings(*candidate,prepared.animator))throw std::runtime_error("Entity Controller speed exceeds float range");
                } else {
                    // First/reloaded model is visible but frozen until authored clip admission succeeds.
                    // Do not execute Object3D's missing-clip fallback.
                    candidate->SetAnimationSpeed(0);
                }
                prepared.object = std::move(candidate);
                if (entry.object) graphics_.WaitForAllFrames();
                entry = std::move(prepared);
            } catch (const std::exception& error) {
                entry.asset = resolved; entry.guid = entity.model.assetGuid;
                entry.revision.Reject(revision);
                Base::Logger::Log(std::string("Entity model load failed: ") + error.what());
            }
        }
        if (!entry.object) continue;
        // Query admission records the authored reference without resolving paths
        // or touching the filesystem from a Lua read.
        entry.authoredAsset = entity.model.assetPath;
        const auto* requested = entity.hasAnimator ? &entity.animator : nullptr;
        if (!entry.animator.Matches(requested) || !entry.animatorReady) {
            AnimationSystem::AnimatorBinding prepared;
            const bool available = !requested || !requested->controllerGuid.empty() || entry.object->HasAnimationClip(requested->clip);
            if (!entry.animator.Prepare(requested, available, false, prepared)) {
                entry.animatorReady = false;
                const auto message = std::string("Entity Animator admission failed: ") + std::to_string(entity.id);
                if (reportedModelErrors_.size() < 256 && reportedModelErrors_.insert(message).second) Base::Logger::Log(message);
            } else {
                try {
                    if (entry.animator.NeedsNewOwner(requested)) {
                        const auto path = Base::PathFromUtf8(entry.asset);
                        auto candidate = Graphics3D::Object3D::Create(Graphics3D::Object3DCommon::GetInstance(),
                            Base::PathToUtf8(path.filename()), Base::PathToUtf8(std::filesystem::absolute(path).parent_path()), entry.guid, entry.revision.current);
                        candidate->SetLighting(false); candidate->SetFrustumCullingEnabled(false);
                        candidate->SetEnvironmentReflectionStrength(0.0f); candidate->SetSkyboxFilePath(environmentPath_);
                        BindRequestedAnimator(*candidate,requested,assets_,projectPaths_.Root(),projectFile_);
                        if(!ApplyAnimatorSettings(*candidate,prepared))throw std::runtime_error("Entity Controller speed exceeds float range");
                        graphics_.WaitForAllFrames();
                        entry.object.swap(candidate);
                    } else {
                        if(!ApplyAnimatorSettings(*entry.object,prepared))throw std::runtime_error("Entity Controller speed exceeds float range");
                    }
                    entry.animator.Swap(prepared); entry.animatorReady = true;
                } catch (const std::exception& error) {
                    entry.animatorReady = false;
                    const auto message = std::string("Entity Animator apply failed: ") + error.what();
                    if (reportedModelErrors_.size() < 256 && reportedModelErrors_.insert(message).second) Base::Logger::Log(message);
                }
            }
        }
        const auto& transform = entity.transform;
        const MaterialComponent material=entity.hasMaterial?entity.material:MaterialComponent{};
        if(material.IsValid()){
            entry.object->SetLighting(material.lighting);entry.object->SetColor(material.color);
            entry.object->SetShininess(material.shininess);
            entry.object->SetEnvironmentRoughness(material.roughness);
            entry.object->SetEnvironmentReflectionStrength(material.reflectionStrength);
            entry.object->SetTextureInfluence(material.textureInfluence);
        }else{
            const auto message="Entity Material settings rejected: "+std::to_string(entity.id);
            if(reportedModelErrors_.size()<256 && reportedModelErrors_.insert(message).second)Base::Logger::Log(message,Base::Logger::Level::Error);
        }
        entry.object->SetTransform({transform.scale, transform.rotation, transform.position});
        Matrix4x4 worldMatrix{};
        if (!world_.GetWorldMatrix(entity.id, worldMatrix)) continue;
        entry.object->SetWorldMatrix(worldMatrix);
        entry.object->Update(deltaSeconds);
    }
    Graphics3D::ModelManager::GetInstance()->CollectUnusedModels();
}
void EntityScene::SynchronizeCamera()
{
    auto* manager = CameraSystem::CameraManager::GetInstance();
    if (editorCamera_) {
        manager->AddCamera("__entity_scene_editor_camera", editorCamera_);
        registeredEditorCamera_ = editorCamera_;
        manager->SetActiveCamera("__entity_scene_editor_camera");
        editorCamera_->SetAspectRatio(sceneAspect_); editorCamera_->Update();
        return;
    }
    manager->DetachCamera("__entity_scene_editor_camera", registeredEditorCamera_, previousCameraName_);
    registeredEditorCamera_ = nullptr;
    EntityId id = 0;
    Matrix4x4 rigidWorld{};
    if (authoredCamera_ && world_.GetPrimaryCamera(id, rigidWorld)) {
        const auto& projection = world_.Find(id)->camera;
        authoredCamera_->SetWorldMatrix(rigidWorld);
        authoredCamera_->SetFovY(projection.fovY);
        authoredCamera_->SetOrthographic(projection.orthographic, projection.orthographicHeight);
        authoredCamera_->SetNearClip(projection.nearClip);
        authoredCamera_->SetFarClip(projection.farClip);
        authoredCamera_->SetAspectRatio(sceneAspect_);
        authoredCamera_->Update();
        manager->SetActiveCamera("__entity_scene_camera");
    } else {
        manager->SetActiveCamera("default");
        if (auto* camera = manager->GetActiveCamera()) { camera->SetAspectRatio(sceneAspect_); camera->Update(); }
    }
}
void EntityScene::Draw()
{
    DrawView(false);
}
void EntityScene::DrawEditorView()
{
    DrawView(true);
}
void EntityScene::DrawView(bool editorView)
{
    auto* common=Graphics3D::Object3DCommon::GetInstance();
    struct Restore {Graphics3D::Object3DCommon* owner;Math::AuthoredLightData previous;~Restore(){owner->SetAuthoredLights(previous);}} restore{common,common->GetAuthoredLights()};
    if(!common->SetAuthoredLights(lightingFrame_.gpu))throw std::logic_error("Invalid prepared Light GPU frame");
    common->CommonDraw();
    for (const auto& entity : world_.GetEntities()) {
        if (!entity.active || !entity.hasModel) continue;
        const auto found = models_.find(entity.id);
        if (found == models_.end() || !found->second.object) continue;
        found->second.object->DrawAnimated();
    }
    if(editorView || !IsParticlePreview())DrawParticles();
    DrawSprites(editorView?editorSpriteFrame_:spriteFrame_);
}
void EntityScene::DrawGameView(float aspect)
{
    if(!std::isfinite(aspect) || aspect<=0) throw std::invalid_argument("Invalid Game view aspect");
    auto* saved=editorCamera_; const auto savedAspect=sceneAspect_; editorCamera_=nullptr; sceneAspect_=aspect;
    const auto refresh=[&] { for (auto& [id,entry] : models_) { (void)id; if (entry.object) entry.object->RefreshViewTransform(); } };
    const auto restore=[&] { editorCamera_=saved; sceneAspect_=savedAspect; SynchronizeCamera(); refresh(); };
    try { SynchronizeCamera(); refresh(); Draw(); }
    catch (...) { restore(); throw; }
    restore();
}
void EntityScene::SetSceneAspectRatio(float aspect)
{
    if (!std::isfinite(aspect) || aspect <= 0) throw std::invalid_argument("Invalid Scene view aspect");
    sceneAspect_=aspect;
}
void EntityScene::ResetAnimations()
{
    for (auto& [id, entry] : models_) {
        if (entry.object) {
            entry.object->ResetAnimationTime();
            const auto* entity = world_.Find(id);
            entry.animator.ResetIntent(entity && entity->hasAnimator ? &entity->animator : nullptr);
            ApplyAnimatorSettings(*entry.object,entry.animator);
        }
    }
}
bool EntityScene::ControlAnimator(EntityId id, AnimationSystem::AnimatorCommand command)
{
    auto found = models_.find(id); const auto* entity = world_.Find(id);
    if (found == models_.end() || !entity || !entity->hasAnimator || !entity->active || !entity->hasModel) return false;
    auto& entry = found->second;
    if (!entry.object || !entry.animatorReady || !entry.animator.Matches(&entity->animator) || !entry.animator.Apply(command)) return false;
    if (command == AnimationSystem::AnimatorCommand::Stop || command == AnimationSystem::AnimatorCommand::Restart) entry.object->ResetAnimationTime();
    return ApplyAnimatorSettings(*entry.object,entry.animator);
}
EntityScene::AnimatorRenderStatus EntityScene::GetAnimatorRenderStatus(EntityId id) const
{
    const auto found = models_.find(id); const auto* entity = world_.Find(id);
    if (found == models_.end() || !entity || !entity->hasAnimator || !entity->hasModel || !entity->active) return {};
    const auto& entry = found->second;
    const bool ready = entry.object && entry.animatorReady && entry.animator.Matches(&entity->animator);
    return {ready, ready && entry.animator.playing, entry.object ? entry.object->GetAnimationTime() : 0};
}
bool EntityScene::ReloadController(EntityId id,std::string& error,const Assets::AnimatorControllerRevision* expected)
{
    try {
        const auto* entity=world_.Find(id);
        if(!entity || !entity->active || !entity->hasModel || !entity->hasAnimator ||
            !entity->animator.IsValid() || entity->animator.controllerGuid.empty()){
            error="Controller reload requires an active Entity with Model and valid Controller settings";return false;
        }
        Base::ProjectContext project;
        if(!Base::OpenProjectManifestAtRoot(projectPaths_.Root(),projectFile_,project,error))return false;
        if(auto* object=ReadyController(id)){
            const auto& entry=models_.at(id);
            graphics_.WaitForAllFrames();
            return object->BindAnimationControllerAssetAtRate(project,assets_,entity->animator.controllerGuid,
                entry.animator.settings.speed,entry.animator.playing,error,expected);
        }
        // An initial Controller failure can reject the Model fingerprint too.
        // Never clear that suppression to manufacture an every-frame retry.
        // This explicit command instead admits a private replacement owner.
        const auto path=Assets::ResolveModelReference(entity->model,assets_,projectPaths_.Root(),error);
        if(path.empty())return false;
        const auto resolved=Base::PathToUtf8(path);auto found=models_.find(id);
        if(found!=models_.end() && (found->second.asset!=resolved || found->second.guid!=entity->model.assetGuid)){
            error="Model reference changed; synchronize the Scene before retrying its Controller";return false;
        }
        Assets::HeldAnimatorControllerAsset admission;
        if(!admission.Open(project,assets_,entity->animator.controllerGuid,error,expected))return false;
        const auto* record=assets_.FindByPath(path);
        RenderEntry prepared;prepared.asset=resolved;prepared.guid=entity->model.assetGuid;
        prepared.authoredAsset=entity->model.assetPath;
        prepared.revision.Commit(record && record->type==Assets::AssetType::Model?record->importFingerprint:std::string{});
        const auto* previous=found!=models_.end()?&found->second.animator:nullptr;
        const bool newModel=found==models_.end() || !found->second.object;
        const AnimationSystem::AnimatorBinding empty;
        if(!(previous?*previous:empty).Prepare(&entity->animator,true,newModel,prepared.animator)){
            error="Cannot prepare Controller owner intent";return false;
        }
        // Model initialization and canonical binding can touch shared GPU
        // resources. Retire prior readers before any candidate acquisition.
        graphics_.WaitForAllFrames();
        auto candidate=Graphics3D::Object3D::Create(Graphics3D::Object3DCommon::GetInstance(),
            Base::PathToUtf8(path.filename()),Base::PathToUtf8(std::filesystem::absolute(path).parent_path()),
            entity->model.assetGuid,prepared.revision.current);
        candidate->SetLighting(false);candidate->SetFrustumCullingEnabled(false);
        candidate->SetEnvironmentReflectionStrength(0.0f);candidate->SetSkyboxFilePath(environmentPath_);
        if(!candidate->BindAnimationControllerAssetAtRate(project,assets_,entity->animator.controllerGuid,
            prepared.animator.settings.speed,prepared.animator.playing,error,admission.Snapshot()))return false;
        prepared.animatorReady=true;prepared.object=std::move(candidate);
        static_assert(std::is_nothrow_move_assignable_v<RenderEntry>);
        if(found==models_.end())models_.emplace(id,std::move(prepared));
        else found->second=std::move(prepared);
        error.clear();return true;
    }catch(const std::exception& exception){error=exception.what();return false;}
}
Graphics3D::Object3D* EntityScene::ReadyController(EntityId id) const noexcept
{
    const auto found=models_.find(id);const auto* entity=world_.Find(id);
    if(found==models_.end() || !entity || !entity->active || !entity->hasModel || !entity->hasAnimator || entity->animator.controllerGuid.empty())return nullptr;
    const auto& entry=found->second;
    if(!entry.object || !entry.animatorReady || !entry.animator.Matches(&entity->animator) ||
        entry.authoredAsset!=entity->model.assetPath || entry.guid!=entity->model.assetGuid || !entry.revision.failed.empty())return nullptr;
    const auto* revision=entry.object->GetControllerAssetRevision();
    return revision && revision->guid==entity->animator.controllerGuid ? entry.object.get() : nullptr;
}
std::string EntityScene::GetControllerState(EntityId id) const
{
    const auto* object=ReadyController(id);return object ? std::string(object->GetControllerState()) : std::string{};
}
std::string_view EntityScene::ReadControllerState(const EntityRecord& candidate,const AnimationSystem::AnimatorComponent& settings) const noexcept
{
    const auto* object=ReadyController(candidate.id);if(!object || settings.controllerGuid.empty())return {};
    const auto found=models_.find(candidate.id);const auto& entry=found->second;
    if(!entry.animator.Matches(&settings) || !candidate.hasModel || entry.authoredAsset!=candidate.model.assetPath || entry.guid!=candidate.model.assetGuid)return {};
    return object->GetControllerState();
}
bool EntityScene::SetControllerFloat(EntityId id,std::string_view name,double value) {
    auto* object=ReadyController(id);return object && object->SetControllerFloat(name,value);
}
std::string EntityScene::GetControllerState(EntityId id,size_t layer) const {
    const auto* object=ReadyController(id);return object?std::string(object->GetControllerState(layer)):std::string{};
}
bool EntityScene::SetControllerLayerWeight(EntityId id,size_t layer,float weight) {
    auto* object=ReadyController(id);return object && object->SetControllerLayerWeight(layer,weight);
}
bool EntityScene::ForceControllerState(EntityId id,size_t layer,std::string_view name,float duration) {
    auto* object=ReadyController(id);return object && object->ForceControllerState(layer,name,duration);
}
std::vector<std::string> EntityScene::GetControllerStateNames(EntityId id,size_t layer) const {
    const auto* object=ReadyController(id);return object?object->GetControllerStateNames(layer):std::vector<std::string>{};
}
AnimationSystem::ControllerLayerState EntityScene::ReadControllerLayer(const EntityRecord& candidate,const AnimationSystem::AnimatorComponent& settings,size_t layer) const noexcept {
    const auto* object=ReadyController(candidate.id);if(!object || settings.controllerGuid.empty())return {};
    const auto found=models_.find(candidate.id);const auto& entry=found->second;
    if(!entry.animator.Matches(&settings) || !candidate.hasModel || entry.authoredAsset!=candidate.model.assetPath || entry.guid!=candidate.model.assetGuid)return {};
    const auto state=object->GetControllerState(layer);const auto time=object->GetControllerLayerTime(layer);
    const auto weight=object->GetControllerLayerWeight(layer);const auto progress=object->GetControllerLayerProgress(layer);
    const auto normalized=object->GetControllerLayerNormalizedTime(layer);
    if(state.empty() || !time || !weight || !progress || !normalized)return {};
    return {true,*time,*weight,*progress,*normalized,state};
}
bool EntityScene::SetControllerBool(EntityId id,std::string_view name,bool value) {
    auto* object=ReadyController(id);return object && object->SetControllerBool(name,value);
}
bool EntityScene::SetControllerTrigger(EntityId id,std::string_view name) {
    auto* object=ReadyController(id);return object && object->SetControllerTrigger(name);
}
AnimationSystem::AnimatorPlaybackState EntityScene::ReadAnimatorState(const EntityRecord& candidate,const AnimationSystem::AnimatorComponent& settings) const noexcept
{
    const auto* live=world_.Find(candidate.id);const auto found=models_.find(candidate.id);
    if(!live || !live->active || !live->hasModel || !live->hasAnimator || !candidate.hasModel || found==models_.end())return {};
    const auto& entry=found->second;
    if(!entry.object || !entry.animatorReady || !entry.animator.Matches(&settings) ||
        entry.authoredAsset!=candidate.model.assetPath || entry.guid!=candidate.model.assetGuid ||
        entry.authoredAsset!=live->model.assetPath || entry.guid!=live->model.assetGuid)return {};
    const auto time=entry.object->GetAnimationTime();
    if(!std::isfinite(time) || time<0)return {};
    return {true,entry.animator.playing,time};
}
std::vector<std::string> EntityScene::GetAnimationClipNames(EntityId id) const
{
    const auto* entity=world_.Find(id);const auto found=models_.find(id);
    if(!entity || !entity->active || !entity->hasModel || found==models_.end())return {};
    const auto& entry=found->second;
    if(!entry.object || !entry.revision.failed.empty() || entry.authoredAsset!=entity->model.assetPath || entry.guid!=entity->model.assetGuid)return {};
    return entry.object->GetAnimationClipNames();
}
EntityScene::AnimatorRequestUsage EntityScene::ApplyAnimatorRequests(const Scripting::ScriptAnimatorRequests::Pending& requests)
{
    if(requests.count>requests.values.size())throw std::invalid_argument("Invalid Animator pending count");
    AnimatorRequestUsage usage;
    for(size_t i=0;i<requests.count;++i){const auto& request=requests.values[i];const auto* entity=world_.Find(request.entity);
        bool applied=false;
        if(entity && entity->hasAnimator && request.Matches(entity->animator)) {
            using Scripting::ScriptAnimatorRequestKind;
            if(request.kind==ScriptAnimatorRequestKind::Playback)applied=ControlAnimator(request.entity,request.command);
            else if(request.ValidControllerPayload()) {
                const std::string_view name(request.parameter.data(),request.parameterBytes);
                switch(request.kind) {
                case ScriptAnimatorRequestKind::Float:applied=SetControllerFloat(request.entity,name,request.number);break;
                case ScriptAnimatorRequestKind::Bool:applied=SetControllerBool(request.entity,name,request.flag);break;
                case ScriptAnimatorRequestKind::Trigger:applied=SetControllerTrigger(request.entity,name);break;
                case ScriptAnimatorRequestKind::LayerWeight:applied=SetControllerLayerWeight(request.entity,request.layer,static_cast<float>(request.number));break;
                case ScriptAnimatorRequestKind::ForceState:applied=ForceControllerState(request.entity,request.layer,name,static_cast<float>(request.number));break;
                default:break;
                }
            }
        }
        if(applied)++usage.applied;
        else ++usage.rejected;
    }
    return usage;
}
EntityScene::ModelRenderStatus EntityScene::GetModelRenderStatus(EntityId id) const
{
    const auto found = models_.find(id);
    if (found == models_.end()) return {};
    return {found->second.object != nullptr, found->second.revision.current, found->second.revision.failed};
}
bool EntityScene::GetWorldModelBounds(EntityId id, Editor::SceneBounds& output) const
{
    const auto found = models_.find(id);
    const auto* entity = world_.Find(id);
    if (found == models_.end() || !found->second.object || !entity || !entity->active || !entity->hasModel) return false;
    Editor::SceneBounds local; Matrix4x4 world{};
    if (!found->second.object->GetLocalModelBounds(local.min,local.max) || !world_.GetWorldMatrix(id,world)) return false;
    return Editor::TransformSceneBounds(local,world,output);
}
EntityId EntityScene::PickModel(const Editor::ScenePickSegment& ray) const
{
    EntityId chosen=0; double nearest=2;
    for (const auto& [id,entry] : models_) {
        (void)entry;
        Editor::SceneBounds bounds; double fraction=0;
        if (GetWorldModelBounds(id,bounds) && Editor::IntersectSceneBounds(ray,bounds,fraction) &&
            (fraction<nearest || (fraction==nearest && (chosen==0 || id<chosen)))) { nearest=fraction; chosen=id; }
    }
    return chosen;
}
}
