#pragma once

#include "SceneDocument.h"
#include "Vector3.h"
#include "Matrix4x4.h"
#include "../../physics/PhysicsComponents.h"
#include "../../script/ScriptComponent.h"
#include "../../audio/AudioComponent.h"
#include "../../animation/AnimatorComponent.h"
#include "RenderComponents.h"
#include "UIComponents.h"
#include "ParticleComponents.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <span>
#include <vector>

namespace Engine::Scene {

using EntityId = uint64_t;

struct TransformComponent {
    Vector3 position{0.0f, 0.0f, 0.0f};
    Vector3 rotation{0.0f, 0.0f, 0.0f};
    Vector3 scale{1.0f, 1.0f, 1.0f};
};

struct EntityTransformOverride {
    EntityId id=0;
    TransformComponent transform{};
};

struct ModelComponent {
    std::string assetPath;
    std::string assetGuid; // When present, authoritative; assetPath is a display/legacy hint.
    static bool IsValidGuid(const std::string& guid) {
        return RenderComponentDetail::ValidAssetGuid(guid);
    }
};

struct CameraComponent {
    bool primary = true;
    float fovY = 0.45f; // radians
    float nearClip = 0.1f;
    float farClip = 100.0f;
    bool orthographic = false;
    float orthographicHeight = 10.0f; // Full vertical span in world units, not half-height.
    bool IsValid() const;
};

struct EntityRecord {
    EntityId id = 0;
    EntityId parent = 0;
    std::string name;
    bool active = true;
    TransformComponent transform{};
    bool hasModel = false;
    ModelComponent model{};
    bool hasCamera = false;
    CameraComponent camera{};
    bool hasCollider = false;
    Physics::ColliderComponent collider{};
    bool hasRigidBody = false;
    Physics::RigidBodyComponent rigidBody{};
    bool hasScript = false;
    Scripting::ScriptComponent script{};
    bool hasAudio=false;
    AudioSystem::AudioComponent audio{};
    // Root-only authored Prefab source/baseline/local-to-Scene identity data.
    // Null for ordinary Entities; runtime executes baked components, not links.
    SceneValue prefabLink;
    bool hasAnimator = false;
    AnimationSystem::AnimatorComponent animator{};
    bool hasLight=false;
    LightComponent light{};
    bool hasMaterial=false;
    MaterialComponent material{};
    bool hasCanvas=false;
    CanvasComponent canvas{};
    bool hasSprite=false;
    SpriteComponent sprite{};
    bool hasParticle=false;
    ParticleComponent particle{};
};

/// @brief Generic scene entity storage used by the editor/runtime migration.
/// @details Existing typed scenes remain supported while new scenes can use this
///          world for hierarchy, transform editing, and asset references.
class EntityWorld final {
public:
    static constexpr EntityId MaximumEntityId=9007199254740991ULL;
    // One-past allocated IDs, possibly MaximumEntityId + 1 (exhausted).
    // Candidate allocators must also inspect current/reserved record identities.
    EntityId AllocationHighWatermark() const { return nextId_; }
    // Candidate reconstruction must not recycle IDs allocated earlier in this
    // live World. Explicit Restore/Undo/load retain their existing semantics.
    void RetainAllocationHighWatermark(const EntityWorld& previous) {
        if(nextId_<previous.nextId_)nextId_=previous.nextId_;
    }
    // Transaction publication exchanges storage and allocator without moving
    // owning EntityRecord fields. Both Worlds' record pointers are invalidated.
    void Swap(EntityWorld& other) noexcept {
        entities_.swap(other.entities_);
        const auto previous=nextId_;nextId_=other.nextId_;other.nextId_=previous;
    }
    // Returns 0 without mutation on identity exhaustion/invalid allocation data.
    // Missing parents retain the legacy root-parent fallback.
    EntityId CreateEntity(std::string name, EntityId parent = 0);
    bool DestroyEntity(EntityId id);
    // Atomic authored copy; new root uses destinationParent, descendants remap
    // to fresh IDs. Asset identities are shared, not copied on disk.
    // Success invalidates pointers into entity storage; failure leaves it intact.
    bool DuplicateSubtree(EntityId source, EntityId destinationParent, EntityId& createdRoot,
        std::string& error, size_t maximumClones = 10000);
    void Clear();

