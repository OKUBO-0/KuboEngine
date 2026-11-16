#pragma once
#include <array>
#include <cmath>
#include <string>

namespace Engine::Editor {
// Operation indices match ImGuizmoManager::Operation: translation, rotation,
// scale. Rotation uses backend degrees; scale is a drag-relative multiplier.
inline bool ValidGizmoSnapStep(int operation,float value) {
    if (operation<0 || operation>2 || !std::isfinite(value) || value<0.0001f) return false;
    const float maximum=operation==0 ? 1000000.0f : operation==1 ? 360.0f : 100.0f;
    return value<=maximum;
}
struct GizmoSnapSettings {
    bool enabled=false;
    float distance=1, degrees=15, scaleRatio=0.1f;
    bool Resolve(int operation,std::array<float,3>& output,std::string& error) const {
        if (operation<0 || operation>2) { error="Unknown gizmo snap operation"; return false; }
        if (!enabled) { output={}; error.clear(); return true; }
        const float value=operation==0 ? distance : operation==1 ? degrees : scaleRatio;
        if (!ValidGizmoSnapStep(operation,value)) {
            error="Snap step must be finite and within 0.0001..1000000 units / 360 degrees / 100 scale ratio";
            return false;
        }
        output={value,value,value}; error.clear(); return true;
    }
};
}
