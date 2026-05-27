#pragma once

#include "Object3D.h"
#include "Vector3.h"
#include "Vector4.h"
#include "game/directxgame/core/GameLightSettings.h"
#include "game/directxgame/enemy/EnemyBehavior.h"
#include <cstdint>
#include <memory>

namespace DirectXGame {

class Player;

class Enemy {
public:
	void Initialize();
	void Update(float deltaTime);
	void Draw();

	void SetPosition(const Vector3& position);
	void SetRotationY(float rotationY);
	void SetPlayer(Player* player) { player_ = player; }
	void SetModelByType(int32_t type);
	void SetBehaviorByType(int32_t type);
	void SetLightSettings(const GameLightSettings& lightSettings);

	const Vector3& GetPosition() const { return position_; }
	float GetCollisionRadius() const;
	Player* GetPlayer() const { return player_; }
	bool IsActive() const { return active_; }
	void Deactivate() { active_ = false; }

	void SetHP(int32_t hp) { hp_ = hp; }
	int32_t GetHP() const { return hp_; }
	void TakeDamage(int32_t damage, const Vector3& knockDirection = { 0.0f, 0.0f, 0.0f }, float strength = 0.0f);

	void SetEXP(int32_t exp) { exp_ = exp; }
	int32_t GetEXP() const { return exp_; }
	bool JustDied() const { return justDied_; }
	void ResetJustDied() { justDied_ = false; }

	void SetSpeed(float speed) { speed_ = speed; }
	float GetSpeed() const { return speed_; }
	void SetBehaviorVisual(const Vector4& color, float scaleMultiplier = 1.0f);
	void ClearBehaviorVisual();

private:
	void ApplyTransform();

	Vector3 position_{ 0.0f, 0.0f, 0.0f };
	float rotationY_ = 0.0f;
	float speed_ = 0.0f;
	int32_t hp_ = 0;
	int32_t exp_ = 0;
	bool active_ = true;
	bool justDied_ = false;

	Player* player_ = nullptr;
	std::unique_ptr<IEnemyBehavior> behavior_;
	std::unique_ptr<Engine::Graphics3D::Object3D> object_;
	GameLightSettings lightSettings_{};

	Vector4 behaviorColor_{ 1.0f, 1.0f, 1.0f, 1.0f };
	float behaviorScaleMultiplier_ = 1.0f;
	float hitFlashTimer_ = 0.0f;
	Vector3 knockbackVelocity_{ 0.0f, 0.0f, 0.0f };
	float knockbackTimer_ = 0.0f;

	static constexpr float kHitFlashDuration = 0.12f;
	static constexpr float kKnockbackDuration = 0.22f;
};

} // namespace DirectXGame
