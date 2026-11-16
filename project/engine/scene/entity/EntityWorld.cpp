#include "EntityWorld.h"
#include "EntityTransformMatrix.h"
#include "TransformConversion.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace Engine::Scene {
namespace {

Matrix4x4 LocalMatrix(const TransformComponent& transform)
{
    return EntityLocalMatrix(transform);
}

SceneValue Vector(const Vector3& value)
{
    return crude_json::array{double(value.x), double(value.y), double(value.z)};
}

Vector3 ReadVector(const SceneValue& value, const char* field)
{
    if (!value.is_array() || value.get<crude_json::array>().size() != 3) {
        throw std::runtime_error(std::string(field) + " must be an array of three numbers");
    }
    const auto& values = value.get<crude_json::array>();
    for (const auto& number : values) {
        if (!number.is_number() || !std::isfinite(number.get<crude_json::number>()) ||
            std::abs(number.get<crude_json::number>()) > std::numeric_limits<float>::max()) {
            throw std::runtime_error(std::string(field) + " contains an invalid number");
        }
    }
    return {
        static_cast<float>(values[0].get<crude_json::number>()),
        static_cast<float>(values[1].get<crude_json::number>()),
        static_cast<float>(values[2].get<crude_json::number>()),
    };
}

uint64_t ReadId(const SceneValue& value, const char* field)
{
    if (!value.is_number()) {
        throw std::runtime_error(std::string(field) + " must be a number");
    }
    const double number = value.get<crude_json::number>();
    if (!std::isfinite(number) || number < 0.0 || std::floor(number) != number || number > double(EntityWorld::MaximumEntityId)) {
        throw std::runtime_error(std::string(field) + " contains an invalid entity id");
    }
    return static_cast<uint64_t>(number);
}

// Bound link JSON before recursive inspection or serializing its baseline.
void CheckPrefabValue(const SceneValue& value,size_t depth,size_t& structures,size_t& textBytes)
{
    if(depth>64 || ((value.is_object() || value.is_array()) && ++structures>65536))
        throw std::runtime_error("Prefab link structure/depth budget exceeded");
    const auto text=[&](const std::string& item){
        if(item.size()>SceneDocument::MaxBytes-textBytes || item.find('\0')!=std::string::npos ||
            (!item.empty() && !MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,item.data(),static_cast<int>(item.size()),nullptr,0)))
            throw std::runtime_error("Prefab link requires bounded UTF-8 without NUL");
        textBytes+=item.size();
    };
    if(value.is_string())text(value.get<crude_json::string>());
    if(value.is_object())for(const auto& [key,child]:value.get<crude_json::object>()){text(key);CheckPrefabValue(child,depth+1,structures,textBytes);}
    if(value.is_array())for(const auto& child:value.get<crude_json::array>())CheckPrefabValue(child,depth+1,structures,textBytes);
    if(value.is_discarded() || (value.is_number() && !std::isfinite(value.get<crude_json::number>())))throw std::runtime_error("Invalid Prefab link value");
}
EntityId PrefabId(const SceneValue& value)
{
    if(!value.is_number())throw std::runtime_error("Prefab identity must be numeric");
    const auto number=value.get<crude_json::number>();
    if(!std::isfinite(number) || number<1 || number>9007199254740991.0 || std::floor(number)!=number)
        throw std::runtime_error("Prefab identity must be nonzero and JSON-exact");
    return EntityId(number);
}
// Missing mapped Scene IDs are deletion overrides and remain reserved.
// Baselines cannot contain links, preventing recursive/nested deserialization.
EntityId CheckPrefabLinks(const std::vector<EntityRecord>& records)
{
    std::unordered_map<EntityId,const EntityRecord*> live;
    for(const auto& record:records)live.emplace(record.id,&record);
    std::unordered_set<EntityId> claimed,roots;
    for(const auto& record:records)if(!record.prefabLink.is_null())roots.insert(record.id);
    EntityId maximum=0;size_t structures=0,textBytes=0,totalBytes=0;
    for(const auto& record:records)if(!record.prefabLink.is_null()) {
        const auto& link=record.prefabLink;CheckPrefabValue(link,0,structures,textBytes);
        const auto bytes=link.dump().size();
        if(bytes>SceneDocument::MaxBytes-totalBytes)throw std::runtime_error("Prefab links exceed document budget");totalBytes+=bytes;
        if(!link.is_object() || link.get<crude_json::object>().size()!=4 || !link.contains("sourceGuid") || !link["sourceGuid"].is_string() ||
            !ModelComponent::IsValidGuid(link["sourceGuid"].get<crude_json::string>()) || !link.contains("rootLocal") || !link.contains("base") || !link.contains("mapping"))
            throw std::runtime_error("Invalid Prefab link schema/source GUID");
        const auto rootLocal=PrefabId(link["rootLocal"]);const auto& base=link["base"];
        if(!base.is_object() || base.get<crude_json::object>().size()!=2 || !base.contains("version") || !base.contains("entities") || !base["entities"].is_array() ||
            base["entities"].get<crude_json::array>().empty() || base["entities"].get<crude_json::array>().size()>10000)
            throw std::runtime_error("Invalid Prefab link baseline");
        for(const auto& node:base["entities"].get<crude_json::array>())if(!node.is_object() || node.contains("prefabLink"))throw std::runtime_error("Nested Prefab baseline links are unsupported");
        EntityWorld checked;std::string error;
        if(!checked.Restore(base,error))throw std::runtime_error("Prefab baseline: "+error);
        if(checked.Capture().dump()!=base.dump())throw std::runtime_error("Prefab baseline must use canonical supported fields");
        const auto* baseRoot=checked.Find(rootLocal);
        if(!baseRoot || baseRoot->parent)throw std::runtime_error("Invalid Prefab baseline root");
        for(const auto& node:checked.GetEntities())if(!node.parent && node.id!=rootLocal)throw std::runtime_error("Disconnected Prefab baseline");
        const auto& mapping=link["mapping"];
        if(!mapping.is_array() || mapping.get<crude_json::array>().size()!=checked.GetEntities().size())throw std::runtime_error("Prefab mapping must cover baseline identities");
        std::unordered_set<EntityId> localIds;
        for(const auto& pair:mapping.get<crude_json::array>()) {
            if(!pair.is_object() || pair.get<crude_json::object>().size()!=2 || !pair.contains("local") || !pair.contains("entity"))throw std::runtime_error("Invalid Prefab mapping pair");
            const auto local=PrefabId(pair["local"]),scene=PrefabId(pair["entity"]);
            if(!checked.Find(local) || !localIds.insert(local).second || !claimed.insert(scene).second)throw std::runtime_error("Repeated/unknown Prefab mapped identity");
            maximum=(std::max)(maximum,scene);
            if(local==rootLocal && scene!=record.id)throw std::runtime_error("Prefab root mapping mismatch");
            auto found=live.find(scene);
            if(found==live.end())continue;
            const auto* node=found->second;size_t depth=0;
            while(node->id!=record.id) {
                if(++depth>records.size() || !node->parent || !live.contains(node->parent))throw std::runtime_error("Prefab mapped Entity is outside its instance");
                node=live.at(node->parent);
            }
        }
        for(auto parent=record.parent;parent;parent=live.at(parent)->parent)if(roots.contains(parent))throw std::runtime_error("Nested Prefab instances are unsupported");
    }
    return maximum;
}

