#pragma once
#include "Vector3.h"
#include "Matrix4x4.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace Engine::Editor {
struct SceneBounds { Vector3 min, max; };
struct ScenePickSegment { Vector3 nearPoint, farPoint; };
inline bool FinitePoint(Vector3 point) { return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z); }
inline bool ValidBounds(const SceneBounds& box) {
    return FinitePoint(box.min) && FinitePoint(box.max) && box.min.x <= box.max.x && box.min.y <= box.max.y && box.min.z <= box.max.z;
}
inline bool ProjectPoint(Vector3 point, const Matrix4x4& matrix, Vector3& output) {
    double values[4]{};
    for (int col=0; col<4; ++col) values[col] = double(point.x)*matrix.m[0][col] + double(point.y)*matrix.m[1][col] + double(point.z)*matrix.m[2][col] + matrix.m[3][col];
    if (!std::isfinite(values[3]) || std::abs(values[3]) < 1.0e-12) return false;
    Vector3 candidate{float(values[0]/values[3]), float(values[1]/values[3]), float(values[2]/values[3])};
    if (!FinitePoint(candidate)) return false;
    output = candidate; return true;
}
inline bool TransformSceneBounds(const SceneBounds& local, const Matrix4x4& world, SceneBounds& output) {
    if (!ValidBounds(local)) return false;
    const float limit = (std::numeric_limits<float>::max)();
    SceneBounds candidate{{limit,limit,limit},{-limit,-limit,-limit}};
    for (int corner=0; corner<8; ++corner) {
        Vector3 transformed;
        if (!ProjectPoint({corner&1 ? local.max.x : local.min.x, corner&2 ? local.max.y : local.min.y,
            corner&4 ? local.max.z : local.min.z}, world, transformed)) return false;
        candidate.min.x = (std::min)(candidate.min.x,transformed.x); candidate.max.x = (std::max)(candidate.max.x,transformed.x);
        candidate.min.y = (std::min)(candidate.min.y,transformed.y); candidate.max.y = (std::max)(candidate.max.y,transformed.y);
        candidate.min.z = (std::min)(candidate.min.z,transformed.z); candidate.max.z = (std::max)(candidate.max.z,transformed.z);
    }
    output = candidate; return true;
}
// NDC x/y already exclude viewport letterboxing; DirectX clip depth is 0..1.
inline bool MakeScenePickSegment(float x, float y, const Matrix4x4& viewProjection, ScenePickSegment& output) {
    if (!std::isfinite(x) || !std::isfinite(y) || std::abs(x)>1 || std::abs(y)>1) return false;
    const auto inverse = viewProjection.Inverse();
    ScenePickSegment candidate;
    if (!ProjectPoint({x,y,0},inverse,candidate.nearPoint) || !ProjectPoint({x,y,1},inverse,candidate.farPoint)) return false;
    if (candidate.nearPoint.x == candidate.farPoint.x && candidate.nearPoint.y == candidate.farPoint.y && candidate.nearPoint.z == candidate.farPoint.z) return false;
    output = candidate; return true;
}
inline bool IntersectSceneBounds(const ScenePickSegment& segment, const SceneBounds& box, double& fraction) {
    if (!ValidBounds(box) || !FinitePoint(segment.nearPoint) || !FinitePoint(segment.farPoint)) return false;
    const double origin[]{segment.nearPoint.x,segment.nearPoint.y,segment.nearPoint.z};
    const double end[]{segment.farPoint.x,segment.farPoint.y,segment.farPoint.z};
    const double low[]{box.min.x,box.min.y,box.min.z}, high[]{box.max.x,box.max.y,box.max.z};
    double first=0,last=1;
    bool nonzero=false;
    for (int axis=0; axis<3; ++axis) {
        const double direction = end[axis]-origin[axis];
        if (direction == 0) { if (origin[axis]<low[axis] || origin[axis]>high[axis]) return false; continue; }
        nonzero=true;
        double a=(low[axis]-origin[axis])/direction,b=(high[axis]-origin[axis])/direction;
        if (a>b) std::swap(a,b);
        first=(std::max)(first,a); last=(std::min)(last,b);
        if (first>last) return false;
    }
    if (!nonzero) return false;
    fraction=first; return true;
}
}
