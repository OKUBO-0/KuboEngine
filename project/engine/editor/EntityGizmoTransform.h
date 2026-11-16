#pragma once
#include "../scene/entity/TransformConversion.h"

namespace Engine::Editor {
inline bool GizmoWorldToLocal(const Matrix4x4& world,const Matrix4x4& parent,
    const Scene::TransformComponent& reference,Scene::TransformComponent& output,std::string& error) {
    return Scene::WorldToLocalTransform(world,parent,reference,output,error);
}
}
