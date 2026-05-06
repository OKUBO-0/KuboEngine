#pragma once

#include "Object3D.h"
#include "Vector3.h"
#include "game/directxgame/core/GameLightSettings.h"
#include "game/directxgame/player/weapons/NormalBullet.h"
#include <memory>
#include <vector>

namespace DirectXGame {

class Drone {
public:
	void Initialize(const Vector3& offset);
	void Update(
		const Vector3& playerPosition,
		float playerRotationY,
		float fireAngleY,
		float& fireTimer,
		float fireInterval,
		int32_t shotCount,
		float bulletSpeed,
		float bulletRange,
		int32_t bulletPierceCount,
		float deltaTime);
	void Draw();

	const std::vector<std::unique_ptr<NormalBullet>>& GetBullets() const { return bullets_; }
	const Vector3& GetPosition() const { return position_; }
	void SetLightSettings(const GameLightSettings& lightSettings);

private:
	void ApplyTransform();
	void FireForward(float angle, int32_t shotCount, float bulletSpeed, float bulletRange, int32_t bulletPierceCount);

	Vector3 position_{ 0.0f, 0.0f, 0.0f };
	Vector3 offset_{ 3.0f, 2.0f, 0.0f };
	float rotationY_ = 0.0f;
	float animationTime_ = 0.0f;

	std::unique_ptr<Engine::Graphics3D::Object3D> object_;
	std::vector<std::unique_ptr<NormalBullet>> bullets_;
	GameLightSettings lightSettings_{};
};

} // namespace DirectXGame