float ReadCameraNumber(const SceneValue& value, const char* field)
{
    if (!value.is_number() || !std::isfinite(value.get<crude_json::number>()) ||
        std::abs(value.get<crude_json::number>()) > std::numeric_limits<float>::max())
        throw std::runtime_error(std::string("Invalid camera ") + field);
    return static_cast<float>(value.get<crude_json::number>());
}

bool RigidCameraMatrix(const Matrix4x4& composed, Matrix4x4& result)
{
    for (const auto& row : composed.m) for (float number : row)
        if (!std::isfinite(number)) return false;
    // Normalize in double precision so very large/small valid scales do not overflow.
    double right[3], up[3], forward[3];
    const auto normalize = [](double* v) {
        const double length = std::sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
        if (!std::isfinite(length) || length == 0.0) return false;
        for (int i = 0; i < 3; ++i) v[i] /= length;
        return true;
    };
    for (int i = 0; i < 3; ++i) {
        right[i] = composed.m[0][i];
        up[i] = composed.m[1][i];
        forward[i] = composed.m[2][i];
    }
    if (!normalize(right) || !normalize(up) || !normalize(forward)) return false;
    const double dot = right[0]*forward[0] + right[1]*forward[1] + right[2]*forward[2];
    for (int i = 0; i < 3; ++i) right[i] -= dot * forward[i];
    const double lengthSquared = right[0]*right[0] + right[1]*right[1] + right[2]*right[2];
    if (lengthSquared < 1.0e-12 || !normalize(right)) return false;
    const double rigidUp[3] = {forward[1]*right[2] - forward[2]*right[1],
        forward[2]*right[0] - forward[0]*right[2], forward[0]*right[1] - forward[1]*right[0]};
    if (std::abs(up[0]*rigidUp[0] + up[1]*rigidUp[1] + up[2]*rigidUp[2]) < 1.0e-6) return false;
    result = {};
    for (int i = 0; i < 3; ++i) {
        result.m[0][i] = static_cast<float>(right[i]);
        result.m[1][i] = static_cast<float>(rigidUp[i]);
        result.m[2][i] = static_cast<float>(forward[i]);
        result.m[3][i] = composed.m[3][i];
    }
    result.m[3][3] = 1.0f;
    const auto view = result.Inverse();
    for (const auto& row : view.m) for (float number : row)
        if (!std::isfinite(number)) return false;
    return true;
}

} // namespace

bool CameraComponent::IsValid() const
{
    return std::isfinite(orthographicHeight) && orthographicHeight > 0.0f &&
        std::isfinite(2.0f / orthographicHeight) && orthographicHeight * 0.5f > 0.0f &&
        std::isfinite(fovY) && fovY > 0.0f && fovY < std::numbers::pi_v<float> &&
        std::isfinite(nearClip) && std::isfinite(farClip) && nearClip > 0.0f && farClip > nearClip &&
        std::isfinite(1.0f / std::tan(fovY * 0.5f)) &&
        std::isfinite(1.0f / (farClip - nearClip)) && std::isfinite(farClip / (farClip - nearClip)) &&
        std::isfinite(nearClip * farClip) && nearClip * farClip > 0.0f &&
        std::isfinite(nearClip * (farClip / (farClip - nearClip)));
}

EntityId EntityWorld::CreateEntity(std::string name, EntityId parent)
{
    // Observe exposed record edits and Prefab deletion tombstones, not just the
    // private high-water mark. Do not increment the allocator before publication.
    EntityId first=nextId_;
    const auto observe=[&](EntityId id){if(!id || id>MaximumEntityId)return false;first=(std::max)(first,id+1);return true;};
    for(const auto& entity:entities_) {
        if(!observe(entity.id))return 0;
        if(entity.prefabLink.is_null())continue;
        const auto& link=entity.prefabLink;
        if(!link.is_object() || !link.contains("mapping") || !link["mapping"].is_array() || link["mapping"].get<crude_json::array>().size()>10000)return 0;
        for(const auto& pair:link["mapping"].get<crude_json::array>()) {
            if(!pair.is_object() || !pair.contains("entity") || !pair["entity"].is_number())return 0;
            const double number=pair["entity"].get<crude_json::number>();
            if(!std::isfinite(number) || number<1 || number>double(MaximumEntityId) || std::floor(number)!=number || !observe(EntityId(number)))return 0;
        }
    }
    if(!first || first>MaximumEntityId)return 0;
    if (parent != 0 && !Find(parent)) {
        parent = 0;
    }
    EntityRecord record{};
    record.id = first;
    record.parent = parent;
    record.name = name.empty() ? "Entity" : std::move(name);
    entities_.push_back(std::move(record));
    nextId_=first+1;
    return entities_.back().id;
}

