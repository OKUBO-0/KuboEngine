#pragma once
#include "Vector4.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <type_traits>

namespace Engine::Math {
// Matches AuthoredLighting.hlsli, b5. Each field occupies one HLSL float4 slot.
struct AuthoredLightGpu {
    Vector4 colorIntensity{};
    Vector4 positionRange{};
    Vector4 directionOuter{};
    Vector4 parameters{}; // cos(inner half angle), decay, kind (0/1/2), reserved
};
struct AuthoredLightData {
    static constexpr uint32_t MaximumLights=64;
    uint32_t mode=0,count=0;
    uint32_t reserved[2]{};
    std::array<AuthoredLightGpu,MaximumLights> lights{};
    bool IsValid() const noexcept {
        if(mode>1 || count>MaximumLights || (!mode && count))return false;
        const auto range=[](float n,float lo,float hi){return std::isfinite(n) && n>=lo && n<=hi;};
        for(uint32_t i=0;i<count;++i){const auto& l=lights[i];
            if(!range(l.colorIntensity.x,0,1) || !range(l.colorIntensity.y,0,1) || !range(l.colorIntensity.z,0,1) ||
                !range(l.colorIntensity.w,0,1000000) || !std::isfinite(l.positionRange.x) ||
                !std::isfinite(l.positionRange.y) || !std::isfinite(l.positionRange.z) || !range(l.positionRange.w,0.0001f,1000000) ||
                !range(l.directionOuter.w,-0.000001f,1) || !range(l.parameters.x,l.directionOuter.w,1) ||
                !range(l.parameters.y,0,16) || (l.parameters.z!=0 && l.parameters.z!=1 && l.parameters.z!=2))return false;
            const double norm=double(l.directionOuter.x)*l.directionOuter.x+double(l.directionOuter.y)*l.directionOuter.y+double(l.directionOuter.z)*l.directionOuter.z;
            if(!std::isfinite(norm) || norm<0.999 || norm>1.001)return false;
        }
        return true;
    }
};
static_assert(std::is_trivially_copyable_v<AuthoredLightData>);
static_assert(sizeof(AuthoredLightGpu)==64 && offsetof(AuthoredLightGpu,parameters)==48);
static_assert(offsetof(AuthoredLightData,lights)==16 && sizeof(AuthoredLightData)==16+64*64);
}
