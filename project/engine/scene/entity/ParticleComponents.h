#pragma once
#include "UIComponents.h"
#include <cstdint>
#include <optional>
#include <vector>
namespace Engine::Scene {
enum class ParticleGeometry {Quad,Ring,Cylinder};
struct ParticleSizeKey {float age=0,size=0;bool operator==(const ParticleSizeKey&) const noexcept=default;};
struct ParticleColorKey {float age=0;Vector4 color{};};
// Local-space authored settings. Runtime clocks, random streams, particles and
// GPU owners never enter Scene/Prefab/history data.
struct ParticleComponent {
    bool enabled=true;
    std::string assetPath,assetGuid;
    ParticleGeometry geometry=ParticleGeometry::Quad;
    uint32_t capacity=128,burst=0,seed=1;
    float rate=16,lifetime=1,size=0.25f;
    Vector3 velocity{0,1,0},spread{0.5f,0.5f,0.5f};
    Vector4 startColor{1,1,1,1},endColor{1,1,1,0};
    static constexpr size_t MaximumCurveKeys=8;
    std::optional<float> endSize;
    std::vector<ParticleSizeKey> sizeKeys;
    std::vector<ParticleColorKey> colorKeys;
    float SizeAt(float age) const noexcept;
    Vector4 ColorAt(float age) const noexcept;
    bool IsValid() const noexcept;
    bool operator==(const ParticleComponent& other) const noexcept;
};
bool DecodeParticle(const SceneValue& value,ParticleComponent& output) noexcept;
SceneValue EncodeParticle(const ParticleComponent& value);
// Only live components select version2; saved Prefab baselines are not live.
double RequiredEntityDocumentVersion(const SceneValue& world);
// For internally assembled candidates after adding/removing Entity fields.
void RetagEntityDocument(SceneValue& world);
}