bool EntityWorld::DestroyEntity(EntityId id)
{
    if (!Find(id)) {
        return false;
    }
    for (auto& entity : entities_) {
        if (entity.parent == id) {
            entity.parent = 0;
        }
    }
    entities_.erase(std::remove_if(entities_.begin(), entities_.end(),
        [id](const EntityRecord& entity) { return entity.id == id; }), entities_.end());
    return true;
}

bool EntityWorld::DuplicateSubtree(EntityId source, EntityId destinationParent, EntityId& createdRoot,
    std::string& error, size_t maximumClones)
{
    try {
        if (!Find(source)) throw std::runtime_error("Duplicate source entity does not exist");
        if (destinationParent && !Find(destinationParent)) throw std::runtime_error("Duplicate destination parent does not exist");
        if (!maximumClones) throw std::runtime_error("Duplicate subtree exceeds entity budget");
        constexpr EntityId maximumExactId=MaximumEntityId;
        EntityId maximumId=0;
        std::unordered_map<EntityId,const EntityRecord*> records;
        std::unordered_map<EntityId,std::vector<EntityId>> children;
        for (const auto& record : entities_) {
            if (!record.id || record.id>maximumExactId || !records.emplace(record.id,&record).second)
                throw std::runtime_error("Duplicate requires unique JSON-exact entity IDs");
            maximumId=(std::max)(maximumId,record.id); children[record.parent].push_back(record.id);
        }
        // Validate even exposed mutable record edits before creating a candidate.
        EntityWorld checked;
        if (!checked.Restore(Capture(),error)) return false;
        std::vector<EntityId> order{source}; std::unordered_set<EntityId> visited{source};
        for (size_t index=0;index<order.size();++index) {
            const auto found=children.find(order[index]);
            if (found==children.end()) continue;
            for (const auto child : found->second) {
                if (order.size()>=maximumClones) throw std::runtime_error("Duplicate subtree exceeds entity budget");
                if (!visited.insert(child).second) throw std::runtime_error("Duplicate subtree contains a cycle");
                order.push_back(child);
            }
        }
        const EntityId first=(std::max)({nextId_,checked.nextId_,maximumId+1});
        if (first>maximumExactId || order.size()>maximumExactId-first+1)
            throw std::runtime_error("Duplicate has exhausted JSON-exact entity IDs");
        EntityWorld candidate=*this;
        candidate.entities_.reserve(candidate.entities_.size()+order.size());
        std::unordered_map<EntityId,EntityId> remapped;
        for (size_t index=0;index<order.size();++index) remapped.emplace(order[index],first+index);
        for (const auto id : order) {
            auto copy=*records.at(id);
            // Existing DuplicateSubtree remains a detached authored copy.
            // Linked duplication requires a fresh map including deletion tombstones.
            copy.prefabLink=SceneValue{};
            copy.id=remapped.at(id);
            copy.parent=id==source ? destinationParent : remapped.at(copy.parent);
            if (id==source) copy.name+=" Copy";
            candidate.entities_.push_back(std::move(copy));
        }
        candidate.nextId_=first+order.size();
        if (!checked.Restore(candidate.Capture(),error)) return false;
        *this=std::move(candidate);
        createdRoot=first; error.clear(); return true;
    } catch (const std::exception& exception) { error=exception.what(); return false; }
}

void EntityWorld::Clear()
{
    entities_.clear();
    nextId_ = 1;
}

EntityRecord* EntityWorld::Find(EntityId id)
{
    return FindMutable(id);
}

const EntityRecord* EntityWorld::Find(EntityId id) const
{
    const auto found = std::find_if(entities_.begin(), entities_.end(),
        [id](const EntityRecord& entity) { return entity.id == id; });
    return found == entities_.end() ? nullptr : &*found;
}

EntityRecord* EntityWorld::FindMutable(EntityId id)
{
    const auto found = std::find_if(entities_.begin(), entities_.end(),
        [id](const EntityRecord& entity) { return entity.id == id; });
    return found == entities_.end() ? nullptr : &*found;
}

bool EntityWorld::GetWorldMatrix(EntityId id, Matrix4x4& result) const
{
    return ComposeWorldMatrix(id,result,{});
}

bool EntityWorld::GetWorldMatrix(EntityId id, Matrix4x4& result, const EntityRecord& transformOverride) const
{
    if(!Find(transformOverride.id))return false;
    const EntityTransformOverride candidate{transformOverride.id,transformOverride.transform};
    return ComposeWorldMatrix(id,result,{&candidate,1});
}

bool EntityWorld::GetWorldMatrix(EntityId id, Matrix4x4& result, std::span<const EntityTransformOverride> overrides) const
{
    for(size_t index=0;index<overrides.size();++index){
        if(!Find(overrides[index].id))return false;
        for(size_t previous=0;previous<index;++previous)if(overrides[index].id==overrides[previous].id)return false;
    }
    return ComposeWorldMatrix(id,result,overrides);
}

bool EntityWorld::ComposeWorldMatrix(EntityId id, Matrix4x4& result, std::span<const EntityTransformOverride> overrides) const
{
    const auto* entity = Find(id);
    if (!entity) return false;
    const auto local=[&](const EntityRecord& record){
        for(const auto& candidate:overrides)if(candidate.id==record.id)return LocalMatrix(candidate.transform);
        return LocalMatrix(record.transform);
    };
    auto matrix = local(*entity);
    size_t visited = 0;
    while (entity->parent != 0) {
        if (++visited > entities_.size()) return false;
        entity = Find(entity->parent);
        if (!entity) return false;
        matrix = matrix * local(*entity);
    }
    result = matrix;
    return true;
}

