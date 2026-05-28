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
	hitFlashTimer_ = 0.0f;
	knockbackTimer_ = 0.0f;
	knockbackVelocity_ = { 0.0f, 0.0f, 0.0f };

	Engine::Base::TextureManager::GetInstance()->LoadTexture(kEnvironmentTexturePath);
	object_ = std::make_unique<Engine::Graphics3D::Object3D>();
	object_->Initialize(Engine::Graphics3D::Object3DCommon::GetInstance());
	object_->SetSkyboxFilePath(kEnvironmentTexturePath);
	object_->SetEnvironmentReflectionStrength(0.08f);
	object_->SetEnvironmentRoughness(0.65f);
	lightSettings_.ApplyTo(*object_);
	ApplyTransform();
	object_->Update();
}

void Enemy::Update(float deltaTime)
{
	if (!active_) {
		return;
	}

	ClearBehaviorVisual();

	if (hitFlashTimer_ > 0.0f) {
		hitFlashTimer_ -= deltaTime;
	}

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
	} else if (behavior_) {
		behavior_->Update(*this, deltaTime);
	}

	if (object_) {
		object_->SetColor(hitFlashTimer_ > 0.0f ? Vector4{ 8.0f, 8.0f, 8.0f, 1.0f } : behaviorColor_);
		ApplyTransform();
		object_->Update();
	}
}

void Enemy::Draw()
{
	if (active_ && object_) {
		object_->Draw();
	}
}

void Enemy::SetPosition(const Vector3& position)
{
	position_ = position;
	ApplyTransform();
	if (object_) {
		object_->Update();
	}
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
	default: break;
	}

	if (object_) {
		const ModelHandle modelHandle = GameModelCache::Load(modelName);
		GameModelCache::ApplyToObject(*object_, modelHandle);
	}
}

void Enemy::SetBehaviorByType(int32_t type)
{
	behavior_ = CreateEnemyBehaviorByType(type);
}

void Enemy::SetLightSettings(const GameLightSettings& lightSettings)
{
	lightSettings_ = lightSettings;
	if (object_) {
		lightSettings_.ApplyTo(*object_);
	}
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
	const float length = std::sqrt(knockDirection.x * knockDirection.x + knockDirection.z * knockDirection.z);
	if (length > 0.001f && strength > 0.0f) {
		knockbackVelocity_.x = (knockDirection.x / length) * strength;
		knockbackVelocity_.z = (knockDirection.z / length) * strength;
		knockbackTimer_ = kKnockbackDuration;
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

void Enemy::ApplyTransform()
{
	if (!object_) {
		return;
	}
	object_->SetScale({ behaviorScaleMultiplier_, behaviorScaleMultiplier_, behaviorScaleMultiplier_ });
	object_->SetRotate({ 0.0f, rotationY_, 0.0f });
	object_->SetTranslate(position_);
}

} // namespace DirectXGame
