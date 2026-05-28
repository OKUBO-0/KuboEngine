#include "ParticleEmitter.h"
#include "ParticleManager.h"

namespace {
constexpr float kFixedEmitterDeltaTime = 1.0f / 60.0f;
constexpr float kEmitterResetTime = 0.0f;
}

namespace Engine::Particle {

ParticleEmitter::ParticleEmitter(const Vector3& position, float lifetime, float currentTime, uint32_t count, const std::string& name)
{
	position_ = position;//位置
	frequency = lifetime;//寿命
	frequencyTime = currentTime;//現在の寿命
	this->count = count;//count
	name_ = name;//名前
	
}

void ParticleEmitter::Update()
{
	// 時間を進める
	frequencyTime += kFixedEmitterDeltaTime;

	// 寿命（frequency）を超えたら発生
	if (frequencyTime >= frequency) {
		ParticleManager::GetInstance()->Emit(name_, position_, count);
		frequencyTime = kEmitterResetTime;
	}
}

void ParticleEmitter::Emit()
{

	//パーティクルを発生
	ParticleManager::GetInstance()->Emit(name_, position_, count);

}

}
