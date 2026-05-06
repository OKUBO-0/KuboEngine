#include "game/directxgame/effects/DirectXGameParticleBehaviors.h"
#include "ParticleManager.h"
#include <numbers>

namespace DirectXGame {

namespace {

constexpr float kRippleLifetime = 0.42f;
constexpr float kSparkLifetime = 0.35f;
constexpr float kGroundY = -1.82f;

}

RippleParticleBehavior::RippleParticleBehavior(const Vector4& color)
	: color_(color)
{
}

Engine::Particle::Particle RippleParticleBehavior::Create(std::mt19937& rng, const Vector3& pos)
{
	std::uniform_real_distribution<float> scaleDist(0.75f, 1.35f);
	std::uniform_real_distribution<float> rotateDist(-std::numbers::pi_v<float>, std::numbers::pi_v<float>);

	Engine::Particle::Particle particle{};
	const float scale = scaleDist(rng);
	particle.transform.scale = { scale, scale, scale };
	particle.transform.rotate = { std::numbers::pi_v<float> * 0.5f, 0.0f, rotateDist(rng) };
	particle.transform.translate = { pos.x, kGroundY, pos.z };
	particle.Velocity = { 0.0f, 0.0f, 0.0f };
	particle.color = color_;
	particle.lifetime = kRippleLifetime;
	particle.currentTime = 0.0f;
	return particle;
}

void RippleParticleBehavior::Update(Engine::Particle::Particle& particle, float dt, Engine::Math::Material* /*materialData*/)
{
	particle.currentTime += dt;
	const float progress = particle.currentTime / particle.lifetime;
	const float scale = 1.0f + progress * 4.5f;
	particle.transform.scale = { scale, scale, scale };
}

SparkParticleBehavior::SparkParticleBehavior(const Vector4& color)
	: color_(color)
{
}

Engine::Particle::Particle SparkParticleBehavior::Create(std::mt19937& rng, const Vector3& pos)
{
	std::uniform_real_distribution<float> velocityDist(-0.18f, 0.18f);
	std::uniform_real_distribution<float> upDist(0.06f, 0.22f);
	std::uniform_real_distribution<float> scaleDist(0.18f, 0.34f);
	std::uniform_real_distribution<float> rotateDist(-std::numbers::pi_v<float>, std::numbers::pi_v<float>);

	Engine::Particle::Particle particle{};
	const float scale = scaleDist(rng);
	particle.transform.scale = { scale, scale, scale };
	particle.transform.rotate = { rotateDist(rng), rotateDist(rng), rotateDist(rng) };
	particle.transform.translate = { pos.x, pos.y + 0.8f, pos.z };
	particle.Velocity = { velocityDist(rng), upDist(rng), velocityDist(rng) };
	particle.color = color_;
	particle.lifetime = kSparkLifetime;
	particle.currentTime = 0.0f;
	return particle;
}

void SparkParticleBehavior::Update(Engine::Particle::Particle& particle, float dt, Engine::Math::Material* /*materialData*/)
{
	particle.transform.translate += particle.Velocity * (dt / (1.0f / 60.0f));
	particle.Velocity.y -= 0.012f;
	particle.currentTime += dt;
}

} // namespace DirectXGame
