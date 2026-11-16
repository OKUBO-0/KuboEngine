#pragma once
#include <string_view>

#include "../core/BaseScene.h"
#include "EntityWorld.h"
#include "RenderLightFrame.h"
#include "RenderSpriteFrame.h"
#include "RenderParticleFrame.h"
#include "../../assets/AssetRegistry.h"
#include "../../assets/AssetRevision.h"
#include "../../editor/ScenePicking.h"
#include "../../base/ProjectPaths.h"
#include "../../animation/AnimatorBinding.h"
#include "../../animation/AnimatorStateReader.h"
#include "../../script/ScriptAnimatorRequests.h"
#include "../../script/ScriptSceneTransition.h"
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace Engine::Graphics3D { class Object3D; }
namespace Engine::Graphics2D { class Sprite; }
namespace Engine::Assets { struct AnimatorControllerRevision; }
namespace Engine::Base { class DirectXCommon; }
namespace Engine::CameraSystem { class Camera; }
namespace Engine::InputSystem { struct InputFrame; }
namespace Engine::Scene {
class UITextRasterizer;

// Generic authored models plus painter-ordered screen overlay Canvas/Sprites.
class EntityScene final : public BaseScene, public AnimationSystem::AnimatorStateReader {
public:
    EntityScene(EntityWorld& world, Base::DirectXCommon& graphics, const Assets::AssetRegistry& assets,
        const std::filesystem::path& projectRoot);
    EntityScene(EntityWorld& world, Base::DirectXCommon& graphics, const Assets::AssetRegistry& assets,
        const std::filesystem::path& projectRoot,const std::filesystem::path& projectFile);
    ~EntityScene() override;
    void Initialize() override;
    void Finalize() override;
    void Update() override;
    void Draw() override;
    void DrawGameView(float aspect = 16.0f/9.0f);
    void SetSceneAspectRatio(float aspect);
    void SetUIOutputExtent(Base::ViewExtent extent);
    RenderSpriteResult GetSpriteResult() const noexcept {return spriteResult_;}
    const RenderSpriteFrame& GetSpriteFrame() const noexcept {return spriteFrame_;}
    std::optional<Scripting::ScriptSceneTransition> UpdateUIButtons(const InputSystem::InputFrame& input,std::optional<Vector2> point);
    void CancelUIButtons() noexcept;
    void SynchronizeModels(float deltaSeconds);
    // Explicit host intent. Zero delta does not mean Editing/Stop; one call per
    // host frame advances fixed Particle ticks, independently of view draws.
    void UpdateParticles(float deltaSeconds,bool running,bool paused=false);
    struct ParticleRenderStatus {RenderParticleResult result=RenderParticleResult::Success;size_t emitters=0,particles=0,dropped=0,steps=0;};
    ParticleRenderStatus GetParticleRenderStatus() const noexcept;
    RenderLightResult GetLightingResult() const noexcept {return lightingResult_;}
    const RenderLightFrame& GetLightingFrame() const noexcept {return lightingFrame_;}
    // Non-owning transient editor view; caller must outlive the scene/clear it.
    void SetEditorCamera(CameraSystem::Camera* camera) { editorCamera_ = camera; }
    void ResetAnimations();
    void ResetWorldRenderState();
    bool ControlAnimator(EntityId id, AnimationSystem::AnimatorCommand command);
    // Explicit source admission; repair failed initial/nonready owners privately.
    // Reset clocks/defaults, retain existing Pause intent and authored World.
    // A first owner uses authored playOnAwake. No automatic file watching.
    bool ReloadController(EntityId id,std::string& error,const Assets::AnimatorControllerRevision* expected=nullptr);
    struct AnimatorRequestUsage {size_t applied=0,rejected=0;};
    AnimatorRequestUsage ApplyAnimatorRequests(const Scripting::ScriptAnimatorRequests::Pending& requests);
    struct AnimatorRenderStatus { bool ready = false, playing = false; float time = 0; };
    AnimatorRenderStatus GetAnimatorRenderStatus(EntityId id) const;
    std::vector<std::string> GetAnimationClipNames(EntityId id) const;
    std::string GetControllerState(EntityId id) const;
    std::string GetControllerState(EntityId id,size_t layer) const;
    bool SetControllerLayerWeight(EntityId id,size_t layer,float weight);
    bool ForceControllerState(EntityId id,size_t layer,std::string_view name,float blendSeconds=0.2f);
    std::vector<std::string> GetControllerStateNames(EntityId id,size_t layer) const;
    bool SetControllerFloat(EntityId id,std::string_view name,double value);
    bool SetControllerBool(EntityId id,std::string_view name,bool value);
    bool SetControllerTrigger(EntityId id,std::string_view name);
    const EntityWorld* OwnerWorld() const noexcept override {return &world_;}
    AnimationSystem::AnimatorPlaybackState ReadAnimatorState(const EntityRecord& candidate,const AnimationSystem::AnimatorComponent& settings) const noexcept override;
    std::string_view ReadControllerState(const EntityRecord& candidate,const AnimationSystem::AnimatorComponent& settings) const noexcept override;
    AnimationSystem::ControllerLayerState ReadControllerLayer(const EntityRecord& candidate,const AnimationSystem::AnimatorComponent& settings,size_t layer) const noexcept override;
    struct ModelRenderStatus { bool loaded = false; std::string revision, failedRevision; };
    ModelRenderStatus GetModelRenderStatus(EntityId id) const;
    bool GetWorldModelBounds(EntityId id, Editor::SceneBounds& output) const;
    EntityId PickModel(const Editor::ScenePickSegment& ray) const;
private:
    struct ParticleSceneState;
    struct ParticleStateDeleter {void operator()(ParticleSceneState*) const noexcept;};
    std::unique_ptr<ParticleSceneState,ParticleStateDeleter> particles_;
    void DrawParticles();
    void FinalizeParticles() noexcept;
    void SynchronizeSprites();
    void SynchronizeUIText(const RenderSpriteFrame& frame);
    void ReportSpriteError(const std::string& message);
    void DrawSprites();
    void SynchronizeCamera();
    Graphics3D::Object3D* ReadyController(EntityId id) const noexcept;
    struct RenderEntry {
        std::string asset;
        std::string authoredAsset;
        std::string guid;
        Assets::ModelRevisionState revision;
        std::unique_ptr<Graphics3D::Object3D> object;
        AnimationSystem::AnimatorBinding animator;
        bool animatorReady = false;
    };
    EntityWorld& world_;
    Base::DirectXCommon& graphics_;
    const Assets::AssetRegistry& assets_;
    const Base::ProjectPaths projectPaths_;
    const std::filesystem::path projectFile_;
    std::unordered_set<std::string> reportedModelErrors_;
    std::string environmentPath_;
    std::unordered_map<EntityId, RenderEntry> models_;
    struct SpriteEntry {
        std::string asset,guid,authoredAsset;
        std::unique_ptr<Graphics2D::Sprite> object;
        bool Matches(const SpriteComponent& sprite) const noexcept {
            return object && guid==sprite.assetGuid && (!guid.empty() || authoredAsset==sprite.assetPath);
        }
    };
    struct TextEntry {
        std::string text;
        Vector2 size{};
        float textSize=0;
        UITextAlignment alignment{};
        std::unique_ptr<Graphics2D::Sprite> object;
        bool Matches(const SpriteComponent& sprite) const noexcept {
            return object && text==sprite.text && textSize==sprite.textSize &&
                alignment==sprite.textAlignment && size.x==sprite.size.x && size.y==sprite.size.y;
        }
    };
    std::unordered_map<EntityId,SpriteEntry> sprites_;
    std::unordered_map<EntityId,TextEntry> texts_;
    std::unique_ptr<UITextRasterizer> textRasterizer_;
    size_t retiredTextTextures_=0;
    std::unordered_set<std::string> reportedSpriteErrors_;
    Base::ViewExtent uiOutputExtent_{};
    RenderSpriteFrame spriteFrame_;
    RenderSpriteResult spriteResult_=RenderSpriteResult::Success;
    EntityId hoveredButton_=0,pressedButton_=0;
    std::string pressedButtonScene_;
    std::unique_ptr<CameraSystem::Camera> authoredCamera_;
    CameraSystem::Camera* editorCamera_ = nullptr;
    CameraSystem::Camera* registeredEditorCamera_ = nullptr;
    float sceneAspect_ = 16.0f / 9.0f;
    std::string previousCameraName_;
    RenderLightFrame lightingFrame_;
    RenderLightResult lightingResult_=RenderLightResult::Success;
    size_t reportedSkippedLights_=0;
};
}
