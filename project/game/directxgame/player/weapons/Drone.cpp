#include "game/directxgame/player/weapons/Drone.h"
#include "game/directxgame/core/GameModelCache.h"
#include "Object3DCommon.h"
#include <algorithm>
#include <cmath>

namespace {

constexpr char kEnvironmentTexturePath[] = "Resources/textures/skybox/test.dds";

}

namespace DirectXGame {

void Drone::Initialize(const Vector3& offset)
{
	offset_ = offset;

	const ModelHandle droneHandle = GameModelCache::Load("cube.obj");
	object_ = std::make_unique<Engine::Graphics3D::Object3D>();
	object_->Initialize(Engine::Graphics3D::Object3DCommon::GetInstance());
	GameModelCache::ApplyToObject(*object_, droneHandle);
	object_->SetSkyboxFilePath(kEnvironmentTexturePath);
	object_->SetEnvironmentReflectionStrength(0.0f);
	object_->SetEnvironmentRoughness(1.0f);
	object_->SetScale({ 0.5f, 0.5f, 0.5f });
}

void Drone::Update(
	const Vector3& playerPosition,
	float playerRotationY,
	float fireAngleY,
	float& fireTimer,
	float fireInterval,
	int32_t shotCount,
	float bulletSpeed,
	float bulletRange,
	int32_t bulletPierceCount,
	float deltaTime)
{
	animationTime_ += deltaTime * 60.0f;
	const float floatY = std::sin(animationTime_ * 0.05f) * 0.5f;

	position_ = {
		playerPosition.x + offset_.x,
		playerPosition.y + offset_.y + floatY,
		playerPosition.z + offset_.z,
	};
	rotationY_ = playerRotationY;

	fireTimer += deltaTime;
	if (fireTimer >= fireInterval) {
		FireForward(fireAngleY, shotCount, bulletSpeed, bulletRange, bulletPierceCount);
		fireTimer = 0.0f;
	}

	for (std::unique_ptr<NormalBullet>& bullet : bullets_) {
		bullet->Update(position_, deltaTime);
	}
	RecycleInactiveBullets();

	ApplyTransform();
	if (object_) {
		object_->Update();
	}
}

void Drone::Draw()
{
	if (object_) {
		object_->Draw();
	}
	for (std::unique_ptr<NormalBullet>& bullet : bullets_) {
		bullet->Draw();
	}
}

void Drone::ApplyTransform()
{
	if (!object_) {
		return;
	}
	object_->SetRotate({ 0.0f, rotationY_, 0.0f });
	object_->SetTranslate(position_);
}

void Drone::FireForward(float angle, int32_t shotCount, float bulletSpeed, float bulletRange, int32_t bulletPierceCount)
{
	const float centerOffset = static_cast<float>(shotCount - 1) * 0.5f;
	constexpr float kSpreadRadians = 0.14f;
	for (int32_t i = 0; i < shotCount; ++i) {
		const float spread = (static_cast<float>(i) - centerOffset) * kSpreadRadians;
		const float shotAngle = angle + spread;
		Vector3 shotDirection{ std::sin(shotAngle), 0.0f, std::cos(shotAngle) };
		NormalBullet& bullet = AcquireBullet();
		bullet.InitializeForward(
			position_,
			shotDirection,
			bulletSpeed,
			bulletRange,
			bulletPierceCount,
			{ 0.45f, 0.88f, 1.0f, 0.82f });
		peakBulletCount_ = (std::max)(peakBulletCount_, bullets_.size());
		if (bullets_.size() > Drone::kMaxActiveBullets) {
			RecycleBullet(0);
			++bulletPruneCount_;
		}
	}
}

NormalBullet& Drone::AcquireBullet()
{
	if (bulletPool_.empty()) {
		bullets_.push_back(std::make_unique<NormalBullet>());
		return *bullets_.back();
	}

	bullets_.push_back(std::move(bulletPool_.back()));
	bulletPool_.pop_back();
	return *bullets_.back();
}

void Drone::RecycleBullet(size_t index)
{
	if (index >= bullets_.size()) {
		return;
	}

	if (bullets_[index]) {
		bullets_[index]->Deactivate();
		bulletPool_.push_back(std::move(bullets_[index]));
	}
	if (index != bullets_.size() - 1) {
		bullets_[index] = std::move(bullets_.back());
	}
	bullets_.pop_back();
}

void Drone::RecycleInactiveBullets()
{
	for (size_t index = 0; index < bullets_.size();) {
		if (bullets_[index] && bullets_[index]->IsActive()) {
			++index;
			continue;
		}
		RecycleBullet(index);
	}
}

} // namespace DirectXGame
