#include "EnemyReactionController.h"

#include <algorithm>
#include <cmath>

namespace {

float ScalePerFrameDecay(float decayPerFrame, float deltaTime)
{
	return std::pow(decayPerFrame, deltaTime / 0.016f);
}

}

namespace DirectXGame {

void EnemyReactionController::Reset()
{
	knockbackVelocity_ = { 0.0f, 0.0f, 0.0f };
	hitFlashTimer_ = 0.0f;
	knockbackTimer_ = 0.0f;
	knockbackCooldownTimer_ = 0.0f;
	knockbackResistance_ = 0.0f;
	knockbackAppliedThisFrame_ = false;
	boss_ = false;
	bossPhase_ = 1;
	bossPhaseChanged_ = false;
	bossPhaseTransitionTimer_ = 0.0f;
}

void EnemyReactionController::Update(
	float deltaTime,
	Vector3& position)
{
	knockbackAppliedThisFrame_ = knockbackTimer_ > 0.0f;
	hitFlashTimer_ =
		(std::max)(0.0f, hitFlashTimer_ - deltaTime);
	knockbackCooldownTimer_ =
		(std::max)(0.0f, knockbackCooldownTimer_ - deltaTime);
	bossPhaseTransitionTimer_ =
		(std::max)(0.0f, bossPhaseTransitionTimer_ - deltaTime);

	if (knockbackTimer_ <= 0.0f) {
		return;
	}

	const float velocityScale = deltaTime / 0.016f;
	position.x += knockbackVelocity_.x * velocityScale;
	position.z += knockbackVelocity_.z * velocityScale;
	const float knockbackDecay =
		ScalePerFrameDecay(0.88f, deltaTime);
	knockbackVelocity_.x *= knockbackDecay;
	knockbackVelocity_.z *= knockbackDecay;
	knockbackTimer_ -= deltaTime;
	if (knockbackTimer_ <= 0.0f) {
		knockbackVelocity_ = { 0.0f, 0.0f, 0.0f };
	}
}

void EnemyReactionController::ApplyHit(
	float hpRatio,
	const Vector3& knockDirection,
	float strength)
{
	hitFlashTimer_ = kHitFlashDuration;

	if (boss_) {
		const int32_t nextPhase =
			hpRatio <= 0.20f ? 5 :
			(hpRatio <= 0.40f ? 4 :
			(hpRatio <= 0.60f ? 3 :
			(hpRatio <= 0.80f ? 2 : 1)));
		if (nextPhase != bossPhase_) {
			bossPhase_ = nextPhase;
			bossPhaseChanged_ = true;
			bossPhaseTransitionTimer_ = 0.7f;
			knockbackVelocity_ = { 0.0f, 0.0f, 0.0f };
			knockbackTimer_ = 0.0f;
		}
		return;
	}

	const float length = std::sqrt(
		knockDirection.x * knockDirection.x +
		knockDirection.z * knockDirection.z);
	if (length <= 0.001f ||
		strength <= 0.0f ||
		knockbackCooldownTimer_ > 0.0f ||
		bossPhaseTransitionTimer_ > 0.0f) {
		return;
	}

	const float resistance =
		(boss_ ? 0.18f : 1.0f) * (1.0f - knockbackResistance_);
	knockbackVelocity_.x =
		(knockDirection.x / length) * strength * resistance;
	knockbackVelocity_.z =
		(knockDirection.z / length) * strength * resistance;
	knockbackTimer_ = kKnockbackDuration;
	knockbackCooldownTimer_ =
		boss_ ? 0.8f : kKnockbackCooldown;
}

void EnemyReactionController::SetBoss(bool boss)
{
	boss_ = boss;
	if (!boss_) {
		bossPhase_ = 1;
		bossPhaseChanged_ = false;
		bossPhaseTransitionTimer_ = 0.0f;
	}
}

void EnemyReactionController::SetKnockbackResistance(float resistance)
{
	knockbackResistance_ = std::clamp(resistance, 0.0f, 1.0f);
}

bool EnemyReactionController::ConsumeBossPhaseChanged()
{
	const bool changed = bossPhaseChanged_;
	bossPhaseChanged_ = false;
	return changed;
}

bool EnemyReactionController::IsBehaviorBlocked() const
{
	return !boss_ && (
		knockbackAppliedThisFrame_ ||
		bossPhaseTransitionTimer_ > 0.0f);
}

bool EnemyReactionController::IsPhaseFlashActive() const
{
	return bossPhaseTransitionTimer_ > 0.0f &&
		static_cast<int32_t>(
			bossPhaseTransitionTimer_ * 18.0f) %
			2 == 0;
}

} // namespace DirectXGame
