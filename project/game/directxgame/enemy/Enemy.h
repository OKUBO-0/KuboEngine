#pragma once

#include "Vector3.h"
#include "Vector4.h"
#include "game/directxgame/enemy/EnemyBehavior.h"
#include "game/directxgame/enemy/EnemyReactionController.h"
#include "game/directxgame/enemy/EnemyView.h"
#include <cstdint>
#include <memory>

namespace DirectXGame {

class Player;

class Enemy {
public:
	void Initialize();
	void Update(float deltaTime);
	void Draw();
	void DrawShadow();
	void StartDeathPresentation();
	void UpdateDeathPresentation(float elapsedTime, float duration);

	void SetPosition(const Vector3& position);
	void SetRotationY(float rotationY);
	void SetPlayer(Player* player) { player_ = player; }
	void SetModelByType(int32_t type);
	void SetBehaviorByType(int32_t type);
	bool IsSuicideType() const { return type_ == 4; }

	const Vector3& GetPosition() const { return position_; }
	const Vector3& GetPreviousPosition() const { return previousPosition_; }
	float GetCollisionRadius() const;
	Engine::Math::AABB GetCollisionAabb() const;
	Engine::Math::OBB GetCollisionObb() const;
	Player* GetPlayer() const { return player_; }
	bool IsActive() const { return active_; }
	bool IsDeathPresentationActive() const { return deathPresentationActive_; }
	void Deactivate() { active_ = false; }
	void DeactivateOnGroundImpact() { active_ = false; groundImpactPending_ = true; }
	bool ConsumeGroundImpact();

	void SetHP(int32_t hp) { hp_ = hp; maxHp_ = hp; }
	int32_t GetHP() const { return hp_; }
	int32_t GetMaxHP() const { return maxHp_; }
	float GetHpRatio() const;
	void TakeDamage(int32_t damage, const Vector3& knockDirection = { 0.0f, 0.0f, 0.0f }, float strength = 0.0f);

	void SetEXP(int32_t exp) { exp_ = exp; }
	int32_t GetEXP() const { return exp_; }
	bool JustDied() const { return justDied_; }
	void ResetJustDied() { justDied_ = false; }

	void SetSpeed(float speed) { speed_ = speed; }
	float GetSpeed() const { return speed_; }
	void SetBehaviorVisual(const Vector4& color, float scaleMultiplier = 1.0f);
	void ClearBehaviorVisual();
	void SetBoss(bool boss);
	bool IsBoss() const { return reactionController_.IsBoss(); }
	int32_t GetBossPhase() const
	{
		return reactionController_.GetBossPhase();
	}
	bool ConsumeBossPhaseChanged()
	{
		return reactionController_.ConsumeBossPhaseChanged();
	}

private:
	Vector3 position_{ 0.0f, 0.0f, 0.0f };
	Vector3 previousPosition_{ 0.0f, 0.0f, 0.0f };
	float rotationY_ = 0.0f;
	float speed_ = 0.0f;
	int32_t hp_ = 0;
	int32_t maxHp_ = 0;
	int32_t exp_ = 0;
	int32_t type_ = 0;
	bool active_ = true;
	bool justDied_ = false;
	bool deathPresentationActive_ = false;

	Player* player_ = nullptr;
	std::unique_ptr<IEnemyBehavior> behavior_;
	EnemyReactionController reactionController_{};
	EnemyView view_{};

	bool groundImpactPending_ = false;
};

} // namespace DirectXGame
