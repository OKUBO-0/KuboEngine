#pragma once

#include "Vector3.h"
#include "Vector4.h"
#include "EnemyBehavior.h"
#include "EnemyReactionController.h"
#include "EnemyView.h"
#include <cstdint>
#include <memory>

namespace DirectXGame {

class Player;

enum class BossAttackType : uint8_t {
	None,
	Beam,
	TripleBeam,
	LeapShockwave,
	SummonLeapShockwave,
	BulletHell,
	ConvergingShockwave,
	DomeBurst,
};

struct BossAttackEvent {
	BossAttackType type = BossAttackType::None;
	Vector3 position{};
	Vector3 direction{ 0.0f, 0.0f, 1.0f };
	Vector3 targetPosition{};
};

struct BossAttackTelegraph {
	BossAttackType type = BossAttackType::None;
	Vector3 position{};
	Vector3 direction{ 0.0f, 0.0f, 1.0f };
	Vector3 targetPosition{};
	float progress = 0.0f;
	float range = 0.0f;
};

class Enemy {
public:
	void Initialize();
	void Update(float deltaTime);
	void SetAnimationUpdateStride(uint32_t stride);
	void SetSimplifiedRenderEnabled(bool enabled);
	void Draw();
	void DrawFloatingShadow();
	void DrawModel();
	void DrawShadow();
	Engine::Graphics3D::Object3D* GetRenderObject() const;
	void StartDeathPresentation();
	void UpdateDeathPresentation(float elapsedTime, float duration);
	bool UpdateDeathPresentationFrame(float deltaTime, float duration);
	void FinishDeathPresentation();
	void NotifyAttack();
	void NotifyJump();

	void SetPosition(const Vector3& position);
	void SetRotationY(float rotationY);
	void SetPlayer(Player* player) { player_ = player; }
	void SetModelByType(int32_t type);
	void SetType(EnemyType type) { type_ = type; }
	bool IsDeathBombType() const { return type_ == EnemyType::Bomb; }
	void SetBehavior(EnemyBehaviorType type);
	void SetDeathBomb(float delay, float radius, int32_t damage)
	{
		deathBombDelay_ = delay;
		deathBombRadius_ = radius;
		deathBombDamage_ = damage;
	}
	float GetDeathBombDelay() const { return deathBombDelay_; }
	float GetDeathBombRadius() const { return deathBombRadius_; }
	int32_t GetDeathBombDamage() const { return deathBombDamage_; }
	void SetKnockbackResistance(float resistance)
	{
		reactionController_.SetKnockbackResistance(resistance);
	}
	bool IsSuicideType() const { return false; }

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
	void SetAttackPower(int32_t attackPower) { attackPower_ = attackPower; }
	int32_t GetAttackPower() const { return attackPower_; }
	void SetCoinValue(int32_t coinValue) { coinValue_ = coinValue; }
	int32_t GetCoinValue() const { return coinValue_; }
	bool JustDied() const { return justDied_; }
	void ResetJustDied() { justDied_ = false; }

	void SetSpeed(float speed) { speed_ = speed; }
	float GetSpeed() const { return speed_; }
	void SetBehaviorVisual(const Vector4& color, float scaleMultiplier = 1.0f);
	void ClearBehaviorVisual();
	void SetBoss(bool boss);
	void StartSpawnPresentation(float duration = 0.58f);
	bool IsBoss() const { return reactionController_.IsBoss(); }
	int32_t GetBossPhase() const
	{
		return reactionController_.GetBossPhase();
	}
	bool ConsumeBossPhaseChanged()
	{
		return reactionController_.ConsumeBossPhaseChanged();
	}
	void QueueBossAttack(
		BossAttackType type,
		const Vector3& direction,
		const Vector3& targetPosition = {});
	bool ConsumeBossAttack(BossAttackEvent& outEvent);
	void SetBossAttackTelegraph(
		BossAttackType type,
		const Vector3& direction,
		float progress,
		float range,
		const Vector3& targetPosition = {});
	const BossAttackTelegraph& GetBossAttackTelegraph() const
	{
		return bossAttackTelegraph_;
	}

private:
	Vector3 position_{ 0.0f, 0.0f, 0.0f };
	Vector3 previousPosition_{ 0.0f, 0.0f, 0.0f };
	float rotationY_ = 0.0f;
	float speed_ = 0.0f;
	int32_t hp_ = 0;
	int32_t maxHp_ = 0;
	int32_t exp_ = 0;
	int32_t attackPower_ = 10;
	int32_t coinValue_ = 0;
	EnemyType type_ = EnemyType::Standard;
	float deathBombDelay_ = 0.0f;
	float deathBombRadius_ = 0.0f;
	int32_t deathBombDamage_ = 0;
	bool active_ = true;
	bool justDied_ = false;
	bool deathPresentationActive_ = false;
	float deathPresentationElapsed_ = 0.0f;
	float spawnPresentationTimer_ = 0.0f;
	float spawnPresentationDuration_ = 0.0f;

	Player* player_ = nullptr;
	std::unique_ptr<IEnemyBehavior> behavior_;
	EnemyReactionController reactionController_{};
	EnemyView view_{};
	BossAttackEvent pendingBossAttack_{};
	BossAttackTelegraph bossAttackTelegraph_{};

	bool groundImpactPending_ = false;
};

} // namespace DirectXGame
