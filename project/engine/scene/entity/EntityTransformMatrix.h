#pragma once
#include "EntityWorld.h"
#include <cmath>

namespace Engine::Scene {
// Shared by authored World composition and read-only physics queries. Preserve
// row-vector S * Rx * Ry * Rz and the existing left-to-right ancestry product.
inline Matrix4x4 EntityLocalMatrix(const TransformComponent& transform)
{
    const auto& r=transform.rotation;
    const float cx=std::cos(r.x),sx=std::sin(r.x),cy=std::cos(r.y),sy=std::sin(r.y),cz=std::cos(r.z),sz=std::sin(r.z);
    const Matrix4x4 x{{{1,0,0,0},{0,cx,sx,0},{0,-sx,cx,0},{0,0,0,1}}};
    const Matrix4x4 y{{{cy,0,-sy,0},{0,1,0,0},{sy,0,cy,0},{0,0,0,1}}};
    const Matrix4x4 z{{{cz,sz,0,0},{-sz,cz,0,0},{0,0,1,0},{0,0,0,1}}};
    Matrix4x4 scale{};scale.m[0][0]=transform.scale.x;scale.m[1][1]=transform.scale.y;scale.m[2][2]=transform.scale.z;scale.m[3][3]=1;
    auto result=scale*x*y*z;result.m[3][0]=transform.position.x;result.m[3][1]=transform.position.y;result.m[3][2]=transform.position.z;return result;
}
}