bool EntityWorld::IsDescendant(EntityId candidate, EntityId ancestor) const
{
    for(size_t depth=0;candidate!=0;++depth) {
        const auto* entity=Find(candidate);
        // Public mutable records may bypass SetParent. Refuse attachment to a
        // broken/cyclic ancestry rather than looping forever in Editor/IPC.
        if(!entity || depth>=entities_.size()) return true;
        if(entity->parent==0) return false;
        if(entity->parent==ancestor) return true;
        candidate=entity->parent;
    }
    return false;
}

bool EntityWorld::GetPrimaryCamera(EntityId& id, Matrix4x4& rigidWorld) const
{
    EntityId selected = 0;
    Matrix4x4 selectedMatrix{};
    for (const auto& entity : entities_) {
        if (!entity.hasCamera || !entity.camera.primary || !entity.camera.IsValid() ||
            (selected != 0 && entity.id >= selected)) continue;
        const auto* ancestor = &entity;
        size_t visited = 0;
        while (ancestor && ancestor->active && ancestor->parent != 0 && ++visited <= entities_.size())
            ancestor = Find(ancestor->parent);
        if (!ancestor || !ancestor->active || visited > entities_.size()) continue;
        Matrix4x4 composed{}, rigid{};
        if (!GetWorldMatrix(entity.id, composed) || !RigidCameraMatrix(composed, rigid)) continue;
        selected = entity.id;
        selectedMatrix = rigid;
    }
    if (selected == 0) { id = 0; return false; }
    id = selected;
    rigidWorld = selectedMatrix;
    return true;
}

bool EntityWorld::SetParent(EntityId id, EntityId parent)
{
    std::string error;return SetParent(id,parent,false,error);
}

bool EntityWorld::SetParent(EntityId id,EntityId parent,bool keepWorld,std::string& error)
{
    auto* entity = FindMutable(id);
    if (!entity || id == parent || (parent != 0 && !Find(parent)) || IsDescendant(parent, id)) {
        error="Parent change rejected: missing Entity/parent, self-parent or invalid/cyclic ancestry";
        return false;
    }
    if(entity->parent==parent) {error.clear();return true;}
    auto candidate=entity->transform;
    if(keepWorld) {
        Matrix4x4 world{},parentWorld{};for(int i=0;i<4;++i) parentWorld.m[i][i]=1;
        if(!GetWorldMatrix(id,world) || (parent && !GetWorldMatrix(parent,parentWorld))) {
            error="Cannot preserve an invalid World hierarchy";return false;
        }
        if(!WorldToLocalTransform(world,parentWorld,entity->transform,candidate,error)) return false;
        const auto rebuilt=LocalMatrix(candidate)*parentWorld;
        for(int i=0;i<4;++i) for(int j=0;j<4;++j) if(!std::isfinite(rebuilt.m[i][j]) ||
            std::abs(double(rebuilt.m[i][j])-world.m[i][j])>1e-4*std::max(1.0,std::abs(double(world.m[i][j])))) {
            error="Parent-local TRS cannot stably preserve the World matrix";return false;
        }
    }
    entity->parent = parent;
    entity->transform=candidate;error.clear();
    return true;
}

bool EntityWorld::SetModel(EntityId id, std::string assetPath, std::string assetGuid)
{
    auto* entity = FindMutable(id);
    if (!entity || (assetPath.empty() && assetGuid.empty()) ||
        (!assetGuid.empty() && !ModelComponent::IsValidGuid(assetGuid))) {
        return false;
    }
    entity->hasModel = true;
    entity->model.assetPath = std::move(assetPath);
    entity->model.assetGuid = std::move(assetGuid);
    return true;
}

void EntityWorld::RemoveModel(EntityId id)
{
    if (auto* entity = FindMutable(id)) {
        entity->hasModel = false;
        entity->model.assetPath.clear();
        entity->model.assetGuid.clear();
    }
}

bool EntityWorld::SetCamera(EntityId id, const CameraComponent& camera)
{
    auto* entity = FindMutable(id);
    if (!entity || !camera.IsValid()) return false;
    entity->hasCamera = true;
    entity->camera = camera;
    return true;
}

void EntityWorld::RemoveCamera(EntityId id)
{
    if (auto* entity = FindMutable(id)) { entity->hasCamera = false; entity->camera = {}; }
}

bool EntityWorld::SetCollider(EntityId id, const Physics::ColliderComponent& collider)
{
    auto* entity = FindMutable(id);
    if (!entity || !collider.HasValidShape() ||
        !std::isfinite(collider.friction) || collider.friction < 0.0f || collider.friction > 1.0f ||
        !std::isfinite(collider.restitution) || collider.restitution < 0.0f || collider.restitution > 1.0f) {
        return false;
    }
    entity->hasCollider = true;
    entity->collider = collider;
    return true;
}

void EntityWorld::RemoveCollider(EntityId id)
{
    if (auto* entity = FindMutable(id)) entity->hasCollider = false;
}

bool EntityWorld::SetRigidBody(EntityId id, const Physics::RigidBodyComponent& rigidBody)
{
    auto* entity = FindMutable(id);
    if (!entity || !std::isfinite(rigidBody.mass) || rigidBody.mass <= 0.0f ||
        !std::isfinite(rigidBody.velocity.x) || !std::isfinite(rigidBody.velocity.y) || !std::isfinite(rigidBody.velocity.z)) return false;
    entity->hasRigidBody = true;
    entity->rigidBody = rigidBody;
    return true;
}

void EntityWorld::RemoveRigidBody(EntityId id)
{
    if (auto* entity = FindMutable(id)) entity->hasRigidBody = false;
}

