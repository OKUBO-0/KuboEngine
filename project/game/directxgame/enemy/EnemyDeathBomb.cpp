#include "EnemyDeathBomb.h"

#include "GameModelCache.h"
#include "Object3D.h"
#include "Object3DCommon.h"
#include <algorithm>
#include <cmath>

namespace DirectXGame {

void EnemyDeathBomb::Initialize(
	const Vector3& position,
	float delay,
	float radius,
	int32_t damage)
{
	position_ = position;
	delay_ = (std::max)(0.1f, delay);
	radius_ = (std::max)(0.5f, radius);
	damage_ = (std::max)(1, damage);
	elapsedTime_ = 0.0f;
	exploded_ = false;

	object_ = std::make_unique<Engine::Graphics3D::Object3D>();
	object_->Initialize(Engine::Graphics3D::Object3DCommon::GetInstance());
	GameModelCache::ApplyToObject(*object_, GameModelCache::Load("cube.obj"));
	object_->SetLighting(false);
	object_->SetEnvironmentReflectionStrength(0.0f);
	object_->SetEnvironmentRoughness(1.0f);
	object_->SetTranslate({ position_.x, position_.y + 0.55f, position_.z });
	object_->SetColor({ 1.0f, 0.32f, 0.04f, 0.65f });
	object_->Update();
}

bool EnemyDeathBomb::Update(float deltaTime)
{
	if (exploded_) {
		return false;
	}

	elapsedTime_ += (std::max)(0.0f, deltaTime);
	const float progress = std::clamp(elapsedTime_ / delay_, 0.0f, 1.0f);
	const float pulse = 0.88f + std::sin(elapsedTime_ * 22.0f) * 0.12f;
	if (object_) {
		const float markerScale = radius_ * (0.075f + progress * 0.045f) * pulse;
		object_->SetScale({ markerScale, markerScale * 0.55f, markerScale });
		object_->SetColor({ 1.0f, 0.34f - progress * 0.18f, 0.03f, 0.48f + progress * 0.42f });
		object_->Update();
	}

	if (elapsedTime_ < delay_) {
		return false;
	}
	exploded_ = true;
	return true;
}

void EnemyDeathBomb::Draw() const
{
	if (object_ && !exploded_) {
		object_->Draw();
	}
}

} // namespace DirectXGame
