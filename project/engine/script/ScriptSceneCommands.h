#pragma once
#include "ScriptSceneView.h"
#include "ScriptSceneStructure.h"
#include "ScriptAssetView.h"
#include "ScriptAnimatorRequests.h"
#include "../scene/entity/EntityWorld.h"
#include "../physics/PhysicsWorld.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <type_traits>

namespace Engine::Scripting {
// Runtime-thread, hook-local staging. Scalar hooks remain allocation-free;
// structural operations lazily promote into an owning World candidate.
// Prepare must precede Audio admission; PublishPrepared follows with no callback,
// reentry or World mutation in between. Lua integration owns hook success/failure.
class ScriptSceneCommands final {
public:
    static constexpr size_t MaxOperations=64;
    enum class Operation { Position,Translate,Rotation,Scale,Active,Velocity };
    enum class Result { Success,Missing,Invalid,Unavailable,Limit,AllocationFailure };
    void Begin(const ScriptSceneView* view,Scene::EntityRecord& self) noexcept {
        Clear();view_=view;self_=&self;
        if(view){world_=view->world_;writable_=view->writeWorld_;token_=view->scopeToken_;}
        detached_=world_ && world_->Find(self.id)!=&self;
    }
    void Clear() noexcept {structure_.Clear();prefabWork_={};queryWork_={};view_=nullptr;world_=nullptr;writable_=nullptr;self_=nullptr;token_=0;count_=calls_=0;prepared_=detached_=structural_=failed_=false;selfFields_=0;}
    size_t OperationCount() const noexcept {return calls_;}
    size_t TargetCount() const noexcept {return count_;}
    bool Writable() const noexcept {return Context() && writable_ && token_ && detached_ && !failed_ && (!structural_ || structure_.Candidate());}
    const Scene::EntityWorld* ReadWorld() const noexcept {
        return Context() && !failed_ ? (structural_ ? structure_.Candidate() : world_) : nullptr;
    }
    bool HasStructuralChanges() const noexcept {return structural_ && structure_.OperationCount()!=0;}
    bool ReplacesWorld() const noexcept {return structural_;}
    size_t CreatedCount() const noexcept {return structural_ ? structure_.CreatedCount() : 0;}
    const ScriptPrefabWorkBudget::Usage& PrefabAdmissionUsage() const noexcept {return prefabWork_.GetUsage();}
    const Physics::PhysicsQueryBudget& PhysicsQueryUsage() const noexcept {return queryWork_;}
    Result Raycast(Vector3 origin,Vector3 direction,float maximumDistance,Physics::PhysicsRayHit& output,Physics::PhysicsQueryFilter filter={}) noexcept {
        const auto* read=ReadWorld();if(!read || prepared_)return Result::Unavailable;
        if(read->GetEntities().size()>Physics::PhysicsQueryBudget::MaxEntities){failed_=true;queryWork_.exhausted=true;return Result::Limit;}
        std::array<Physics::PhysicsQueryOverride,MaxOperations+1> overrides{};const auto used=QueryOverrides(*read,overrides);
        return QueryResult(Physics::PhysicsWorld::Raycast(*read,origin,direction,maximumDistance,output,filter,{overrides.data(),used},&queryWork_));
    }
    Result OverlapBox(Vector3 center,Vector3 half,std::vector<Scene::EntityId>& output,Physics::PhysicsQueryFilter filter={}) noexcept {
        const auto* read=ReadWorld();if(!read || prepared_)return Result::Unavailable;
        if(read->GetEntities().size()>Physics::PhysicsQueryBudget::MaxEntities){failed_=true;queryWork_.exhausted=true;return Result::Limit;}
        std::array<Physics::PhysicsQueryOverride,MaxOperations+1> overrides{};const auto used=QueryOverrides(*read,overrides);
        return QueryResult(Physics::PhysicsWorld::OverlapBox(*read,center,half,output,filter,{overrides.data(),used},&queryWork_));
    }
    const AnimationSystem::AnimatorComponent* Animator(Scene::EntityId id) const noexcept {
        const auto* record=Live(id);return record && record->hasAnimator ? &record->animator : nullptr;
    }
    const Scene::LightComponent* Light(Scene::EntityId id) const noexcept {const auto* record=Live(id);return record && record->hasLight?&record->light:nullptr;}
    const Scene::MaterialComponent* Material(Scene::EntityId id) const noexcept {const auto* record=Live(id);return record && record->hasMaterial?&record->material:nullptr;}
    const Scene::CanvasComponent* Canvas(Scene::EntityId id) const noexcept {const auto* record=Live(id);return record && record->hasCanvas?&record->canvas:nullptr;}
    const Scene::SpriteComponent* Sprite(Scene::EntityId id) const noexcept {const auto* record=Live(id);return record && record->hasSprite?&record->sprite:nullptr;}
    // Borrowed/trivial input keeps owning strings out of Lua's longjmp frames.
    struct SpriteInput {
        std::string_view asset,guid,buttonSceneGuid,text;
        Vector2 size{100,100},pivot{0.5f,0.5f};Vector4 color{1,1,1,1};
        bool enabled=true,flipX=false,flipY=false;int sortOrder=0;
        Vector2 anchor{0,0};
        float textSize=32;
        Scene::UITextAlignment textAlignment=Scene::UITextAlignment::Center;
        Vector4 textColor{1,1,1,1};
    };
    static_assert(std::is_trivially_destructible_v<SpriteInput>);
    Result SetCanvas(Scene::EntityId id,const Scene::CanvasComponent& value) noexcept {
        if(!id || id>Scene::EntityWorld::MaximumEntityId || !value.IsValid())return Result::Invalid;
        if(!Writable() || prepared_)return Result::Unavailable;
        if(!Live(id))return Result::Missing;
        if(calls_==MaxOperations)return Result::Limit;
        const auto promotion=Promote();if(promotion!=Result::Success)return promotion;
        if(!structure_.candidate_->SetCanvas(id,value)){failed_=true;return Result::Invalid;}
        if(self_->id==id){self_->canvas=value;self_->hasCanvas=true;}++calls_;return Result::Success;
    }
    Result SetSprite(Scene::EntityId id,const SpriteInput& input) noexcept {
        if(!id || id>Scene::EntityWorld::MaximumEntityId || !Scene::UIComponentDetail::AssetPath(input.asset) || !Scene::UIComponentDetail::Guid(input.guid))return Result::Invalid;
        if(!Writable() || prepared_)return Result::Unavailable;
        if(!Live(id))return Result::Missing;
        if(calls_==MaxOperations)return Result::Limit;
        try {
            Scene::SpriteComponent value;value.assetPath=input.asset;value.assetGuid=input.guid;value.size=input.size;value.pivot=input.pivot;value.color=input.color;
            value.enabled=input.enabled;value.flipX=input.flipX;value.flipY=input.flipY;value.sortOrder=input.sortOrder;
            value.anchor=input.anchor;
            value.buttonSceneGuid=input.buttonSceneGuid;
            value.text=input.text;value.textSize=input.textSize;value.textAlignment=input.textAlignment;value.textColor=input.textColor;
            if(!value.IsValid())return Result::Invalid;
            if(!value.buttonSceneGuid.empty()){
                if(!view_->assets_)return Result::Unavailable;
                if(!view_->assets_->ButtonDestination(value.buttonSceneGuid)){failed_=true;return Result::Invalid;}
            }
            if(!input.asset.empty() || !input.guid.empty()){
                if(!view_->assets_)return Result::Unavailable;
                ScriptPrefabAdmission::Reference reference;
                const auto admission=view_->assets_->Sprite(input.asset,input.guid,reference,&prefabWork_);
                if(admission!=ScriptAssetView::Result::Success){failed_=true;return admission==ScriptAssetView::Result::AllocationFailure?Result::AllocationFailure:admission==ScriptAssetView::Result::Limit?Result::Limit:Result::Invalid;}
                value.assetPath.swap(reference.hint);value.assetGuid.swap(reference.guid);
                if(!value.IsValid()){failed_=true;return Result::Invalid;}
            }
            Scene::SpriteComponent preparedSelf;if(self_->id==id)preparedSelf=value;
            const auto promotion=Promote();if(promotion!=Result::Success)return promotion;
            if(!structure_.candidate_->SetSprite(id,value)){failed_=true;return Result::AllocationFailure;}
            static_assert(std::is_nothrow_move_assignable_v<Scene::SpriteComponent>);
            if(self_->id==id){self_->sprite=std::move(preparedSelf);self_->hasSprite=true;}++calls_;return Result::Success;
        }catch(...){failed_=true;return Result::AllocationFailure;}
    }
    Result RemoveCanvas(Scene::EntityId id) noexcept {
        if(!id || id>Scene::EntityWorld::MaximumEntityId)return Result::Invalid;
        if(!Writable() || prepared_)return Result::Unavailable;
        const auto* record=Live(id);if(!record || !record->hasCanvas)return Result::Missing;
        if(calls_==MaxOperations)return Result::Limit;
        const auto promotion=Promote();if(promotion!=Result::Success)return promotion;
        structure_.candidate_->RemoveCanvas(id);if(self_->id==id)self_->hasCanvas=false;++calls_;return Result::Success;
    }
    Result RemoveSprite(Scene::EntityId id) noexcept {
        if(!id || id>Scene::EntityWorld::MaximumEntityId)return Result::Invalid;
        if(!Writable() || prepared_)return Result::Unavailable;
        const auto* record=Live(id);if(!record || !record->hasSprite)return Result::Missing;
        if(calls_==MaxOperations)return Result::Limit;
        const auto promotion=Promote();if(promotion!=Result::Success)return promotion;
        structure_.candidate_->RemoveSprite(id);if(self_->id==id)self_->hasSprite=false;++calls_;return Result::Success;
    }
    Result SetLight(Scene::EntityId id,const Scene::LightComponent& value) noexcept {
        if(!id || id>Scene::EntityWorld::MaximumEntityId || !value.IsValid())return Result::Invalid;
        if(!Writable() || prepared_)return Result::Unavailable;
        if(!Live(id))return Result::Missing;
        if(calls_==MaxOperations)return Result::Limit;
        const auto promotion=Promote();if(promotion!=Result::Success)return promotion;
        if(!structure_.candidate_->SetLight(id,value)){failed_=true;return Result::Invalid;}
        if(self_->id==id){self_->hasLight=true;self_->light=value;}++calls_;return Result::Success;
    }
    Result SetMaterial(Scene::EntityId id,const Scene::MaterialComponent& value) noexcept {
        if(!id || id>Scene::EntityWorld::MaximumEntityId || !value.IsValid())return Result::Invalid;
        if(!Writable() || prepared_)return Result::Unavailable;
        if(!Live(id))return Result::Missing;
        if(calls_==MaxOperations)return Result::Limit;
        const auto promotion=Promote();if(promotion!=Result::Success)return promotion;
        if(!structure_.candidate_->SetMaterial(id,value)){failed_=true;return Result::Invalid;}
        if(self_->id==id){self_->hasMaterial=true;self_->material=value;}++calls_;return Result::Success;
    }
    Result RemoveLight(Scene::EntityId id) noexcept {
        if(!id || id>Scene::EntityWorld::MaximumEntityId)return Result::Invalid;
        if(!Writable() || prepared_)return Result::Unavailable;
        const auto* record=Live(id);if(!record || !record->hasLight)return Result::Missing;
        if(calls_==MaxOperations)return Result::Limit;
        const auto promotion=Promote();if(promotion!=Result::Success)return promotion;
        structure_.candidate_->RemoveLight(id);if(self_->id==id)self_->hasLight=false;++calls_;return Result::Success;
    }
    Result RemoveMaterial(Scene::EntityId id) noexcept {
        if(!id || id>Scene::EntityWorld::MaximumEntityId)return Result::Invalid;
        if(!Writable() || prepared_)return Result::Unavailable;
        const auto* record=Live(id);if(!record || !record->hasMaterial)return Result::Missing;
        if(calls_==MaxOperations)return Result::Limit;
        const auto promotion=Promote();if(promotion!=Result::Success)return promotion;
        structure_.candidate_->RemoveMaterial(id);if(self_->id==id)self_->hasMaterial=false;++calls_;return Result::Success;
    }
    const Physics::ColliderComponent* Collider(Scene::EntityId id) const noexcept {
        const auto* record=Live(id);return record && record->hasCollider ? &record->collider : nullptr;
    }
    bool RigidBody(Scene::EntityId id,Physics::RigidBodyComponent& output) const noexcept {
        const auto* record=Live(id);if(!record || !record->hasRigidBody)return false;
        const auto* velocity=Velocity(id);if(!velocity)return false;
        output=record->rigidBody;output.velocity=*velocity;return true;
    }
    Result SetCollider(Scene::EntityId id,const Physics::ColliderComponent& value) noexcept {
        if(!id || id>Scene::EntityWorld::MaximumEntityId || !value.HasValidShape() ||
            !std::isfinite(value.friction) || value.friction<0 || value.friction>1 ||
            !std::isfinite(value.restitution) || value.restitution<0 || value.restitution>1)return Result::Invalid;
        if(!Writable() || prepared_)return Result::Unavailable;
        if(!Live(id))return Result::Missing;
        if(calls_==MaxOperations)return Result::Limit;
        const auto promotion=Promote();if(promotion!=Result::Success)return promotion;
        if(!structure_.candidate_->SetCollider(id,value)){failed_=true;return Result::Invalid;}
        if(self_->id==id){self_->hasCollider=true;self_->collider=value;}
        ++calls_;return Result::Success;
    }
    Result RemoveCollider(Scene::EntityId id) noexcept {
        if(!id || id>Scene::EntityWorld::MaximumEntityId)return Result::Invalid;
        if(!Writable() || prepared_)return Result::Unavailable;
        const auto* record=Live(id);if(!record || !record->hasCollider)return Result::Missing;
        if(calls_==MaxOperations)return Result::Limit;
        const auto promotion=Promote();if(promotion!=Result::Success)return promotion;
        structure_.candidate_->RemoveCollider(id);
        if(self_->id==id)self_->hasCollider=false;
        ++calls_;return Result::Success;
    }
    // Full settings replacement, not a partial patch. Grounded is solver-owned;
    // explicit configuration clears it. Velocity is world-space, as in PhysicsWorld.
    Result SetRigidBody(Scene::EntityId id,Physics::RigidBodyComponent value) noexcept {
        if(!id || id>Scene::EntityWorld::MaximumEntityId || !Finite(value.velocity) ||
            !std::isfinite(value.mass) || value.mass<=0)return Result::Invalid;
        if(!Writable() || prepared_)return Result::Unavailable;
        if(!Live(id))return Result::Missing;
        if(calls_==MaxOperations)return Result::Limit;
        const auto promotion=Promote();if(promotion!=Result::Success)return promotion;
        value.grounded=false;
        if(!structure_.candidate_->SetRigidBody(id,value)){failed_=true;return Result::Invalid;}
        // Prior scalar patches must not overwrite this later full replacement.
        if(self_->id==id){self_->hasRigidBody=true;self_->rigidBody=value;}
        else if(const auto* patch=Find(id))patches_[static_cast<size_t>(patch-patches_.data())].velocity=value.velocity;
        ++calls_;return Result::Success;
    }
    Result RemoveRigidBody(Scene::EntityId id) noexcept {
        if(!id || id>Scene::EntityWorld::MaximumEntityId)return Result::Invalid;
        if(!Writable() || prepared_)return Result::Unavailable;
        const auto* record=Live(id);if(!record || !record->hasRigidBody)return Result::Missing;
        if(calls_==MaxOperations)return Result::Limit;
        const auto promotion=Promote();if(promotion!=Result::Success)return promotion;
        structure_.candidate_->RemoveRigidBody(id);
        if(self_->id==id){self_->hasRigidBody=false;selfFields_&=~VelocityField;}
        else if(const auto* patch=Find(id))patches_[static_cast<size_t>(patch-patches_.data())].fields&=~VelocityField;
        ++calls_;return Result::Success;
    }
    Result QueueAnimator(Scene::EntityId id,AnimationSystem::AnimatorCommand command,ScriptAnimatorBatch& batch) noexcept {
        if(!id || id>Scene::EntityWorld::MaximumEntityId || static_cast<unsigned>(command)>3)return Result::Invalid;
        if(!Writable() || prepared_)return Result::Unavailable;
        const auto* record=Live(id);bool active=false;
        if(!record || !record->hasModel || !record->hasAnimator || !Active(id,active) || !active)return Result::Missing;
        if(calls_==MaxOperations)return Result::Limit;
        if(!record->animator.IsValid())return Result::Invalid;
        if(!batch.Append(id,command,record->animator))return Result::Limit;
        ++calls_;return Result::Success;
    }
    Result QueueController(Scene::EntityId id,ScriptAnimatorRequestKind kind,std::string_view name,double number,bool flag,ScriptAnimatorBatch& batch) noexcept {
        if(!id || id>Scene::EntityWorld::MaximumEntityId || kind==ScriptAnimatorRequestKind::Playback || static_cast<unsigned>(kind)>3 ||
            !ValidScriptText(name,128,true) || (kind==ScriptAnimatorRequestKind::Float &&
            (!std::isfinite(number) || std::abs(number)>(std::numeric_limits<float>::max)())))return Result::Invalid;
        if(!Writable() || prepared_)return Result::Unavailable;
        const auto* record=Live(id);bool active=false;
        if(!record || !record->hasModel || !record->hasAnimator || record->animator.controllerGuid.empty() || !Active(id,active) || !active)return Result::Missing;
        if(calls_==MaxOperations)return Result::Limit;
        if(!record->animator.IsValid())return Result::Invalid;
        if(!batch.AppendController(id,kind,name,number,flag,record->animator))return Result::Limit;
        ++calls_;return Result::Success;
    }
    Result QueueLayerWeight(Scene::EntityId id,size_t layer,double weight,ScriptAnimatorBatch& batch) noexcept {
        if(!id || id>Scene::EntityWorld::MaximumEntityId || layer>=4 || !std::isfinite(weight) || weight<0 || weight>1)return Result::Invalid;
        if(!Writable() || prepared_)return Result::Unavailable;
        const auto* record=Live(id);bool active=false;
        if(!record || !record->hasModel || !record->hasAnimator || record->animator.controllerGuid.empty() || !Active(id,active) || !active)return Result::Missing;
        if(calls_==MaxOperations)return Result::Limit;
        if(!record->animator.IsValid())return Result::Invalid;
        if(!batch.AppendLayerWeight(id,layer,weight,record->animator))return Result::Limit;
        ++calls_;return Result::Success;
    }
    Result QueueForceState(Scene::EntityId id,size_t layer,std::string_view name,double duration,ScriptAnimatorBatch& batch) noexcept {
        if(!id || id>Scene::EntityWorld::MaximumEntityId || layer>=4 || !ValidScriptText(name,128,true) || !std::isfinite(duration) || duration<0 || duration>60)return Result::Invalid;
        if(!Writable() || prepared_)return Result::Unavailable;
        const auto* record=Live(id);bool active=false;
        if(!record || !record->hasModel || !record->hasAnimator || record->animator.controllerGuid.empty() || !Active(id,active) || !active)return Result::Missing;
        if(calls_==MaxOperations)return Result::Limit;
        if(!record->animator.IsValid())return Result::Invalid;
        if(!batch.AppendForceState(id,layer,name,duration,record->animator))return Result::Limit;
        ++calls_;return Result::Success;
    }
    // Owning settings use the existing private World promotion, never a string
    // copy after Audio admission. Queries/Clone see these staged values immediately.
    Result SetAnimator(Scene::EntityId id,std::string_view clip,double speed,bool loop,bool playOnAwake) noexcept {
        if(!id || id>Scene::EntityWorld::MaximumEntityId || !ValidScriptText(clip,256,true) ||
            !std::isfinite(speed) || speed<0 || speed>(std::numeric_limits<float>::max)())return Result::Invalid;
        if(!Writable() || prepared_)return Result::Unavailable;
        if(!Live(id))return Result::Missing;
        if(calls_==MaxOperations)return Result::Limit;
        try {
            AnimationSystem::AnimatorComponent settings;settings.clip=clip;settings.speed=static_cast<float>(speed);
            settings.loop=loop;settings.playOnAwake=playOnAwake;
            const auto promotion=Promote();if(promotion!=Result::Success)return promotion;
            if(!structure_.candidate_->SetAnimator(id,settings)){failed_=true;return Result::AllocationFailure;}
            ++calls_;return Result::Success;
        } catch(...) {failed_=true;return Result::AllocationFailure;}
    }
    Result RemoveAnimator(Scene::EntityId id) noexcept {
        if(!id || id>Scene::EntityWorld::MaximumEntityId)return Result::Invalid;
        if(!Writable() || prepared_)return Result::Unavailable;
        const auto* record=Live(id);if(!record || !record->hasAnimator)return Result::Missing;
        if(calls_==MaxOperations)return Result::Limit;
        const auto promotion=Promote();if(promotion!=Result::Success)return promotion;
        structure_.candidate_->RemoveAnimator(id);++calls_;return Result::Success;
    }
    Result SpawnModel(std::string_view name,std::string_view hint,std::string_view guid,Scene::EntityId parent,Scene::EntityId& created) noexcept {
        if(!ValidScriptText(name,ScriptSceneStructure::MaxNameBytes,true) || parent>Scene::EntityWorld::MaximumEntityId)return Result::Invalid;
        if(!Writable() || prepared_ || !view_->assets_)return Result::Unavailable;
        if(parent && !Live(parent))return Result::Missing;
        if(calls_==MaxOperations)return Result::Limit;
        if(!CreationCapacity())return Result::Limit;
        Scene::ModelComponent model;
        const auto admission=view_->assets_->Model(hint,guid,model);
        if(admission!=ScriptAssetView::Result::Success){
            failed_=true;
            return admission==ScriptAssetView::Result::AllocationFailure ? Result::AllocationFailure : Result::Invalid;
        }
        const auto promotion=Promote();if(promotion!=Result::Success)return promotion;
        const auto result=static_cast<Result>(structure_.SpawnModel(name,model,parent,created));
        if(result==Result::Success)++calls_;
        if(result==Result::AllocationFailure)failed_=true;
        return result;
    }
    Result SpawnPrefab(std::string_view hint,std::string_view guid,Scene::EntityId parent,Scene::EntityId& created) noexcept {
        if(parent>Scene::EntityWorld::MaximumEntityId)return Result::Invalid;
        if(!Writable() || prepared_ || !view_->assets_)return Result::Unavailable;
        if(parent && !Live(parent))return Result::Missing;
        if(calls_==MaxOperations)return Result::Limit;
        const auto capacity=CreationCapacity();if(!capacity)return Result::Limit;
        Prefab::PrefabValue definition;const auto admission=view_->assets_->Prefab(hint,guid,definition,&prefabWork_,capacity);
        if(admission==ScriptAssetView::Result::Limit){if(prefabWork_.GetUsage().exceeded)failed_=true;return Result::Limit;}
        if(admission!=ScriptAssetView::Result::Success){failed_=true;return admission==ScriptAssetView::Result::AllocationFailure?Result::AllocationFailure:Result::Invalid;}
        const auto promotion=Promote();if(promotion!=Result::Success)return promotion;
        if(!SyncStructure()){failed_=true;return Result::Invalid;}
        const auto result=static_cast<Result>(structure_.SpawnPrefab(definition,parent,created));
        if(result==Result::Success)++calls_;
        if(result==Result::Invalid || result==Result::AllocationFailure)failed_=true;
        return result;
    }
    Result Clone(Scene::EntityId source,Scene::EntityId parent,Scene::EntityId& created) noexcept {
        if(!source || source>Scene::EntityWorld::MaximumEntityId || parent>Scene::EntityWorld::MaximumEntityId)return Result::Invalid;
        if(!Writable() || prepared_)return Result::Unavailable;
        if(!Live(source) || (parent && !Live(parent)))return Result::Missing;
        if(calls_==MaxOperations)return Result::Limit;
        const auto promotion=Promote();if(promotion!=Result::Success)return promotion;
        // Source may have changed through e: or scalar commands after the first
        // promotion. Clone must capture the latest values, not stale storage.
        if(!SyncStructure()){failed_=true;return Result::Invalid;}
        const auto result=static_cast<Result>(structure_.Clone(source,parent,created));
        if(result==Result::Success)++calls_;
        if(result==Result::AllocationFailure)failed_=true;
        return result;
    }
    Result Spawn(std::string_view name,Scene::EntityId parent,Scene::EntityId& created) noexcept {
        if(!ValidScriptText(name,ScriptSceneStructure::MaxNameBytes,true) || parent>Scene::EntityWorld::MaximumEntityId)return Result::Invalid;
        if(!Writable() || prepared_)return Result::Unavailable;
        if(parent && !Live(parent))return Result::Missing;
        if(calls_==MaxOperations)return Result::Limit;
        const auto promotion=Promote();if(promotion!=Result::Success)return promotion;
        const auto result=static_cast<Result>(structure_.Spawn(name,parent,created));
        if(result==Result::Success)++calls_;
        if(result==Result::AllocationFailure)failed_=true;
        return result;
    }
    Result Destroy(Scene::EntityId id,bool subtree=false) noexcept {
        if(!id || id>Scene::EntityWorld::MaximumEntityId)return Result::Invalid;
        if(!Writable() || prepared_)return Result::Unavailable;
        if(!Live(id))return Result::Missing;
        if(calls_==MaxOperations)return Result::Limit;
        const auto promotion=Promote();if(promotion!=Result::Success)return promotion;
        const auto result=static_cast<Result>(structure_.Destroy(id,subtree));
        if(result==Result::Success)++calls_;
        if(result==Result::AllocationFailure)failed_=true;
        return result;
    }
    const Scene::TransformComponent* Transform(Scene::EntityId id) const noexcept {
        const auto* live=Live(id);if(!live)return nullptr;
        if(self_->id==id)return &self_->transform;
        const auto* patch=Find(id);return patch ? &patch->transform : &live->transform;
    }
    bool Active(Scene::EntityId id,bool& result) const noexcept {
        const auto* live=Live(id);if(!live)return false;
        if(self_->id==id)result=self_->active;
        else if(const auto* patch=Find(id))result=patch->active;
        else result=live->active;
        return true;
    }
    const Vector3* Velocity(Scene::EntityId id) const noexcept {
        const auto* live=Live(id);if(!live || !live->hasRigidBody)return nullptr;
        if(self_->id==id)return self_->hasRigidBody ? &self_->rigidBody.velocity : nullptr;
        const auto* patch=Find(id);return patch ? &patch->velocity : &live->rigidBody.velocity;
    }
    bool WorldMatrix(Scene::EntityId id,Matrix4x4& result) const {
        const auto* read=ReadWorld();if(!read)return false;
        std::array<Scene::EntityTransformOverride,MaxOperations+1> transforms{};size_t used=0;
        if(read->Find(self_->id))transforms[used++]={self_->id,self_->transform};
        for(size_t index=0;index<count_;++index)if(read->Find(patches_[index].id))transforms[used++]={patches_[index].id,patches_[index].transform};
        Matrix4x4 candidate{};
        if(!read->GetWorldMatrix(id,candidate,{transforms.data(),used}))return false;
        for(const auto& row:candidate.m)for(float value:row)if(!std::isfinite(value))return false;
        result=candidate;return true;
    }
    Result Apply(Scene::EntityId id,Operation operation,Vector3 value={},bool active=false) noexcept {
        if(!id || id>Scene::EntityWorld::MaximumEntityId ||
            static_cast<unsigned>(operation)>static_cast<unsigned>(Operation::Velocity) ||
            (operation!=Operation::Active && !Finite(value)))return Result::Invalid;
        if(!Writable() || prepared_)return Result::Unavailable;
        const auto* live=Live(id);if(!live)return Result::Missing;
        const auto* record=self_->id==id ? self_ : live;
        if(operation==Operation::Velocity && !record->hasRigidBody)return Result::Missing;
        if(calls_==MaxOperations)return Result::Limit;
        const auto* existing=Find(id);
        Patch candidate=existing ? *existing : Patch{id,record->transform,record->rigidBody.velocity,record->active,0};
        const unsigned field=operation==Operation::Translate ? PositionField :
            operation==Operation::Position ? PositionField : operation==Operation::Rotation ? RotationField :
            operation==Operation::Scale ? ScaleField : operation==Operation::Active ? ActiveField : VelocityField;
        switch(operation){
        case Operation::Position:candidate.transform.position=value;break;
        case Operation::Translate:candidate.transform.position+=value;if(!Finite(candidate.transform.position))return Result::Invalid;break;
        case Operation::Rotation:candidate.transform.rotation=value;break;
        case Operation::Scale:candidate.transform.scale=value;break;
        case Operation::Active:candidate.active=active;break;
        case Operation::Velocity:candidate.velocity=value;break;
        }
        candidate.fields|=field;
        if(self_->id==id){Write(*self_,candidate);selfFields_|=field;}
        else if(existing)patches_[static_cast<size_t>(existing-patches_.data())]=candidate;
        else patches_[count_++]=candidate;
        ++calls_;return Result::Success;
    }
    bool Prepare() noexcept {
        prepared_=false;
        if(failed_)return false;
        // No write token is needed for a hook which never used command mutation.
        if(!calls_)return true;
        if(!Writable())return false;
        if(structural_){
            if(!SyncStructure())return false;
            prepared_=structure_.PrepareCandidate(true);
            if(!prepared_)failed_=true;
            return prepared_;
        }
        if(selfFields_){
            const auto* live=world_->Find(self_->id);
            if(!live || ((selfFields_&VelocityField) && (!live->hasRigidBody || !self_->hasRigidBody)) ||
                !Valid(Patch{self_->id,self_->transform,self_->rigidBody.velocity,self_->active,selfFields_}))return false;
        }
        for(size_t index=0;index<count_;++index){
            const auto& patch=patches_[index];auto* record=writable_->Find(patch.id);
            if(!record || ((patch.fields&VelocityField) && !record->hasRigidBody) || !Valid(patch))return false;
            preparedRecords_[index]=record;
        }
        prepared_=true;return true;
    }
    // Precondition: successful Prepare and no intervening host reentry/mutation.
    bool PublishPrepared() noexcept {
        if(!prepared_)return false;
        if(structural_){
            if(!Writable() || !structure_.PublishPrepared())return false;
            prepared_=false;return true;
        }
        // Reacquire every target before the first write. Expired scopes and
        // removed targets/components must never leave a partial publication.
        if(!Prepare())return false;
        for(size_t index=0;index<count_;++index)Write(*preparedRecords_[index],patches_[index]);
        prepared_=false;return true;
    }
private:
    size_t QueryOverrides(const Scene::EntityWorld& read,std::array<Physics::PhysicsQueryOverride,MaxOperations+1>& values) const noexcept {
        size_t used=0;if(read.Find(self_->id))values[used++]={self_->id,self_->transform,self_->active};
        for(size_t i=0;i<count_;++i)if(read.Find(patches_[i].id))values[used++]={patches_[i].id,patches_[i].transform,patches_[i].active};return used;
    }
    Result QueryResult(Physics::PhysicsQueryResult result) noexcept {
        if(result==Physics::PhysicsQueryResult::Success)return Result::Success;
        failed_=true;
        if(result==Physics::PhysicsQueryResult::Limit)return Result::Limit;
        if(result==Physics::PhysicsQueryResult::AllocationFailure)return Result::AllocationFailure;
        return Result::Invalid;
    }
    Result Promote() noexcept {
        if(structural_)return Result::Success;
        const auto result=static_cast<Result>(structure_.Begin(view_));
        if(result!=Result::Success){failed_=true;return result;}
        structural_=true;
        if(!SyncStructure()){failed_=true;return Result::Invalid;}
        return Result::Success;
    }
    bool SyncStructure() noexcept {
        if(!structure_.Candidate())return false;
        auto& candidate=*structure_.candidate_;
        // Never move owning self fields. Structural deletion of self/peer is
        // authoritative; retained scalar patches cannot resurrect those IDs.
        if(auto* record=candidate.Find(self_->id)){
            if(!Valid(Patch{self_->id,self_->transform,self_->rigidBody.velocity,self_->active,selfFields_}))return false;
            record->transform=self_->transform;record->active=self_->active;
            if(record->hasRigidBody)record->rigidBody.velocity=self_->rigidBody.velocity;
        }
        for(size_t index=0;index<count_;++index){
            const auto& patch=patches_[index];auto* record=candidate.Find(patch.id);
            if(!record)continue;
            if(!Valid(patch) || ((patch.fields&VelocityField) && !record->hasRigidBody))return false;
            Write(*record,patch);
        }
        return true;
    }
    static constexpr unsigned PositionField=1,RotationField=2,ScaleField=4,ActiveField=8,VelocityField=16;
    struct Patch {
        Scene::EntityId id;Scene::TransformComponent transform;Vector3 velocity;bool active;unsigned fields;
    };
    static_assert(std::is_trivially_copyable_v<Patch> && std::is_trivially_destructible_v<Patch>);
    static bool Finite(const Vector3& value) noexcept {return std::isfinite(value.x)&&std::isfinite(value.y)&&std::isfinite(value.z);}
    static bool Valid(const Patch& patch) noexcept {
        return (!(patch.fields&PositionField)||Finite(patch.transform.position)) &&
            (!(patch.fields&RotationField)||Finite(patch.transform.rotation)) &&
            (!(patch.fields&ScaleField)||Finite(patch.transform.scale)) &&
            (!(patch.fields&VelocityField)||Finite(patch.velocity));
    }
    static void Write(Scene::EntityRecord& record,const Patch& patch) noexcept {
        if(patch.fields&PositionField)record.transform.position=patch.transform.position;
        if(patch.fields&RotationField)record.transform.rotation=patch.transform.rotation;
        if(patch.fields&ScaleField)record.transform.scale=patch.transform.scale;
        if(patch.fields&ActiveField)record.active=patch.active;
        if(patch.fields&VelocityField)record.rigidBody.velocity=patch.velocity;
    }
    bool Context() const noexcept {return view_ && self_ && world_ && view_->world_==world_ && view_->writeWorld_==writable_ && view_->scopeToken_==token_;}
    size_t CreationCapacity() const noexcept {
        const auto* read=ReadWorld();if(!read)return 0;
        const auto entities=read->GetEntities().size(),created=CreatedCount();
        if(entities>=ScriptSceneStructure::MaxEntities || created>=ScriptSceneStructure::MaxOperations)return 0;
        return (std::min)(ScriptSceneStructure::MaxEntities-entities,ScriptSceneStructure::MaxOperations-created);
    }
    const Scene::EntityRecord* Live(Scene::EntityId id) const noexcept {const auto* read=ReadWorld();return read ? read->Find(id) : nullptr;}
    const Patch* Find(Scene::EntityId id) const noexcept {for(size_t index=0;index<count_;++index)if(patches_[index].id==id)return &patches_[index];return nullptr;}
    const ScriptSceneView* view_=nullptr;const Scene::EntityWorld* world_=nullptr;
    Scene::EntityWorld* writable_=nullptr;Scene::EntityRecord* self_=nullptr;
    uint64_t token_=0;size_t count_=0,calls_=0;bool prepared_=false,detached_=false;unsigned selfFields_=0;
    std::array<Patch,MaxOperations> patches_{};
    std::array<Scene::EntityRecord*,MaxOperations> preparedRecords_{};
    ScriptSceneStructure structure_;
    ScriptPrefabWorkBudget prefabWork_;
    Physics::PhysicsQueryBudget queryWork_;
    bool structural_=false,failed_=false;
};
}
