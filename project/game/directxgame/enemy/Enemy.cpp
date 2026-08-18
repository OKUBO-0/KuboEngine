#include "Enemy.h"
#include "GameAudioCache.h"
#include <algorithm>

namespace {

constexpr char kDeathSePath[] = "se/enemy_death.wav";
constexpr char kAudioEnemyDeath[] = "combat.enemyDeath";

}

namespace DirectXGame {

void Enemy::Initialize()
{
	active_ = true;
	justDied_ = false;
	deathPresentationActive_ = false;
	deathPresentationElapsed_ = 0.0f;
	spawnPresentationTimer_ = 0.0f;
	spawnPresentationDuration_ = 0.0f;
	groundImpactPending_ = false;
	pendingBossAttack_ = {};
	bossAttackTelegraph_ = {};
	reactionController_.Reset();

	view_.Initialize();
	view_.SetFloatingEnabled(true);
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
	bossAttackTelegraph_ = {};
	if (spawnPresentationTimer_ > 0.0f) {
		spawnPresentationTimer_ = (std::max)(
			0.0f,
			spawnPresentationTimer_ - deltaTime);
		const float progress = spawnPresentationDuration_ > 0.0f
			? 1.0f - spawnPresentationTimer_ / spawnPresentationDuration_
			: 1.0f;
		const float eased =
			std::clamp(progress, 0.0f, 1.0f) *
			std::clamp(progress, 0.0f, 1.0f) *
			(3.0f - 2.0f * std::clamp(progress, 0.0f, 1.0f));
		view_.SetSpawnScaleMultiplier(0.03f + eased * 0.97f);
	} else {
		view_.SetSpawnScaleMultiplier(1.0f);
	}

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

void Enemy::SetAnimationUpdateStride(uint32_t stride)
{
	view_.SetAnimationUpdateStride(stride);
}

void Enemy::SetSimplifiedRenderEnabled(bool enabled)
{
	view_.SetSimplifiedRenderEnabled(enabled);
}

void Enemy::Draw()
{
	view_.Draw(active_ || deathPresentationActive_);
}

void Enemy::DrawFloatingShadow()
{
	view_.DrawFloatingShadow(active_ || deathPresentationActive_);
}

void Enemy::DrawModel()
{
	view_.DrawModel(active_ || deathPresentationActive_);
}

void Enemy::DrawShadow()
{
	view_.DrawShadow(active_ || deathPresentationActive_);
}

Engine::Graphics3D::Object3D* Enemy::GetRenderObject() const
{
	return active_ || deathPresentationActive_
		? view_.GetObject()
		: nullptr;
}

void Enemy::StartDeathPresentation()
{
	deathPresentationActive_ = true;
	deathPresentationElapsed_ = 0.0f;
	view_.SetSimplifiedRenderEnabled(false);
	view_.SetFrustumCullingEnabled(false);
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

bool Enemy::UpdateDeathPresentationFrame(float deltaTime, float duration)
{
	if (!deathPresentationActive_) {
		return true;
	}
	deathPresentationElapsed_ += (std::max)(0.0f, deltaTime);
	UpdateDeathPresentation(deathPresentationElapsed_, duration);
	return deathPresentationElapsed_ >= duration;
}

void Enemy::FinishDeathPresentation()
{
	deathPresentationActive_ = false;
	view_.SetFrustumCullingEnabled(true);
}

void Enemy::NotifyAttack()
{
	view_.NotifyAttack();
}

void Enemy::NotifyJump()
{
	view_.NotifyJump();
}

void Enemy::NotifyLand()
{
	view_.NotifyLand();
}

void Enemy::SetPosition(const Vector3& position)
{
	if (!active_) {
		return;
	}
	position_ = position;
	view_.Update(position_, rotationY_, false, false);
}

void Enemy::ApplyPresentationPose(
	const Vector3& position,
	bool idleMotion)
{
	view_.ClearBehaviorVisual();
	view_.SetSimplifiedRenderEnabled(false);
	view_.SetFrustumCullingEnabled(false);
	view_.ApplyPresentationPose(position, rotationY_, idleMotion);
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

void Enemy::SetBehavior(EnemyBehaviorType type)
{
	behavior_ = CreateEnemyBehavior(type);
	view_.SetFloatingEnabled(true);
}

void Enemy::SetBoss(bool boss)
{
	reactionController_.SetBoss(boss);
	view_.SetFloatingEnabled(true);
}

void Enemy::StartSpawnPresentation(float duration)
{
	spawnPresentationDuration_ = (std::max)(0.01f, duration);
	spawnPresentationTimer_ = spawnPresentationDuration_;
	view_.SetSpawnScaleMultiplier(0.03f);
	view_.Update(position_, rotationY_, false, false);
}

void Enemy::FinishSpawnPresentation()
{
	spawnPresentationDuration_ = 0.0f;
	spawnPresentationTimer_ = 0.0f;
	view_.SetSpawnScaleMultiplier(1.0f);
	view_.SetFrustumCullingEnabled(true);
}

void Enemy::SetPresentationCullingEnabled(bool enabled)
{
	view_.SetFrustumCullingEnabled(enabled);
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
		GameAudioCache::PlayTuned(sharedDeathSeHandle, kAudioEnemyDeath, 0.54f, 0.035f);
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

void Enemy::QueueBossAttack(
	BossAttackType type,
	const Vector3& direction,
	const Vector3& targetPosition)
{
	pendingBossAttack_ = { type, position_, direction, targetPosition };
}

bool Enemy::ConsumeBossAttack(BossAttackEvent& outEvent)
{
	if (pendingBossAttack_.type == BossAttackType::None) {
		return false;
	}
	outEvent = pendingBossAttack_;
	pendingBossAttack_ = {};
	return true;
}

void Enemy::SetBossAttackTelegraph(
	BossAttackType type,
	const Vector3& direction,
	float progress,
	float range,
	const Vector3& targetPosition)
{
	bossAttackTelegraph_ = {
		type,
		position_,
		direction,
		targetPosition,
		std::clamp(progress, 0.0f, 1.0f),
		(std::max)(0.0f, range),
	};
}

void Enemy::ClearBossAttackTelegraph()
{
	bossAttackTelegraph_ = {};
}

} // namespace DirectXGame
