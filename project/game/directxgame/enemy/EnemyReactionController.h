#pragma once

#include "Vector3.h"
#include <cstdint>

namespace DirectXGame {

class EnemyReactionController final {
public:
	void Reset();
	void Update(float deltaTime, Vector3& position);
	void ApplyHit(
		float hpRatio,
		const Vector3& knockDirection,
		float strength);

	void SetBoss(bool boss);
	void SetKnockbackResistance(float resistance);
	bool IsBoss() const { return boss_; }
	int32_t GetBossPhase() const { return bossPhase_; }
	bool ConsumeBossPhaseChanged();
	bool IsBehaviorBlocked() const;
	bool IsHitFlashActive() const { return hitFlashTimer_ > 0.0f; }
	bool IsPhaseFlashActive() const;

private:
	Vector3 knockbackVelocity_{ 0.0f, 0.0f, 0.0f };
	float hitFlashTimer_ = 0.0f;
	float knockbackTimer_ = 0.0f;
	float knockbackCooldownTimer_ = 0.0f;
	float knockbackResistance_ = 0.0f;
	bool knockbackAppliedThisFrame_ = false;
	bool boss_ = false;
	int32_t bossPhase_ = 1;
	bool bossPhaseChanged_ = false;
	float bossPhaseTransitionTimer_ = 0.0f;

	static constexpr float kHitFlashDuration = 0.12f;
	static constexpr float kKnockbackDuration = 0.22f;
	static constexpr float kKnockbackCooldown = 0.45f;
};

} // namespace DirectXGame
