#pragma once

#include "IParticleBehavior.h"
#include "Vector4.h"

namespace DirectXGame {

class RippleParticleBehavior : public Engine::Particle::IParticleBehavior {
public:
	struct Settings {
		float lifetime = 0.42f;
		float initialScaleMin = 0.75f;
		float initialScaleMax = 1.35f;
		float expandSpeed = 4.5f;
		float groundY = -1.82f;
	};

	explicit RippleParticleBehavior(
		const Vector4& color = { 0.45f, 0.75f, 1.0f, 1.0f },
		const Settings& settings = Settings{});

	Engine::Particle::Particle Create(std::mt19937& rng, const Vector3& pos) override;
	void Update(Engine::Particle::Particle& particle, float dt, Engine::Math::Material* materialData) override;

private:
	Vector4 color_{ 0.45f, 0.75f, 1.0f, 1.0f };
	Settings settings_{};
};

class SparkParticleBehavior : public Engine::Particle::IParticleBehavior {
public:
	struct Settings {
		float lifetime = 0.35f;
		float horizontalSpeed = 0.18f;
		float verticalSpeedMin = 0.06f;
		float verticalSpeedMax = 0.22f;
		float scaleMin = 0.18f;
		float scaleMax = 0.34f;
		float gravity = 0.012f;
		float yOffset = 0.8f;
	};

	explicit SparkParticleBehavior(
		const Vector4& color = { 1.0f, 0.35f, 0.25f, 1.0f },
		const Settings& settings = Settings{});

	Engine::Particle::Particle Create(std::mt19937& rng, const Vector3& pos) override;
	void Update(Engine::Particle::Particle& particle, float dt, Engine::Math::Material* materialData) override;

private:
	Vector4 color_{ 1.0f, 0.35f, 0.25f, 1.0f };
	Settings settings_{};
};

class SmokeParticleBehavior : public Engine::Particle::IParticleBehavior {
public:
	struct Settings {
		float lifetime = 0.72f;
		float offsetRange = 0.65f;
		float horizontalSpeed = 0.035f;
		float verticalSpeedMin = 0.025f;
		float verticalSpeedMax = 0.075f;
		float scaleMin = 0.45f;
		float scaleMax = 0.85f;
		float scaleGrow = 0.018f;
		float yOffset = 0.75f;
	};

	explicit SmokeParticleBehavior(
		const Vector4& color = { 0.45f, 0.42f, 0.38f, 0.85f },
		const Settings& settings = Settings{});

	Engine::Particle::Particle Create(std::mt19937& rng, const Vector3& pos) override;
	void Update(Engine::Particle::Particle& particle, float dt, Engine::Math::Material* materialData) override;

private:
	Vector4 color_{ 0.45f, 0.42f, 0.38f, 0.85f };
	Settings settings_{};
};

class ConfettiParticleBehavior : public Engine::Particle::IParticleBehavior {
public:
	struct Settings {
		float lifetimeMin = 0.85f;
		float lifetimeMax = 1.45f;
		float velocityScale = 1.0f;
		float scaleMultiplier = 1.0f;
		float gravity = 0.010f;
	};

	explicit ConfettiParticleBehavior(const Settings& settings = Settings{});

	Engine::Particle::Particle Create(std::mt19937& rng, const Vector3& pos) override;
	void Update(Engine::Particle::Particle& particle, float dt, Engine::Math::Material* materialData) override;

private:
	Settings settings_{};
};

} // namespace DirectXGame
