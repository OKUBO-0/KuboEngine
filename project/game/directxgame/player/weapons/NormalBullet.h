#pragma once

#include "Object3D.h"
#include "Vector3.h"
#include "game/directxgame/core/GameLightSettings.h"
#include <memory>
#include <unordered_map>

namespace DirectXGame {

class NormalBullet {
public:
	void InitializeForward(
		const Vector3& startPosition,
		const Vector3& forward,
		float speed = 1.0f,
		float range = 30.0f,
		int32_t maxHits = 1);
	void Update(const Vector3& playerPosition, float deltaTime);
	void Draw();

	bool IsActive() const { return active_; }
	void Deactivate() { active_ = false; }
	const Vector3& GetPosition() const { return position_; }
	float GetCollisionRadius() const;
	Engine::Math::AABB GetCollisionAabb() const;
	Engine::Math::OBB GetCollisionObb() const;

	bool CanHitEnemy(void* enemyPtr);
	void RegisterHit(void* enemyPtr);
	bool ConsumeHit();
	void SetLightSettings(const GameLightSettings& lightSettings);

private:
	void ApplyTransform();

	Vector3 position_{ 0.0f, 0.0f, 0.0f };
	Vector3 direction_{ 0.0f, 0.0f, 1.0f };
	float rotationY_ = 0.0f;
	float speed_ = 1.0f;
	float range_ = 30.0f;
	float traveled_ = 0.0f;
	int32_t remainingHits_ = 1;
	bool active_ = false;

	std::unique_ptr<Engine::Graphics3D::Object3D> object_;
	GameLightSettings lightSettings_{};
	std::unordered_map<void*, float> hitCooldowns_;
	static constexpr float kHitInterval = 0.5f;
};

} // namespace DirectXGame