bool EntityWorld::SetScript(EntityId id, std::string assetPath, bool enabled, std::string assetGuid)
{
    auto* entity = FindMutable(id);
    if (!entity || (assetPath.empty() && assetGuid.empty()) ||
        (!assetGuid.empty() && !ModelComponent::IsValidGuid(assetGuid))) return false;
    entity->hasScript = true;
    entity->script.assetPath = std::move(assetPath);
    entity->script.assetGuid = std::move(assetGuid);
    entity->script.enabled = enabled;
    entity->script.started = false;
    return true;
}

void EntityWorld::RemoveScript(EntityId id)
{
    if (auto* entity = FindMutable(id)) {
        entity->hasScript = false;
        entity->script = {};
    }
}

bool EntityWorld::SetScriptProperty(EntityId id,std::string name,Scripting::ScriptProperty value)
{
    auto* entity=FindMutable(id);
    if(!entity || !entity->hasScript || !Scripting::ValidScriptText(name,128,true) || !Scripting::ValidScriptProperty(value) ||
        (entity->script.properties.size()>=64 && !entity->script.properties.contains(name))) return false;
    entity->script.properties[std::move(name)]=std::move(value);return true;
}
bool EntityWorld::SetAudio(EntityId id,const AudioSystem::AudioComponent& audio)
{
    auto* entity=FindMutable(id);if(!entity || !audio.IsValid()) return false;
    entity->audio=audio;entity->hasAudio=true;return true;
}
void EntityWorld::RemoveAudio(EntityId id)
{
    if(auto* entity=FindMutable(id)) {entity->hasAudio=false;entity->audio={};}
}
bool EntityWorld::SetAnimator(EntityId id, const AnimationSystem::AnimatorComponent& animator)
{
    auto* entity = FindMutable(id);
    if (!entity || !animator.IsValid()) return false;
    try {
        auto candidate = animator;
        entity->animator.Swap(candidate);
        entity->hasAnimator = true;
        return true;
    } catch (...) { return false; }
}
void EntityWorld::RemoveAnimator(EntityId id)
{
    if (auto* entity = FindMutable(id)) entity->hasAnimator = false;
}
bool EntityWorld::RemoveScriptProperty(EntityId id,const std::string& name)
{
    auto* entity=FindMutable(id);return entity && entity->hasScript && entity->script.properties.erase(name)>0;
}

bool EntityWorld::SetLight(EntityId id,const LightComponent& light)
{
    auto* entity=FindMutable(id);if(!entity || !light.IsValid())return false;
    entity->light=light;entity->hasLight=true;return true;
}
void EntityWorld::RemoveLight(EntityId id) {if(auto* entity=FindMutable(id))entity->hasLight=false;}
bool EntityWorld::SetMaterial(EntityId id,const MaterialComponent& material)
{
    auto* entity=FindMutable(id);if(!entity || !material.IsValid())return false;
    try {
        MaterialComponent candidate=material;
        entity->material=std::move(candidate);
        entity->hasMaterial=true;
        return true;
    } catch(...) {return false;}
}
void EntityWorld::RemoveMaterial(EntityId id) {if(auto* entity=FindMutable(id))entity->hasMaterial=false;}
bool EntityWorld::SetCanvas(EntityId id,const CanvasComponent& canvas)
{
    auto* entity=FindMutable(id);if(!entity || !canvas.IsValid())return false;
    entity->canvas=canvas;entity->hasCanvas=true;return true;
}
void EntityWorld::RemoveCanvas(EntityId id) {if(auto* entity=FindMutable(id))entity->hasCanvas=false;}
bool EntityWorld::SetSprite(EntityId id,const SpriteComponent& sprite)
{
    auto* entity=FindMutable(id);if(!entity || !sprite.IsValid())return false;
    try {
        auto candidate=sprite; // Both owning strings prepared before publication (also supports self-alias).
        static_assert(std::is_nothrow_move_assignable_v<SpriteComponent>);
        entity->sprite=std::move(candidate);entity->hasSprite=true;return true;
    }catch(...){return false;}
}
void EntityWorld::RemoveSprite(EntityId id) {if(auto* entity=FindMutable(id))entity->hasSprite=false;}
bool EntityWorld::SetParticle(EntityId id,const ParticleComponent& particle)
{
    auto* entity=FindMutable(id);if(!entity||!particle.IsValid())return false;
    try {auto candidate=particle;static_assert(std::is_nothrow_move_assignable_v<ParticleComponent>);entity->particle=std::move(candidate);entity->hasParticle=true;return true;}catch(...){return false;}
}
void EntityWorld::RemoveParticle(EntityId id) {if(auto* entity=FindMutable(id))entity->hasParticle=false;}

