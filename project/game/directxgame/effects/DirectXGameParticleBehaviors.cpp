#include "game/directxgame/effects/DirectXGameParticleBehaviors.h"
#include "ParticleManager.h"
#include <array>
#include <numbers>

namespace DirectXGame {

namespace {

constexpr std::array<Vector4, 6> kConfettiColors{
	Vector4{ 1.0f, 0.35f, 0.35f, 1.0f },
	Vector4{ 1.0f, 0.82f, 0.22f, 1.0f },
	Vector4{ 0.35f, 0.86f, 0.52f, 1.0f },
	Vector4{ 0.30f, 0.72f, 1.0f, 1.0f },
	Vector4{ 0.98f, 0.52f, 0.88f, 1.0f },
	Vector4{ 1.0f, 1.0f, 1.0f, 1.0f },
};

}

RippleParticleBehavior::RippleParticleBehavior(const Vector4& color, const Settings& settings)
	: color_(color)
	, settings_(settings)
{
}

Engine::Particle::Particle RippleParticleBehavior::Create(std::mt19937& rng, const Vector3& pos)
{
	std::uniform_real_distribution<float> scaleDist(settings_.initialScaleMin, settings_.initialScaleMax);
	std::uniform_real_distribution<float> rotateDist(-std::numbers::pi_v<float>, std::numbers::pi_v<float>);

	Engine::Particle::Particle particle{};
	const float scale = scaleDist(rng);
	particle.transform.scale = { scale, scale, scale };
	particle.transform.rotate = { std::numbers::pi_v<float> * 0.5f, 0.0f, rotateDist(rng) };
	particle.transform.translate = { pos.x, settings_.groundY, pos.z };
	particle.Velocity = { 0.0f, 0.0f, 0.0f };
	particle.color = color_;
	particle.lifetime = settings_.lifetime;
	particle.currentTime = 0.0f;
	return particle;
}

void RippleParticleBehavior::Update(Engine::Particle::Particle& particle, float dt, Engine::Math::Material* /*materialData*/)
{
	particle.currentTime += dt;
	const float progress = particle.currentTime / particle.lifetime;
	const float scale = 1.0f + progress * settings_.expandSpeed;
	particle.transform.scale = { scale, scale, scale };
}

SparkParticleBehavior::SparkParticleBehavior(const Vector4& color, const Settings& settings)
	: color_(color)
	, settings_(settings)
{
}

Engine::Particle::Particle SparkParticleBehavior::Create(std::mt19937& rng, const Vector3& pos)
{
	std::uniform_real_distribution<float> velocityDist(-settings_.horizontalSpeed, settings_.horizontalSpeed);
	std::uniform_real_distribution<float> upDist(settings_.verticalSpeedMin, settings_.verticalSpeedMax);
	std::uniform_real_distribution<float> scaleDist(settings_.scaleMin, settings_.scaleMax);
	std::uniform_real_distribution<float> rotateDist(-std::numbers::pi_v<float>, std::numbers::pi_v<float>);

	Engine::Particle::Particle particle{};
	const float scale = scaleDist(rng);
	particle.transform.scale = { scale, scale, scale };
	particle.transform.rotate = { rotateDist(rng), rotateDist(rng), rotateDist(rng) };
	particle.transform.translate = { pos.x, pos.y + settings_.yOffset, pos.z };
	particle.Velocity = { velocityDist(rng), upDist(rng), velocityDist(rng) };
	particle.color = color_;
	particle.lifetime = settings_.lifetime;
	particle.currentTime = 0.0f;
	return particle;
}

void SparkParticleBehavior::Update(Engine::Particle::Particle& particle, float dt, Engine::Math::Material* /*materialData*/)
{
	particle.transform.translate += particle.Velocity * (dt / (1.0f / 60.0f));
	particle.Velocity.y -= settings_.gravity;
	particle.currentTime += dt;
}

SmokeParticleBehavior::SmokeParticleBehavior(const Vector4& color, const Settings& settings)
	: color_(color)
	, settings_(settings)
{
}

Engine::Particle::Particle SmokeParticleBehavior::Create(std::mt19937& rng, const Vector3& pos)
{
	std::uniform_real_distribution<float> offsetDist(-settings_.offsetRange, settings_.offsetRange);
	std::uniform_real_distribution<float> velocityDist(-settings_.horizontalSpeed, settings_.horizontalSpeed);
	std::uniform_real_distribution<float> upDist(settings_.verticalSpeedMin, settings_.verticalSpeedMax);
	std::uniform_real_distribution<float> scaleDist(settings_.scaleMin, settings_.scaleMax);
	std::uniform_real_distribution<float> rotateDist(-std::numbers::pi_v<float>, std::numbers::pi_v<float>);

	Engine::Particle::Particle particle{};
	const float scale = scaleDist(rng);
	particle.transform.scale = { scale, scale, scale };
	particle.transform.rotate = { rotateDist(rng), rotateDist(rng), rotateDist(rng) };
	particle.transform.translate = { pos.x + offsetDist(rng), pos.y + settings_.yOffset, pos.z + offsetDist(rng) };
	particle.Velocity = { velocityDist(rng), upDist(rng), velocityDist(rng) };
	particle.color = color_;
	particle.lifetime = settings_.lifetime;
	particle.currentTime = 0.0f;
	return particle;
}

void SmokeParticleBehavior::Update(Engine::Particle::Particle& particle, float dt, Engine::Math::Material* /*materialData*/)
{
	const float fixedStepScale = dt / (1.0f / 60.0f);
	particle.transform.translate += particle.Velocity * fixedStepScale;
	const float scaleStep = settings_.scaleGrow * fixedStepScale;
	particle.transform.scale.x += scaleStep;
	particle.transform.scale.y += scaleStep;
	particle.transform.scale.z += scaleStep;
	particle.transform.rotate.z += 0.025f * fixedStepScale;
	particle.currentTime += dt;
}

ConfettiParticleBehavior::ConfettiParticleBehavior(const Settings& settings)
	: settings_(settings)
{
}

Engine::Particle::Particle ConfettiParticleBehavior::Create(std::mt19937& rng, const Vector3& pos)
{
	std::uniform_real_distribution<float> offsetDist(-5.5f, 5.5f);
	std::uniform_real_distribution<float> velocityXDist(-0.16f, 0.16f);
	std::uniform_real_distribution<float> velocityYDist(0.18f, 0.34f);
	std::uniform_real_distribution<float> velocityZDist(-0.12f, 0.12f);
	std::uniform_real_distribution<float> scaleXDist(0.12f, 0.24f);
	std::uniform_real_distribution<float> scaleYDist(0.28f, 0.52f);
	std::uniform_real_distribution<float> rotateDist(-std::numbers::pi_v<float>, std::numbers::pi_v<float>);
	std::uniform_real_distribution<float> lifetimeDist(settings_.lifetimeMin, settings_.lifetimeMax);
	std::uniform_int_distribution<size_t> colorIndexDist(0, kConfettiColors.size() - 1);

	Engine::Particle::Particle particle{};
	particle.transform.scale = {
		scaleXDist(rng) * settings_.scaleMultiplier,
		scaleYDist(rng) * settings_.scaleMultiplier,
		1.0f
	};
	particle.transform.rotate = { rotateDist(rng), rotateDist(rng), rotateDist(rng) };
	particle.transform.translate = { pos.x + offsetDist(rng), pos.y + 1.2f, pos.z + offsetDist(rng) };
	particle.Velocity = {
		velocityXDist(rng) * settings_.velocityScale,
		velocityYDist(rng) * settings_.velocityScale,
		velocityZDist(rng) * settings_.velocityScale
	};
	particle.color = kConfettiColors[colorIndexDist(rng)];
	particle.lifetime = lifetimeDist(rng);
	particle.currentTime = 0.0f;
	return particle;
}

void ConfettiParticleBehavior::Update(Engine::Particle::Particle& particle, float dt, Engine::Math::Material* /*materialData*/)
{
	const float fixedStepScale = dt / (1.0f / 60.0f);
	particle.transform.translate += particle.Velocity * fixedStepScale;
	particle.Velocity.y -= settings_.gravity;
	particle.transform.rotate.x += 0.13f * fixedStepScale;
	particle.transform.rotate.y += 0.09f * fixedStepScale;
	particle.transform.rotate.z += 0.17f * fixedStepScale;
	particle.currentTime += dt;
}

} // namespace DirectXGame
