#pragma once
#include "EntityGizmoTransform.h"
#include "EntityHistory.h"
#include "GizmoSnapSettings.h"
#include <limits>
#include <unordered_set>

namespace Engine::Editor {
// Explicit all-axis alignment to a world-origin grid; unrelated to drag-relative snap.
inline bool SnapWorldGridTarget(Matrix4x4& desired,float step,std::string& error) {
    const auto reject=[&](const char* why) {error=why;return false;};
    if(!ValidGizmoSnapStep(0,step)) return reject("Invalid world grid distance step");
    for(int axis=0;axis<3;++axis) {
        const double position=desired.m[3][axis];
        if(!std::isfinite(position)) return reject("World grid position is not finite");
        const double snapped=std::round(position/double(step))*double(step);
        if(!std::isfinite(snapped) || std::abs(snapped)>(std::numeric_limits<float>::max)())
            return reject("World grid position exceeds float range");
        const float stored=float(snapped);
        if(std::abs(double(stored)-snapped)>double(step)*1e-4)
            return reject("World grid step is too fine for this position");
        desired.m[3][axis]=stored;
    }
    error.clear();return true;
}
inline bool ApplyWorldGridTarget(Scene::EntityWorld& world,Scene::EntityId id,const Matrix4x4& desired,float step,std::string& error) {
    const auto reject=[&](const char* why) {error=why;return false;};
    auto* entity=world.Find(id);if(!entity)return reject("Select an Entity to align");
    Matrix4x4 parent{};for(int axis=0;axis<4;++axis)parent.m[axis][axis]=1;
    if(entity->parent && !world.GetWorldMatrix(entity->parent,parent))return reject("World grid hierarchy is unavailable");
    const auto original=entity->transform;
    Scene::TransformComponent converted;
    if(!GizmoWorldToLocal(desired,parent,original,converted,error)) return false;
    // Translation alone must never rewrite authored Euler winding or scale signs.
    auto candidate=original;candidate.position=converted.position;
    entity->transform=candidate;
    Matrix4x4 rebuilt{};
    bool representable=world.GetWorldMatrix(id,rebuilt);
    for(int axis=0;axis<3;++axis)
        representable=representable && std::abs(double(rebuilt.m[3][axis])-desired.m[3][axis])<=double(step)*1e-4;
    if(!representable) {entity->transform=original;return reject("Parent/local float precision cannot represent this world grid step");}
    error.clear();return true;
}
inline bool AlignEntitiesToWorldGrid(Scene::EntityWorld& world,const std::vector<Scene::EntityId>& ids,float step,
    EntityHistory& history,Scene::EntityId primary,std::string& error,const EntitySelection::Snapshot* fullSelection=nullptr) {
    const auto reject=[&](const char* why) {error=why;return false;};
    if(!ValidGizmoSnapStep(0,step))return reject("Invalid world grid distance step");
    if(ids.empty() || ids.size()>10000)return reject("World grid selection must contain 1..10000 Entities");
    if(std::find(ids.begin(),ids.end(),primary)==ids.end())return reject("World grid primary must belong to selection");
    struct Target {Scene::EntityId id;size_t depth;Matrix4x4 matrix;};
    std::vector<Target> targets;std::unordered_set<Scene::EntityId> unique;
    for(const auto id:ids) {
        const auto* entity=world.Find(id);
        if(!entity || !unique.insert(id).second)return reject("World grid selection is missing or duplicated");
        Target target{id,0,{}};
        if(!world.GetWorldMatrix(id,target.matrix) || !SnapWorldGridTarget(target.matrix,step,error)) {
            if(error.empty())error="World grid hierarchy is unavailable";return false;
        }
        for(auto parent=entity->parent;parent;) {
            const auto* ancestor=world.Find(parent);
            if(!ancestor || ++target.depth>world.GetEntities().size())return reject("World grid ancestry is missing or cyclic");
            parent=ancestor->parent;
        }
        targets.push_back(target);
    }
    // Capture every target from the original world, then parents before descendants.
    std::sort(targets.begin(),targets.end(),[](const Target& a,const Target& b){return a.depth==b.depth?a.id<b.id:a.depth<b.depth;});
    auto candidate=world;
    for(const auto& target:targets)if(!ApplyWorldGridTarget(candidate,target.id,target.matrix,step,error))return false;
    // A selected descendant must still match after every selected ancestor moves.
    for(const auto& target:targets) {
        Matrix4x4 actual{};if(!candidate.GetWorldMatrix(target.id,actual))return reject("Aligned hierarchy is unavailable");
        for(int axis=0;axis<3;++axis)if(std::abs(double(actual.m[3][axis])-target.matrix.m[3][axis])>double(step)*1e-4)
            return reject("Aligned hierarchy cannot retain every target position");
    }
    const auto before=world.Capture();
    if(before.dump()==candidate.Capture().dump()){error.clear();return true;}
    const auto selection=fullSelection?*fullSelection:EntitySelection::Snapshot{ids,primary,primary};
    if(selection.ids!=ids || selection.primary!=primary)return reject("World grid selection snapshot mismatch");
    history.Commit();
    if(!history.Record(before,candidate,selection,selection,error))return false;
    history.Commit();world=std::move(candidate);error.clear();return true;
}
inline bool AlignEntityToWorldGrid(Scene::EntityWorld& world,Scene::EntityId id,float step,
    EntityHistory& history,std::string& error) {
    return AlignEntitiesToWorldGrid(world,{id},step,history,id,error);
}
}
