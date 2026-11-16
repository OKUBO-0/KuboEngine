#pragma once
#include "EntityWorld.h"
namespace Engine::Scene {
enum class RenderParticleResult {Success,InvalidWorld,InvalidComponent,Limit,AllocationFailure,AssetFailure,ResourceFailure};
struct RenderParticleItem {EntityId id=0;ParticleComponent settings;Matrix4x4 model{};};
struct RenderParticleFrame {
    static constexpr size_t MaximumEmitters=64,MaximumCapacity=16384;
    std::vector<RenderParticleItem> items;
    size_t capacity=0,work=0,skippedInvalidFrames=0;
};
// Settings/pose snapshot only, no GPU work. Publication is whole-frame and
// local-space affine poses retain nonuniform scale, shear and mirroring.
RenderParticleResult BuildRenderParticleFrame(const EntityWorld& world,RenderParticleFrame& output) noexcept;
}