SceneValue EntityWorld::Capture() const
{
    SceneValue root;
    root["version"] = 1.0;
    root["entities"] = crude_json::array{};
    for (const auto& entity : entities_) {
        SceneValue value;
        value["id"] = double(entity.id);
        value["parent"] = double(entity.parent);
        value["name"] = entity.name;
        value["active"] = entity.active;
        value["transform"]["position"] = Vector(entity.transform.position);
        value["transform"]["rotation"] = Vector(entity.transform.rotation);
        value["transform"]["scale"] = Vector(entity.transform.scale);
        value["model"] = entity.hasModel ? entity.model.assetPath : "";
        if (entity.hasModel && !entity.model.assetGuid.empty()) value["modelGuid"] = entity.model.assetGuid;
        value["camera"]["enabled"] = entity.hasCamera;
        value["camera"]["primary"] = entity.camera.primary;
        value["camera"]["fovY"] = double(entity.camera.fovY);
        value["camera"]["nearClip"] = double(entity.camera.nearClip);
        value["camera"]["farClip"] = double(entity.camera.farClip);
        value["camera"]["projection"] = entity.camera.orthographic ? "orthographic" : "perspective";
        value["camera"]["orthographicHeight"] = double(entity.camera.orthographicHeight);
        value["collider"]["enabled"] = entity.hasCollider;
        value["collider"]["halfExtents"] = Vector(entity.collider.halfExtents);
        value["collider"]["trigger"] = entity.collider.isTrigger;
        value["collider"]["friction"] = double(entity.collider.friction);
        value["collider"]["restitution"] = double(entity.collider.restitution);
        if (entity.collider.shape!=Physics::ColliderShape::Box) {
            value["collider"]["shape"] = entity.collider.shape==Physics::ColliderShape::Sphere ? "sphere" : "capsule";
            value["collider"]["radius"] = double(entity.collider.radius);
            if (entity.collider.shape==Physics::ColliderShape::Capsule)
                value["collider"]["halfSegment"] = double(entity.collider.halfSegment);
        }
        value["rigidBody"]["enabled"] = entity.hasRigidBody;
        value["rigidBody"]["velocity"] = Vector(entity.rigidBody.velocity);
        value["rigidBody"]["mass"] = double(entity.rigidBody.mass);
        value["rigidBody"]["dynamic"] = entity.rigidBody.dynamic;
        value["rigidBody"]["gravity"] = entity.rigidBody.useGravity;
        value["script"]["enabled"] = entity.hasScript && entity.script.enabled;
        value["script"]["asset"] = entity.hasScript ? entity.script.assetPath : "";
        if (entity.hasScript && !entity.script.assetGuid.empty()) value["script"]["guid"] = entity.script.assetGuid;
        if(entity.hasScript && !entity.script.properties.empty()) {
            value["script"]["properties"]=crude_json::object{};
            for(const auto& [name,property]:entity.script.properties) {
                if(const auto* number=std::get_if<double>(&property)) value["script"]["properties"][name]=*number;
                else if(const auto* boolean=std::get_if<bool>(&property)) value["script"]["properties"][name]=*boolean;
                else value["script"]["properties"][name]=std::get<std::string>(property);
            }
        }
        if(entity.hasAudio) {
            value["audio"]["asset"]=entity.audio.assetPath;
            if(!entity.audio.assetGuid.empty()) value["audio"]["guid"]=entity.audio.assetGuid;
            value["audio"]["enabled"]=entity.audio.enabled;
            value["audio"]["loop"]=entity.audio.loop;
            value["audio"]["playOnAwake"]=entity.audio.playOnAwake;
            value["audio"]["volume"]=double(entity.audio.volume);
        }
        if (entity.hasAnimator) value["animator"] = AnimationSystem::EncodeAnimator(entity.animator);
        if(entity.hasLight)value["light"]=EncodeLight(entity.light);
        if(entity.hasMaterial)value["material"]=EncodeMaterial(entity.material);
        if(entity.hasCanvas)value["canvas"]=EncodeCanvas(entity.canvas);
        if(entity.hasSprite)value["sprite"]=EncodeSprite(entity.sprite);
        if(entity.hasParticle)value["particle"]=EncodeParticle(entity.particle);
        if(!entity.prefabLink.is_null())value["prefabLink"]=entity.prefabLink;
        root["entities"].push_back(std::move(value));
    }
    RetagEntityDocument(root);return root;
}

