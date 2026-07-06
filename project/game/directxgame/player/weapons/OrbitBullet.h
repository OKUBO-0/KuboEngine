#pragma once

#include "Object3D.h"
#include "Vector3.h"
#include <memory>
#include <unordered_map>

namespace DirectXGame {

class OrbitBullet {
public:
	void Initialize(const Vector3& center, float radius, float angle, float angularSpeed, float scale = 1.0f, float hitInterval = 0.5f);
	void Update(const Vector3& center, float deltaTime);
	void ApplyRuntimeModifiers(
		float projectileSpeedMultiplier,
		float areaSizeMultiplier,
		float attackSpeedMultiplier);
	void Draw();

	bool IsActive() const { return active_; }
	const Vector3& GetPosition() const { return position_; }
	const Vector3& GetPreviousPosition() const { return previousPosition_; }
	float GetCollisionRadius() const;
	Engine::Math::AABB GetCollisionAabb() const;
	Engine::Math::OBB GetCollisionObb() const;
	bool CanHitEnemy(void* enemyPtr);
	void RegisterHit(void* enemyPtr);

private:
	void ApplyTransform();

	Vector3 position_{ 0.0f, 0.0f, 0.0f };
	Vector3 previousPosition_{ 0.0f, 0.0f, 0.0f };
	float orbitRadius_ = 10.0f;
	float angle_ = 0.0f;
	float angularSpeed_ = 0.05f;
	float scale_ = 1.0f;
	float hitInterval_ = 0.5f;
	float baseAngularSpeed_ = 0.05f;
	float baseScale_ = 1.0f;
	float baseHitInterval_ = 0.5f;
	bool active_ = false;

	std::unique_ptr<Engine::Graphics3D::Object3D> object_;
	std::unordered_map<void*, float> hitCooldowns_;
};

} // namespace DirectXGame
