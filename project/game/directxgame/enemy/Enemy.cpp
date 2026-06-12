#include "game/directxgame/enemy/Enemy.h"
#include "game/directxgame/core/GameAudioCache.h"
#include "game/directxgame/core/GameModelCache.h"
#include "Object3DCommon.h"
#include "TextureManager.h"
#include <algorithm>
#include <cmath>

namespace {

constexpr char kEnvironmentTexturePath[] = "Resources/textures/skybox/test.dds";
constexpr char kDeathSePath[] = "audio/se/se_death.wav";
constexpr char kAudioEnemyDeath[] = "combat.enemyDeath";
constexpr float kEnemyFallbackCollisionRadius = 1.35f;
constexpr float kFloatingShadowGroundY = -1.84f;

float ScalePerFrameDecay(float decayPerFrame, float deltaTime)
{
	return std::pow(decayPerFrame, deltaTime / 0.016f);
}

}

namespace DirectXGame {

void Enemy::Initialize()
{
	active_ = true;
	justDied_ = false;
	deathPresentationActive_ = false;
	hitFlashTimer_ = 0.0f;
	knockbackTimer_ = 0.0f;
	knockbackCooldownTimer_ = 0.0f;
	knockbackVelocity_ = { 0.0f, 0.0f, 0.0f };
	floatingVisualEnabled_ = false;
	groundImpactPending_ = false;
	floatingShadowObject_.reset();
	boss_ = false;
	bossPhase_ = 1;
	bossPhaseChanged_ = false;
	bossPhaseTransitionTimer_ = 0.0f;

	Engine::Base::TextureManager::GetInstance()->LoadTexture(kEnvironmentTexturePath);
	object_ = std::make_unique<Engine::Graphics3D::Object3D>();
	object_->Initialize(Engine::Graphics3D::Object3DCommon::GetInstance());
	object_->SetSkyboxFilePath(kEnvironmentTexturePath);
	object_->SetEnvironmentReflectionStrength(0.08f);
	object_->SetEnvironmentRoughness(0.65f);
	ApplyTransform();
	object_->Update();
}

void Enemy::Update(float deltaTime)
{
	if (!active_) {
		return;
	}

	previousPosition_ = position_;
	ClearBehaviorVisual();

	if (hitFlashTimer_ > 0.0f) {
		hitFlashTimer_ -= deltaTime;
	}
	knockbackCooldownTimer_ = (std::max)(0.0f, knockbackCooldownTimer_ - deltaTime);
	bossPhaseTransitionTimer_ = (std::max)(0.0f, bossPhaseTransitionTimer_ - deltaTime);

	if (knockbackTimer_ > 0.0f) {
		const float velocityScale = deltaTime / 0.016f;
		position_.x += knockbackVelocity_.x * velocityScale;
		position_.z += knockbackVelocity_.z * velocityScale;
		const float knockbackDecay = ScalePerFrameDecay(0.88f, deltaTime);
		knockbackVelocity_.x *= knockbackDecay;
		knockbackVelocity_.z *= knockbackDecay;
		knockbackTimer_ -= deltaTime;
		if (knockbackTimer_ <= 0.0f) {
			knockbackVelocity_ = { 0.0f, 0.0f, 0.0f };
		}
	} else if (behavior_ && bossPhaseTransitionTimer_ <= 0.0f) {
		behavior_->Update(*this, deltaTime);
	}

	if (object_) {
		const bool phaseFlash = bossPhaseTransitionTimer_ > 0.0f &&
			static_cast<int32_t>(bossPhaseTransitionTimer_ * 18.0f) % 2 == 0;
		object_->SetColor((hitFlashTimer_ > 0.0f || phaseFlash) ? Vector4{ 8.0f, 8.0f, 8.0f, 1.0f } : behaviorColor_);
		ApplyTransform();
		object_->Update();
	}
	UpdateFloatingShadow();
}

void Enemy::Draw()
{
	if ((active_ || deathPresentationActive_) && object_) {
		if (floatingVisualEnabled_ && floatingShadowObject_) {
			floatingShadowObject_->Draw();
		}
		object_->Draw();
	}
}

void Enemy::DrawShadow()
{
	if (object_ && (active_ || deathPresentationActive_)) {
		object_->DrawShadow();
	}
}

void Enemy::StartDeathPresentation()
{
	deathPresentationActive_ = true;
	ApplyDeathPose(0.0f);
}

void Enemy::UpdateDeathPresentation(float elapsedTime, float duration)
{
	if (!deathPresentationActive_) {
		return;
	}

	const float rawProgress = duration > 0.0f ? elapsedTime / duration : 1.0f;
	const float progress = std::clamp(rawProgress, 0.0f, 1.0f);
	ApplyDeathPose(progress * progress * (3.0f - 2.0f * progress));
}

void Enemy::SetPosition(const Vector3& position)
{
	if (!active_) {
		return;
	}
	position_ = position;
	ApplyTransform();
	if (object_) {
		object_->Update();
	}
	UpdateFloatingShadow();
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
	ApplyTransform();
	if (object_) {
		object_->Update();
	}
}

float Enemy::GetCollisionRadius() const
{
	return object_ ? object_->GetScaledModelBoundingRadius(kEnemyFallbackCollisionRadius) : kEnemyFallbackCollisionRadius;
}

Engine::Math::AABB Enemy::GetCollisionAabb() const
{
	return object_ ? object_->GetScaledModelAabb(kEnemyFallbackCollisionRadius) : Engine::Math::AABB{
		{ position_.x - kEnemyFallbackCollisionRadius, position_.y - kEnemyFallbackCollisionRadius, position_.z - kEnemyFallbackCollisionRadius },
		{ position_.x + kEnemyFallbackCollisionRadius, position_.y + kEnemyFallbackCollisionRadius, position_.z + kEnemyFallbackCollisionRadius },
	};
}

Engine::Math::OBB Enemy::GetCollisionObb() const
{
	return object_ ? object_->GetScaledModelObb(kEnemyFallbackCollisionRadius) : Engine::Math::OBB{
		position_,
		{ Vector3{ 1.0f, 0.0f, 0.0f }, Vector3{ 0.0f, 1.0f, 0.0f }, Vector3{ 0.0f, 0.0f, 1.0f } },
		Vector3{ kEnemyFallbackCollisionRadius, kEnemyFallbackCollisionRadius, kEnemyFallbackCollisionRadius },
	};
}

void Enemy::SetModelByType(int32_t type)
{
	const char* modelName = "octopus.obj";
	switch (type) {
	case 0: modelName = "Enemy1.obj"; break;
	case 1: modelName = "Enemy2.obj"; break;
	case 2: modelName = "Enemy3.obj"; break;
	case 3: modelName = "Enemy4.obj"; break;
	case 4: modelName = "Enemy4.obj"; break;
	case 5: modelName = "octopus.obj"; break;
	default: break;
	}

	if (object_) {
		const ModelHandle modelHandle = GameModelCache::Load(modelName);
		GameModelCache::ApplyToObject(*object_, modelHandle);
	}
}

void Enemy::SetBehaviorByType(int32_t type)
{
	type_ = type;
	behavior_ = CreateEnemyBehaviorByType(type);
	floatingVisualEnabled_ = type == 4;
	if (floatingVisualEnabled_) {
		InitializeFloatingShadow();
	} else {
		floatingShadowObject_.reset();
	}
}

void Enemy::SetBoss(bool boss)
{
	boss_ = boss;
	floatingVisualEnabled_ = false;
	if (floatingShadowObject_) {
		floatingShadowObject_.reset();
	}
}

float Enemy::GetHpRatio() const
{
	return maxHp_ > 0 ? std::clamp(static_cast<float>(hp_) / static_cast<float>(maxHp_), 0.0f, 1.0f) : 0.0f;
}

bool Enemy::ConsumeBossPhaseChanged()
{
	const bool changed = bossPhaseChanged_;
	bossPhaseChanged_ = false;
	return changed;
}

void Enemy::TakeDamage(int32_t damage, const Vector3& knockDirection, float strength)
{
	hp_ -= damage;
	if (hp_ <= 0) {
		static SoundHandle sharedDeathSeHandle = 0;
		if (sharedDeathSeHandle == 0) {
			sharedDeathSeHandle = GameAudioCache::LoadWave(kDeathSePath);
		}
		if (sharedDeathSeHandle != 0) {
			GameAudioCache::Play(sharedDeathSeHandle);
			GameAudioCache::SetVolumeFromTuning(sharedDeathSeHandle, kAudioEnemyDeath, 1.0f);
		}
		active_ = false;
		justDied_ = true;
		return;
	}

	hitFlashTimer_ = kHitFlashDuration;
	if (boss_) {
		const float hpRatio = GetHpRatio();
		const int32_t nextPhase = hpRatio <= 0.33f ? 3 : (hpRatio <= 0.66f ? 2 : 1);
		if (nextPhase != bossPhase_) {
			bossPhase_ = nextPhase;
			bossPhaseChanged_ = true;
			bossPhaseTransitionTimer_ = 0.7f;
			knockbackVelocity_ = { 0.0f, 0.0f, 0.0f };
			knockbackTimer_ = 0.0f;
		}
	}
	const float length = std::sqrt(knockDirection.x * knockDirection.x + knockDirection.z * knockDirection.z);
	if (length > 0.001f && strength > 0.0f && knockbackCooldownTimer_ <= 0.0f && bossPhaseTransitionTimer_ <= 0.0f) {
		const float resistance = boss_ ? 0.18f : 1.0f;
		knockbackVelocity_.x = (knockDirection.x / length) * strength * resistance;
		knockbackVelocity_.z = (knockDirection.z / length) * strength * resistance;
		knockbackTimer_ = kKnockbackDuration;
		knockbackCooldownTimer_ = boss_ ? 0.8f : kKnockbackCooldown;
	}
}

void Enemy::SetBehaviorVisual(const Vector4& color, float scaleMultiplier)
{
	behaviorColor_ = color;
	behaviorScaleMultiplier_ = scaleMultiplier;
}

void Enemy::ClearBehaviorVisual()
{
	behaviorColor_ = { 1.0f, 1.0f, 1.0f, 1.0f };
	behaviorScaleMultiplier_ = 1.0f;
}

void Enemy::InitializeFloatingShadow()
{
	if (floatingShadowObject_) {
		return;
	}

	floatingShadowObject_ = std::make_unique<Engine::Graphics3D::Object3D>();
	floatingShadowObject_->Initialize(Engine::Graphics3D::Object3DCommon::GetInstance());
	const ModelHandle planeHandle = GameModelCache::Load("plane.obj");
	GameModelCache::ApplyToObject(*floatingShadowObject_, planeHandle);
	floatingShadowObject_->SetSkyboxFilePath(kEnvironmentTexturePath);
	floatingShadowObject_->SetEnvironmentReflectionStrength(0.0f);
	floatingShadowObject_->SetEnvironmentRoughness(1.0f);
	floatingShadowObject_->SetLighting(false);
	UpdateFloatingShadow();
}

void Enemy::ApplyTransform()
{
	if (!object_) {
		return;
	}
	object_->SetScale({ behaviorScaleMultiplier_, behaviorScaleMultiplier_, behaviorScaleMultiplier_ });
	object_->SetRotate({ 0.0f, rotationY_, 0.0f });
	object_->SetTranslate(position_);
}

void Enemy::UpdateFloatingShadow()
{
	if (!floatingVisualEnabled_ || !floatingShadowObject_) {
		return;
	}

	const float altitude = (std::max)(0.0f, position_.y - kFloatingShadowGroundY);
	const float shadowScale = std::clamp(2.15f + altitude * 0.18f, 2.15f, 3.6f);
	const float shadowAlpha = std::clamp(0.46f - altitude * 0.045f, 0.18f, 0.42f);
	floatingShadowObject_->SetScale({
		shadowScale * 1.35f,
		1.0f,
		shadowScale * 0.78f,
		});
	floatingShadowObject_->SetRotate({ 0.0f, rotationY_, 0.0f });
	floatingShadowObject_->SetTranslate({
		position_.x,
		kFloatingShadowGroundY,
		position_.z,
		});
	floatingShadowObject_->SetColor({ 0.02f, 0.025f, 0.035f, shadowAlpha });
	floatingShadowObject_->Update();
}

void Enemy::ApplyDeathPose(float progress)
{
	if (!object_) {
		return;
	}

	const float pulse = std::sin(progress * 3.14159265f);
	const float scale = behaviorScaleMultiplier_ * (1.0f + pulse * 0.18f);
	object_->SetScale({ scale, scale, scale });
	object_->SetRotate({
		progress * 1.32f,
		rotationY_ + progress * 0.42f,
		progress * -0.36f,
		});
	object_->SetTranslate({
		position_.x,
		position_.y - progress * 1.15f,
		position_.z,
		});
	object_->SetColor(Vector4{ 1.0f + pulse * 2.0f, 0.32f + pulse * 0.35f, 0.22f, 1.0f });
	object_->Update();
}

} // namespace DirectXGame
