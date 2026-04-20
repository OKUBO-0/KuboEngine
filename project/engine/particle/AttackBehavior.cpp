#include "AttackBehavior.h"
#include "ParticleManager.h"
#include <numbers>

namespace {
constexpr float kFixedParticleDeltaTime = 1.0f / 60.0f;
constexpr float kAttackBaseScaleX = 0.5f;
constexpr float kAttackBaseScaleZ = 1.0f;
constexpr float kAttackLifetime = 1.0f;
}

namespace Engine::Particle {

Particle AttackBehavior::Create(std::mt19937& randomEngine, const Vector3& pos)
{
	std::uniform_real_distribution<float>distribution(-1.0, 1.0f);
	std::uniform_real_distribution<float>distColor(0.0f, 1.0f);
	std::uniform_real_distribution<float>disRotate(-std::numbers::pi_v<float>, std::numbers::pi_v<float>);
	std::uniform_real_distribution<float>disScale(0.4f, 1.5f);

	Particle particle;

	particle.transform.scale = { kAttackBaseScaleX,disScale(randomEngine),kAttackBaseScaleZ };
	particle.transform.rotate = { disRotate(randomEngine),disRotate(randomEngine),disRotate(randomEngine) };
	particle.transform.translate = pos;
	particle.Velocity = { 0.0f,0.0f,0.0f };
	particle.color = { distColor(randomEngine),distColor(randomEngine),distColor(randomEngine),1.0f };
	particle.lifetime = kAttackLifetime;
	particle.currentTime = 0;
	return particle;
}

void AttackBehavior::Update(Particle& particle, float /*dt*/, ::Material* /*materialData*/)
{
	//パーティクルの位置を更新
	particle.transform.translate += particle.Velocity * kFixedParticleDeltaTime;
	//パーティクルの寿命を減らす
	particle.currentTime += kFixedParticleDeltaTime;
}

}
