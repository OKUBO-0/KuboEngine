#include "game/directxgame/enemy/Enemy.h"
#include "game/directxgame/core/GameAudioCache.h"
#include <algorithm>

namespace {

constexpr char kDeathSePath[] = "audio/se/se_death.wav";
constexpr char kAudioEnemyDeath[] = "combat.enemyDeath";

}

namespace DirectXGame {

void Enemy::Initialize()
{
	active_ = true;
	justDied_ = false;
	deathPresentationActive_ = false;
	groundImpactPending_ = false;
	reactionController_.Reset();

	view_.Initialize();
	view_.SetFloatingEnabled(false);
	view_.ClearBehaviorVisual();
	view_.Update(position_, rotationY_, false, false);
}

void Enemy::Update(float deltaTime)
{
	if (!active_) {
		return;
	}

	previousPosition_ = position_;
	ClearBehaviorVisual();

	reactionController_.Update(deltaTime, position_);
	if (behavior_ && !reactionController_.IsBehaviorBlocked()) {
		behavior_->Update(*this, deltaTime);
	}

	view_.Update(
		position_,
		rotationY_,
		reactionController_.IsHitFlashActive(),
		reactionController_.IsPhaseFlashActive());
}

void Enemy::Draw()
{
	view_.Draw(active_ || deathPresentationActive_);
}

void Enemy::DrawShadow()
{
	view_.DrawShadow(active_ || deathPresentationActive_);
}

void Enemy::StartDeathPresentation()
{
	deathPresentationActive_ = true;
	view_.ApplyDeathPose(position_, rotationY_, 0.0f);
}

void Enemy::UpdateDeathPresentation(float elapsedTime, float duration)
{
	if (!deathPresentationActive_) {
		return;
	}

	const float rawProgress = duration > 0.0f ? elapsedTime / duration : 1.0f;
	const float progress = std::clamp(rawProgress, 0.0f, 1.0f);
	view_.ApplyDeathPose(
		position_,
		rotationY_,
		progress * progress * (3.0f - 2.0f * progress));
}

void Enemy::SetPosition(const Vector3& position)
{
	if (!active_) {
		return;
	}
	position_ = position;
	view_.Update(position_, rotationY_, false, false);
}

bool Enemy::ConsumeGroundImpact()
{
	const bool impacted = groundImpactPending_;
	groundImpactPending_ = false;
	return impacted;
}

void Enemy::SetRotationY(float rotationY)
{
	rotationY_ = rotationY;
	view_.Update(position_, rotationY_, false, false);
}

float Enemy::GetCollisionRadius() const
{
	return view_.GetCollisionRadius();
}

Engine::Math::AABB Enemy::GetCollisionAabb() const
{
	return view_.GetCollisionAabb(position_);
}

Engine::Math::OBB Enemy::GetCollisionObb() const
{
	return view_.GetCollisionObb(position_);
}

void Enemy::SetModelByType(int32_t type)
{
	view_.SetModelByType(type);
}

void Enemy::SetBehaviorByType(int32_t type)
{
	type_ = type;
	behavior_ = CreateEnemyBehaviorByType(type);
	view_.SetFloatingEnabled(type == 4);
}

void Enemy::SetBoss(bool boss)
{
	reactionController_.SetBoss(boss);
	view_.SetFloatingEnabled(false);
}

float Enemy::GetHpRatio() const
{
	return maxHp_ > 0 ? std::clamp(static_cast<float>(hp_) / static_cast<float>(maxHp_), 0.0f, 1.0f) : 0.0f;
}

void Enemy::TakeDamage(int32_t damage, const Vector3& knockDirection, float strength)
{
	hp_ -= damage;
	if (hp_ <= 0) {
		static SoundHandle sharedDeathSeHandle{};
		if (!sharedDeathSeHandle) {
			sharedDeathSeHandle = GameAudioCache::LoadWave(kDeathSePath);
		}
		if (sharedDeathSeHandle) {
			GameAudioCache::Play(sharedDeathSeHandle);
			GameAudioCache::SetVolumeFromTuning(sharedDeathSeHandle, kAudioEnemyDeath, 1.0f);
		}
		active_ = false;
		justDied_ = true;
		return;
	}

	reactionController_.ApplyHit(
		GetHpRatio(),
		knockDirection,
		strength);
}

void Enemy::SetBehaviorVisual(const Vector4& color, float scaleMultiplier)
{
	view_.SetBehaviorVisual(color, scaleMultiplier);
}

void Enemy::ClearBehaviorVisual()
{
	view_.ClearBehaviorVisual();
}

} // namespace DirectXGame