    EntityRecord* Find(EntityId id);
    const EntityRecord* Find(EntityId id) const;
    std::vector<EntityRecord>& GetEntities() { return entities_; }
    const std::vector<EntityRecord>& GetEntities() const { return entities_; }
    bool GetWorldMatrix(EntityId id, Matrix4x4& result) const;
    // Read-only TRS candidate override; hierarchy links still come from the live
    // World. The override must identify a live Entity, possibly an ancestor.
    bool GetWorldMatrix(EntityId id, Matrix4x4& result, const EntityRecord& transformOverride) const;
    // Multiple read-only TRS candidates; duplicate/missing IDs reject without
    // changing result. Parent links always come from live storage.
    bool GetWorldMatrix(EntityId id, Matrix4x4& result, std::span<const EntityTransformOverride> overrides) const;
    // Lowest-id primary camera with active ancestors and a valid rigid frame.
    // Camera scale/shear is removed; composed world position is retained.
    bool GetPrimaryCamera(EntityId& id, Matrix4x4& rigidWorld) const;

    bool SetParent(EntityId id, EntityId parent);
    // Preserve the composed World matrix when representable as local TRS;
    // reject shear/singular/unstable results without changing any record.
    bool SetParent(EntityId id,EntityId parent,bool keepWorld,std::string& error);
    bool SetModel(EntityId id, std::string assetPath, std::string assetGuid = {});
    void RemoveModel(EntityId id);
    bool SetCamera(EntityId id, const CameraComponent& camera);
    void RemoveCamera(EntityId id);
    bool SetCollider(EntityId id, const Physics::ColliderComponent& collider);
    void RemoveCollider(EntityId id);
    bool SetRigidBody(EntityId id, const Physics::RigidBodyComponent& rigidBody);
    void RemoveRigidBody(EntityId id);
    bool SetScript(EntityId id, std::string assetPath, bool enabled = true, std::string assetGuid = {});
    void RemoveScript(EntityId id);
    bool SetAudio(EntityId id,const AudioSystem::AudioComponent& audio);
    void RemoveAudio(EntityId id);
    // Candidate preparation completes before published fields are swapped.
    bool SetAnimator(EntityId id, const AnimationSystem::AnimatorComponent& animator);
    // Keep dormant draft settings in memory; omitted from Capture while absent.
    void RemoveAnimator(EntityId id);
    bool SetLight(EntityId id,const LightComponent& light);
    void RemoveLight(EntityId id);
    bool SetMaterial(EntityId id,const MaterialComponent& material);
    void RemoveMaterial(EntityId id);
    bool SetCanvas(EntityId id,const CanvasComponent& canvas);
    void RemoveCanvas(EntityId id);
    bool SetSprite(EntityId id,const SpriteComponent& sprite);
    void RemoveSprite(EntityId id);
    bool SetParticle(EntityId id,const ParticleComponent& particle);
    void RemoveParticle(EntityId id);
    bool SetScriptProperty(EntityId id, std::string name, Scripting::ScriptProperty value);
    bool RemoveScriptProperty(EntityId id, const std::string& name);

    SceneValue Capture() const;
    bool Restore(const SceneValue& value, std::string& error);
    bool Save(const std::filesystem::path& path, std::string& error, bool replaceExisting=true) const;
    bool Load(const std::filesystem::path& path, std::string& error);

private:
    bool ComposeWorldMatrix(EntityId id, Matrix4x4& result, std::span<const EntityTransformOverride> overrides) const;
    EntityRecord* FindMutable(EntityId id);
    bool IsDescendant(EntityId candidate, EntityId ancestor) const;

    std::vector<EntityRecord> entities_;
    EntityId nextId_ = 1;
};

} // namespace Engine::Scene
