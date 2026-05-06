#pragma once

#include "IParticleBehavior.h"
#include "Vector4.h"

namespace DirectXGame {

class RippleParticleBehavior : public Engine::Particle::IParticleBehavior {
public:
	explicit RippleParticleBehavior(const Vector4& color = { 0.45f, 0.75f, 1.0f, 1.0f });

	Engine::Particle::Particle Create(std::mt19937& rng, const Vector3& pos) override;
	void Update(Engine::Particle::Particle& particle, float dt, Engine::Math::Material* materialData) override;

private:
	Vector4 color_{ 0.45f, 0.75f, 1.0f, 1.0f };
};

class SparkParticleBehavior : public Engine::Particle::IParticleBehavior {
public:
	explicit SparkParticleBehavior(const Vector4& color = { 1.0f, 0.35f, 0.25f, 1.0f });

	Engine::Particle::Particle Create(std::mt19937& rng, const Vector3& pos) override;
	void Update(Engine::Particle::Particle& particle, float dt, Engine::Math::Material* materialData) override;

private:
	Vector4 color_{ 1.0f, 0.35f, 0.25f, 1.0f };
};

} // namespace DirectXGame
