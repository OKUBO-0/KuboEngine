#pragma once

#include "ScenePicking.h"
#include "Vector2.h"
#include <array>

namespace Engine::Editor {

// Clip against the DirectX view volume before the perspective divide so a
// guide crossing the near plane ends at the image instead of flipping.
inline bool ProjectSceneGuideLine(Vector3 a, Vector3 b, const Matrix4x4& matrix,
    Vector2& start, Vector2& end)
{
    const auto clipPoint = [&](Vector3 point) {
        std::array<double, 4> result{};
        for (size_t i = 0; i < result.size(); ++i) {
            result[i] = double(point.x) * matrix.m[0][i] + double(point.y) * matrix.m[1][i]
                + double(point.z) * matrix.m[2][i] + matrix.m[3][i];
        }
        return result;
    };
    const auto from = clipPoint(a);
    const auto to = clipPoint(b);
    const auto halfSpaces = [](const auto& point) {
        const auto [x, y, z, w] = point;
        return std::array{x + w, w - x, y + w, w - y, z, w - z};
    };
    const auto fromPlanes = halfSpaces(from);
    const auto toPlanes = halfSpaces(to);
    double first = 0, last = 1;
    for (size_t i = 0; i < fromPlanes.size(); ++i) {
        const auto f = fromPlanes[i], t = toPlanes[i];
        if (!std::isfinite(f) || !std::isfinite(t) || (f < 0 && t < 0)) return false;
        if (f < 0) first = (std::max)(first, f / (f - t));
        if (t < 0) last = (std::min)(last, f / (f - t));
        if (first > last) return false;
    }
    const auto divide = [&](double fraction, Vector2& output) {
        const double w = std::lerp(from[3], to[3], fraction);
        if (w <= 0) return false;
        const double x = std::lerp(from[0], to[0], fraction) / w;
        const double y = std::lerp(from[1], to[1], fraction) / w;
        if (!std::isfinite(x) || !std::isfinite(y)) return false;
        output = {float(std::clamp(x, -1.0, 1.0)), float(std::clamp(y, -1.0, 1.0))};
        return true;
    };
    Vector2 clippedStart{}, clippedEnd{};
    if (!divide(first, clippedStart) || !divide(last, clippedEnd)) return false;
    start = clippedStart;
    end = clippedEnd;
    return true;
}

} // namespace Engine::Editor
