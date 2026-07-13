#pragma once

#include "Object3D.h"
#include "Vector3.h"
#include "Vector4.h"
#include <memory>
#include <string>
#include <unordered_map>

namespace DirectXGame {

class NormalBullet {
public:
	enum class MovementMode {
		Straight,
		ReturnToPlayer,
	};
	struct VisualStyle {
		const char* modelPath = "bullet.obj";
		Vector4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
		Vector3 scaleMultiplier{ 1.0f, 1.0f, 1.0f };
	};
	void InitializeForward(
		const Vector3& startPosition,
		const Vector3& forward,
		float speed = 1.0f,
		float range = 30.0f,
		int32_t maxHits = 1,
		float scale = 1.0f,
		MovementMode movementMode = MovementMode::Straight,
		const VisualStyle& visualStyle = {});
	void Update(const Vector3& playerPosition, float deltaTime);
	void Draw();

	bool IsActive() const { return active_; }
	void Deactivate()
	{
		active_ = false;
	}
	const Vector3& GetPosition() const { return position_; }
	const Vector3& GetPreviousPosition() const { return previousPosition_; }
	float GetCollisionRadius() const;
	Engine::Math::AABB GetCollisionAabb() const;
	Engine::Math::OBB GetCollisionObb() const;

	bool CanHitEnemy(void* enemyPtr);
	void RegisterHit(void* enemyPtr);
	bool ConsumeHit();
	void RedirectToward(const Vector3& targetPosition);

private:
	void ApplyTransform();

	Vector3 position_{ 0.0f, 0.0f, 0.0f };
	Vector3 previousPosition_{ 0.0f, 0.0f, 0.0f };
	Vector3 direction_{ 0.0f, 0.0f, 1.0f };
	float rotationY_ = 0.0f;
	float speed_ = 1.0f;
	float range_ = 30.0f;
	float traveled_ = 0.0f;
	float scale_ = 1.0f;
	float spinAngle_ = 0.0f;
	int32_t remainingHits_ = 1;
	bool active_ = false;
	MovementMode movementMode_ = MovementMode::Straight;
	bool returning_ = false;
	VisualStyle visualStyle_{};
	std::string modelPath_{};

	std::unique_ptr<Engine::Graphics3D::Object3D> object_;
	std::unordered_map<void*, float> hitCooldowns_;
	static constexpr float kHitInterval = 0.5f;
};

} // namespace DirectXGame
