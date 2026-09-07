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
	SetName(name);
	
}

void ParticleEmitter::Update()
{
	// 時間を進める
	frequencyTime += kFixedEmitterDeltaTime;

	// 寿命（frequency）を超えたら発生
	if (frequencyTime >= frequency) {
		Emit();
		frequencyTime = kEmitterResetTime;
	}
}

void ParticleEmitter::Emit()
{

	//パーティクルを発生
	ParticleManager::GetInstance()->Emit(groupHandle_, position_, count);

}

void ParticleEmitter::SetName(const std::string& name)
{
	name_ = name;
	const std::optional<ParticleGroupHandle> handle =
		ParticleManager::GetInstance()->GetParticleGroupHandle(name_);
	groupHandle_ = handle.value_or(ParticleGroupHandle{});
}

}