bool EntityWorld::Restore(const SceneValue& value, std::string& error)
{
    try {
        if (!value.is_object() || !value.contains("version") || !value["version"].is_number() ||
            value["version"].get<crude_json::number>() != RequiredEntityDocumentVersion(value) || !value.contains("entities") ||
            !value["entities"].is_array()) {
            throw std::runtime_error("Invalid EntityWorld scene document");
        }
        std::vector<EntityRecord> restored;
        EntityId maximumId = 0;
        for (const auto& valueEntity : value["entities"].get<crude_json::array>()) {
            if (!valueEntity.is_object() || !valueEntity.contains("id") || !valueEntity.contains("parent") ||
                !valueEntity.contains("name") || !valueEntity.contains("active") || !valueEntity.contains("transform") ||
                !valueEntity.contains("model") || !valueEntity["name"].is_string() ||
                !valueEntity["active"].is_boolean() || !valueEntity["transform"].is_object() ||
                !valueEntity["model"].is_string()) {
                throw std::runtime_error("Invalid EntityWorld entity");
            }
            EntityRecord entity{};
            if(valueEntity.contains("particle")) {
                if(!DecodeParticle(valueEntity["particle"],entity.particle))throw std::runtime_error("Invalid Particle component");
                entity.hasParticle=true;
            }
            if(valueEntity.contains("canvas")) {
                if(!DecodeCanvas(valueEntity["canvas"],entity.canvas))throw std::runtime_error("Invalid Canvas component");
                entity.hasCanvas=true;
            }
            if(valueEntity.contains("sprite")) {
                if(!DecodeSprite(valueEntity["sprite"],entity.sprite))throw std::runtime_error("Invalid Sprite component");
                entity.hasSprite=true;
            }
            if(valueEntity.contains("light")) {
                if(!DecodeLight(valueEntity["light"],entity.light))throw std::runtime_error("Invalid Light component");
                entity.hasLight=true;
            }
            if(valueEntity.contains("material")) {
                if(!DecodeMaterial(valueEntity["material"],entity.material))throw std::runtime_error("Invalid Material component");
                entity.hasMaterial=true;
            }
            if(valueEntity.contains("prefabLink")) {
                if(valueEntity["prefabLink"].is_null())throw std::runtime_error("Prefab link must be omitted rather than null");
                entity.prefabLink=valueEntity["prefabLink"];
            }
            entity.id = ReadId(valueEntity["id"], "id");
            entity.parent = ReadId(valueEntity["parent"], "parent");
            entity.name = valueEntity["name"].get<crude_json::string>();
            entity.active = valueEntity["active"].get<crude_json::boolean>();
            const auto& transform = valueEntity["transform"];
            entity.transform.position = ReadVector(transform["position"], "position");
            entity.transform.rotation = ReadVector(transform["rotation"], "rotation");
            entity.transform.scale = ReadVector(transform["scale"], "scale");
            const auto model = valueEntity["model"].get<crude_json::string>();
            entity.hasModel = !model.empty();
            entity.model.assetPath = model;
            if (valueEntity.contains("modelGuid")) {
                if (!valueEntity["modelGuid"].is_string()) throw std::runtime_error("Invalid model GUID");
                entity.model.assetGuid = valueEntity["modelGuid"].get<crude_json::string>();
                if (!ModelComponent::IsValidGuid(entity.model.assetGuid)) throw std::runtime_error("Invalid model GUID");
                entity.hasModel = true;
            }
            if (valueEntity.contains("animator")) {
                if (!AnimationSystem::DecodeAnimator(valueEntity["animator"], entity.animator))
                    throw std::runtime_error("Invalid Animator component");
                entity.hasAnimator = true;
            }
            if (valueEntity.contains("camera")) {
                const auto& camera = valueEntity["camera"];
                if (!camera.is_object() || !camera.contains("enabled") || !camera["enabled"].is_boolean() ||
                    !camera.contains("primary") || !camera["primary"].is_boolean() ||
                    !camera.contains("fovY") || !camera.contains("nearClip") || !camera.contains("farClip"))
                    throw std::runtime_error("Invalid camera component");
                entity.hasCamera = camera["enabled"].get<crude_json::boolean>();
                entity.camera.primary = camera["primary"].get<crude_json::boolean>();
                entity.camera.fovY = ReadCameraNumber(camera["fovY"], "fovY");
                entity.camera.nearClip = ReadCameraNumber(camera["nearClip"], "nearClip");
                entity.camera.farClip = ReadCameraNumber(camera["farClip"], "farClip");
                if (camera.contains("projection")) {
                    if (!camera["projection"].is_string()) throw std::runtime_error("Invalid camera projection mode");
                    const auto mode = camera["projection"].get<crude_json::string>();
                    if (mode != "perspective" && mode != "orthographic") throw std::runtime_error("Unknown camera projection mode");
                    entity.camera.orthographic = mode == "orthographic";
                }
                if (camera.contains("orthographicHeight"))
                    entity.camera.orthographicHeight = ReadCameraNumber(camera["orthographicHeight"], "orthographicHeight");
                if (!entity.camera.IsValid()) throw std::runtime_error("Invalid camera projection settings");
            }
            if (valueEntity.contains("collider") && valueEntity["collider"].is_object()) {
                const auto& collider = valueEntity["collider"];
                entity.hasCollider = collider.contains("enabled") && collider["enabled"].is_boolean() && collider["enabled"].get<crude_json::boolean>();
                if (collider.contains("halfExtents")) entity.collider.halfExtents = ReadVector(collider["halfExtents"], "halfExtents");
                if (entity.collider.halfExtents.x < 0.0f || entity.collider.halfExtents.y < 0.0f || entity.collider.halfExtents.z < 0.0f)
                    throw std::runtime_error("Collider half extents must be non-negative");
                if (collider.contains("trigger") && collider["trigger"].is_boolean()) entity.collider.isTrigger = collider["trigger"].get<crude_json::boolean>();
                if (collider.contains("shape")) {
                    if (!collider["shape"].is_string()) throw std::runtime_error("Invalid collider shape");
                    const auto shape=collider["shape"].get<std::string>();
                    if (shape=="sphere") entity.collider.shape=Physics::ColliderShape::Sphere;
                    else if (shape=="capsule") entity.collider.shape=Physics::ColliderShape::Capsule;
                    else if (shape!="box") throw std::runtime_error("Unknown collider shape");
                }
                const auto dimension=[&](const char* key,float& output) {
                    if (!collider.contains(key)) return;
                    const auto& value=collider[key];
                    if (!value.is_number()) throw std::runtime_error(std::string("Invalid collider ")+key);
                    const auto number=value.get<double>();
                    if (!std::isfinite(number) || number<0 || number>std::numeric_limits<float>::max())
                        throw std::runtime_error(std::string("Invalid collider ")+key);
                    output=static_cast<float>(number);
                };
                dimension("radius",entity.collider.radius);
                dimension("halfSegment",entity.collider.halfSegment);
                if (!entity.collider.HasValidShape()) throw std::runtime_error("Invalid collider dimensions");
                for(const auto key:{"friction","restitution"}) if(collider.contains(key)) {
                    if(!collider[key].is_number()) throw std::runtime_error("Collider material must be numeric");
                    const auto coefficient=collider[key].get<crude_json::number>();
                    if(!std::isfinite(coefficient) || coefficient<0.0 || coefficient>1.0)
                        throw std::runtime_error("Collider material must be finite and between 0 and 1");
                    (std::string(key)=="friction" ? entity.collider.friction : entity.collider.restitution)=static_cast<float>(coefficient);
                }
            }
            if (valueEntity.contains("rigidBody") && valueEntity["rigidBody"].is_object()) {
                const auto& body = valueEntity["rigidBody"];
                entity.hasRigidBody = body.contains("enabled") && body["enabled"].is_boolean() && body["enabled"].get<crude_json::boolean>();
                if (body.contains("velocity")) entity.rigidBody.velocity = ReadVector(body["velocity"], "velocity");
                if (body.contains("mass")) {
                    if (!body["mass"].is_number()) throw std::runtime_error("Rigid body mass must be a number");
                    const double mass = body["mass"].get<crude_json::number>();
                    if (!std::isfinite(mass) || mass <= 0.0 || mass > std::numeric_limits<float>::max())
                        throw std::runtime_error("Rigid body mass must be finite and positive");
                    entity.rigidBody.mass = static_cast<float>(mass);
                    if (entity.rigidBody.mass <= 0.0f) throw std::runtime_error("Rigid body mass is too small");
                }
                if (body.contains("dynamic") && body["dynamic"].is_boolean()) entity.rigidBody.dynamic = body["dynamic"].get<crude_json::boolean>();
                if (body.contains("gravity") && body["gravity"].is_boolean()) entity.rigidBody.useGravity = body["gravity"].get<crude_json::boolean>();
            }
            if (valueEntity.contains("script") && valueEntity["script"].is_object()) {
                const auto& script = valueEntity["script"];
                if (script.contains("asset") && script["asset"].is_string()) {
                    entity.script.assetPath = script["asset"].get<crude_json::string>();
                    entity.hasScript = !entity.script.assetPath.empty();
                }
                if (script.contains("enabled") && script["enabled"].is_boolean()) entity.script.enabled = script["enabled"].get<crude_json::boolean>();
                if (script.contains("guid")) {
                    if (!script["guid"].is_string()) throw std::runtime_error("Invalid script GUID");
                    entity.script.assetGuid = script["guid"].get<crude_json::string>();
                    if (!ModelComponent::IsValidGuid(entity.script.assetGuid)) throw std::runtime_error("Invalid script GUID");
                    entity.hasScript = true;
                }
                entity.script.started = false;
                if(script.contains("properties")) {
                    if(!script["properties"].is_object() || script["properties"].get<crude_json::object>().size()>64) throw std::runtime_error("Invalid script properties object");
                    for(const auto& [name,property]:script["properties"].get<crude_json::object>()) {
                        if(!Scripting::ValidScriptText(name,128,true)) throw std::runtime_error("Invalid script property name");
                        Scripting::ScriptProperty item;
                        if(property.is_number()) item=property.get<crude_json::number>();
                        else if(property.is_boolean()) item=property.get<crude_json::boolean>();
                        else if(property.is_string()) item=property.get<crude_json::string>();
                        else throw std::runtime_error("Script properties require number, boolean or string values");
                        if(!Scripting::ValidScriptProperty(item)) throw std::runtime_error("Invalid script property value");
                        entity.script.properties.emplace(name,std::move(item));
                    }
                    if(!entity.hasScript && !entity.script.properties.empty()) throw std::runtime_error("Properties require a script reference");
                }
            }
            if(valueEntity.contains("audio")) {
                const auto& audio=valueEntity["audio"];
                if(!audio.is_object()) throw std::runtime_error("Audio component must be an object");
                if(audio.contains("asset")) {
                    if(!audio["asset"].is_string()) throw std::runtime_error("Audio asset must be a string");
                    entity.audio.assetPath=audio["asset"].get<crude_json::string>();
                }
                if(audio.contains("guid")) {
                    if(!audio["guid"].is_string()) throw std::runtime_error("Audio GUID must be a string");
                    entity.audio.assetGuid=audio["guid"].get<crude_json::string>();
                    if(entity.audio.assetGuid.empty()) throw std::runtime_error("Audio GUID must not be empty when present");
                }
                const auto readBool=[&](const char* field,bool& output) {
                    if(audio.contains(field)) {
                        if(!audio[field].is_boolean()) throw std::runtime_error(std::string("Invalid Audio ")+field);
                        output=audio[field].get<crude_json::boolean>();
                    }
                };
                readBool("enabled",entity.audio.enabled);readBool("loop",entity.audio.loop);readBool("playOnAwake",entity.audio.playOnAwake);
                if(audio.contains("volume")) {
                    if(!audio["volume"].is_number()) throw std::runtime_error("Audio volume must be a number");
                    const auto number=audio["volume"].get<crude_json::number>();
                    if(!std::isfinite(number) || number<0 || number>1) throw std::runtime_error("Audio volume must be between zero and one");
                    entity.audio.volume=static_cast<float>(number);
                }
                if(!entity.audio.IsValid()) throw std::runtime_error("Invalid Audio component reference or settings");
                entity.hasAudio=true;
            }
            if (entity.id == 0 || std::any_of(restored.begin(), restored.end(),
                [&entity](const EntityRecord& other) { return other.id == entity.id; })) {
                throw std::runtime_error("Entity ids must be unique and non-zero");
            }
            maximumId = std::max(maximumId, entity.id);
            restored.push_back(std::move(entity));
        }
        for (const auto& entity : restored) {
            if (entity.parent != 0 && !std::any_of(restored.begin(), restored.end(),
                [&entity](const EntityRecord& other) { return other.id == entity.parent; })) {
                throw std::runtime_error("Entity parent does not exist");
            }
            EntityId ancestor = entity.parent;
            size_t visited = 0;
            while (ancestor != 0) {
                if (ancestor == entity.id || ++visited > restored.size()) throw std::runtime_error("Entity hierarchy contains a cycle");
                const auto found = std::find_if(restored.begin(), restored.end(),
                    [ancestor](const EntityRecord& record) { return record.id == ancestor; });
                if (found == restored.end()) throw std::runtime_error("Entity parent does not exist");
                ancestor = found->parent;
            }
        }
        maximumId=(std::max)(maximumId,CheckPrefabLinks(restored));
        entities_ = std::move(restored);
        nextId_ = maximumId + 1; // Max exact ID + 1 is a non-wrapping exhausted sentinel.
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

bool EntityWorld::Save(const std::filesystem::path& path, std::string& error, bool replaceExisting) const
{
    const auto value = Capture();
    EntityWorld checked;if(!checked.Restore(value,error))return false;
    return SceneDocument::Write(path, value, error,replaceExisting);
}

bool EntityWorld::Load(const std::filesystem::path& path, std::string& error)
{
    try {
        SceneValue parsed;
        if(!SceneDocument::Read(path,parsed,error)) return false;
        return Restore(parsed, error);
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

} // namespace Engine::Scene
