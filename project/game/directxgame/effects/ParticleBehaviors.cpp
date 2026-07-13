#include "ParticleBehaviors.h"
#include "ParticleManager.h"
#include <algorithm>
#include <array>
#include <cmath>
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

Vector3 SafeNormalize(const Vector3& value, const Vector3& fallback)
{
	const float lengthSquared =
		value.x * value.x +
		value.y * value.y +
		value.z * value.z;
	if (lengthSquared <= 0.0001f) {
		return fallback;
	}

	const float invLength = 1.0f / std::sqrt(lengthSquared);
	return {
		value.x * invLength,
		value.y * invLength,
		value.z * invLength,
	};
}

}

RippleParticleBehavior::RippleParticleBehavior(const Vector4& color, const Settings& settings)
	: color_(color)
	, settings_(settings)
{
}

Engine::Particle::Particle RippleParticleBehavior::Create(std::mt19937& rng, const Vector3& pos)
{
	std::uniform_real_distribution<float> scaleDist(settings_.initialScaleMin, settings_.initialScaleMax);

	Engine::Particle::Particle particle{};
	const float scale = scaleDist(rng);
	particle.transform.scale = { scale, scale, scale };
	particle.transform.rotate = { std::numbers::pi_v<float> * 0.5f, 0.0f, 0.0f };
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
	particle.transform.rotate = { std::numbers::pi_v<float> * 0.5f, 0.0f, 0.0f };
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

ExplosionBurstParticleBehavior::ExplosionBurstParticleBehavior(
	const Vector4& color,
	const Settings& settings)
	: color_(color)
	, settings_(settings)
{
}

Engine::Particle::Particle ExplosionBurstParticleBehavior::Create(
	std::mt19937& rng,
	const Vector3& pos)
{
	std::uniform_real_distribution<float> angleDist(
		0.0f,
		std::numbers::pi_v<float> * 2.0f);
	std::uniform_real_distribution<float> horizontalDist(
		settings_.horizontalSpeedMin,
		settings_.horizontalSpeedMax);
	std::uniform_real_distribution<float> verticalDist(
		settings_.verticalSpeedMin,
		settings_.verticalSpeedMax);
	std::uniform_real_distribution<float> scaleDist(
		settings_.scaleMin,
		settings_.scaleMax);
	std::uniform_real_distribution<float> colorMixDist(0.0f, 1.0f);

	const float angle = angleDist(rng);
	const float horizontalSpeed = horizontalDist(rng);
	const float scale = scaleDist(rng);
	const float whiteMix = colorMixDist(rng) * 0.42f;

	Engine::Particle::Particle particle{};
	particle.transform.scale = { scale, scale, scale };
	particle.transform.rotate = { angle, angle * 0.5f, angle };
	particle.transform.translate = {
		pos.x,
		pos.y + settings_.yOffset,
		pos.z,
	};
	particle.Velocity = {
		std::cos(angle) * horizontalSpeed,
		verticalDist(rng),
		std::sin(angle) * horizontalSpeed,
	};
	particle.color = {
		color_.x + (1.0f - color_.x) * whiteMix,
		color_.y + (1.0f - color_.y) * whiteMix,
		color_.z + (1.0f - color_.z) * whiteMix,
		color_.w,
	};
	particle.lifetime = settings_.lifetime;
	particle.currentTime = 0.0f;
	return particle;
}

void ExplosionBurstParticleBehavior::Update(
	Engine::Particle::Particle& particle,
	float dt,
	Engine::Math::Material* /*materialData*/)
{
	const float fixedStepScale = dt / (1.0f / 60.0f);
	particle.transform.translate += particle.Velocity * fixedStepScale;
	particle.Velocity.y -= settings_.gravity * fixedStepScale;
	const float grow = settings_.scaleGrow * fixedStepScale;
	particle.transform.scale.x += grow;
	particle.transform.scale.y += grow;
	particle.transform.scale.z += grow;
	particle.transform.rotate.z += 0.09f * fixedStepScale;
	particle.currentTime += dt;
	const float progress = std::clamp(
		particle.currentTime / particle.lifetime,
		0.0f,
		1.0f);
	particle.color.w = color_.w * std::pow(1.0f - progress, 1.35f);
}

ConfettiParticleBehavior::ConfettiParticleBehavior(const Settings& settings)
	: settings_(settings)
{
}

Engine::Particle::Particle ConfettiParticleBehavior::Create(std::mt19937& rng, const Vector3& pos)
{
	std::uniform_real_distribution<float> horizontalOffsetDist(-settings_.horizontalOffsetRange, settings_.horizontalOffsetRange);
	std::uniform_real_distribution<float> depthOffsetDist(-settings_.depthOffsetRange, settings_.depthOffsetRange);
	std::uniform_real_distribution<float> sideVelocityDist(-0.10f, 0.10f);
	std::uniform_real_distribution<float> upVelocityDist(0.24f, 0.46f);
	std::uniform_real_distribution<float> depthVelocityDist(-0.04f, 0.04f);
	std::uniform_real_distribution<float> scaleXDist(0.16f, 0.34f);
	std::uniform_real_distribution<float> scaleYDist(0.34f, 0.68f);
	std::uniform_real_distribution<float> tiltDist(-0.35f, 0.35f);
	std::uniform_real_distribution<float> rotateZDist(-std::numbers::pi_v<float>, std::numbers::pi_v<float>);
	std::uniform_real_distribution<float> lifetimeDist(settings_.lifetimeMin, settings_.lifetimeMax);
	std::uniform_int_distribution<size_t> colorIndexDist(0, kConfettiColors.size() - 1);

	const Vector3 horizontalAxis = SafeNormalize(settings_.horizontalAxis, { 1.0f, 0.0f, 0.0f });
	const Vector3 verticalAxis = SafeNormalize(settings_.verticalAxis, { 0.0f, 1.0f, 0.0f });
	const Vector3 depthAxis = SafeNormalize(settings_.depthAxis, { 0.0f, 0.0f, 1.0f });

	Engine::Particle::Particle particle{};
	particle.transform.scale = {
		scaleXDist(rng) * settings_.scaleMultiplier,
		scaleYDist(rng) * settings_.scaleMultiplier,
		1.0f
	};
	particle.transform.rotate = { tiltDist(rng), tiltDist(rng), rotateZDist(rng) };
	particle.transform.translate =
		pos +
		horizontalAxis * horizontalOffsetDist(rng) +
		verticalAxis * settings_.yOffset +
		depthAxis * depthOffsetDist(rng);
	particle.Velocity =
		(horizontalAxis * sideVelocityDist(rng) +
			verticalAxis * upVelocityDist(rng) +
			depthAxis * depthVelocityDist(rng)) *
		settings_.velocityScale;
	particle.color = kConfettiColors[colorIndexDist(rng)];
	particle.lifetime = lifetimeDist(rng);
	particle.currentTime = 0.0f;
	return particle;
}

void ConfettiParticleBehavior::Update(Engine::Particle::Particle& particle, float dt, Engine::Math::Material* /*materialData*/)
{
	const float fixedStepScale = dt / (1.0f / 60.0f);
	const Vector3 verticalAxis = SafeNormalize(settings_.verticalAxis, { 0.0f, 1.0f, 0.0f });
	particle.transform.translate += particle.Velocity * fixedStepScale;
	particle.Velocity =
		particle.Velocity - verticalAxis * (settings_.gravity * fixedStepScale);
	particle.transform.rotate.x += 0.13f * fixedStepScale;
	particle.transform.rotate.y += 0.09f * fixedStepScale;
	particle.transform.rotate.z += 0.17f * fixedStepScale;
	particle.currentTime += dt;
}

TrailParticleBehavior::TrailParticleBehavior(const Vector4& color, const Settings& settings)
	: color_(color)
	, settings_(settings)
{
}

Engine::Particle::Particle TrailParticleBehavior::Create(std::mt19937& rng, const Vector3& pos)
{
	std::uniform_real_distribution<float> scaleDist(settings_.scaleMin, settings_.scaleMax);
	std::uniform_real_distribution<float> rotateDist(-std::numbers::pi_v<float>, std::numbers::pi_v<float>);

	Engine::Particle::Particle particle{};
	const float scale = scaleDist(rng);
	particle.transform.scale = { scale, scale, scale };
	particle.transform.rotate = { 0.0f, rotateDist(rng), rotateDist(rng) };
	particle.transform.translate = { pos.x, pos.y + settings_.yOffset, pos.z };
	particle.Velocity = { 0.0f, 0.0f, 0.0f };
	particle.color = color_;
	particle.lifetime = settings_.lifetime;
	particle.currentTime = 0.0f;
	return particle;
}

void TrailParticleBehavior::Update(Engine::Particle::Particle& particle, float dt, Engine::Math::Material* /*materialData*/)
{
	const float fixedStepScale = dt / (1.0f / 60.0f);
	const float shrink = std::pow(settings_.shrinkRate, fixedStepScale);
	particle.transform.scale.x *= shrink;
	particle.currentTime += dt;

	const float progress = std::clamp(particle.currentTime / particle.lifetime, 0.0f, 1.0f);
	const float fadeIn = settings_.fadeInRatio > 0.0f
		? std::clamp(progress / settings_.fadeInRatio, 0.0f, 1.0f)
		: 1.0f;
	const float remaining = 1.0f - progress;
	const float additionalFadeOut = std::pow(remaining, (std::max)(0.0f, settings_.fadeOutPower - 1.0f));
	particle.color.w = color_.w * fadeIn * additionalFadeOut;
}

LightningImpactParticleBehavior::LightningImpactParticleBehavior(
	const Vector4& color,
	const Settings& settings)
	: color_(color)
	, settings_(settings)
{
}

Engine::Particle::Particle LightningImpactParticleBehavior::Create(std::mt19937& rng, const Vector3& pos)
{
	std::uniform_real_distribution<float> angleDist(0.0f, std::numbers::pi_v<float> * 2.0f);
	std::uniform_real_distribution<float> horizontalDist(settings_.horizontalSpeedMin, settings_.horizontalSpeedMax);
	std::uniform_real_distribution<float> verticalDist(settings_.verticalSpeedMin, settings_.verticalSpeedMax);
	std::uniform_real_distribution<float> scaleDist(settings_.scaleMin, settings_.scaleMax);
	std::uniform_real_distribution<float> colorMixDist(0.0f, 1.0f);

	const float angle = angleDist(rng);
	const float horizontalSpeed = horizontalDist(rng);
	const float scale = scaleDist(rng);
	const float whiteMix = colorMixDist(rng) * 0.55f;

	Engine::Particle::Particle particle{};
	particle.transform.scale = { scale * 0.45f, scale * 2.8f, scale * 0.45f };
	particle.transform.rotate = { 0.0f, angle, angle };
	particle.transform.translate = { pos.x, pos.y + 0.25f, pos.z };
	particle.Velocity = {
		std::cos(angle) * horizontalSpeed,
		verticalDist(rng),
		std::sin(angle) * horizontalSpeed,
	};
	particle.color = {
		color_.x + (1.0f - color_.x) * whiteMix,
		color_.y + (1.0f - color_.y) * whiteMix,
		color_.z + (1.0f - color_.z) * whiteMix,
		color_.w,
	};
	particle.lifetime = settings_.lifetime;
	particle.currentTime = 0.0f;
	return particle;
}

void LightningImpactParticleBehavior::Update(
	Engine::Particle::Particle& particle,
	float dt,
	Engine::Math::Material* /*materialData*/)
{
	const float fixedStepScale = dt / (1.0f / 60.0f);
	particle.transform.translate += particle.Velocity * fixedStepScale;
	particle.Velocity.y -= settings_.gravity * fixedStepScale;
	particle.transform.scale.x *= std::pow(0.9f, fixedStepScale);
	particle.transform.scale.z = particle.transform.scale.x;
	particle.currentTime += dt;
}

} // namespace DirectXGame
