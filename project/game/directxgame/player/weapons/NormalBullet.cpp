#include "game/directxgame/player/weapons/NormalBullet.h"
#include "game/directxgame/core/GameAudioCache.h"
#include "game/directxgame/core/GameModelCache.h"
#include "Object3DCommon.h"
#include "TextureManager.h"
#include <algorithm>
#include <cmath>

namespace {

constexpr char kEnvironmentTexturePath[] = "Resources/textures/skybox/test.dds";
constexpr char kShotSePath[] = "audio/se/se_shot.wav";
constexpr char kAudioShot[] = "combat.shot";
constexpr float kBulletFallbackCollisionRadius = 0.75f;

Vector3 NormalizeOrForward(const Vector3& vector)
{
	const float length = std::sqrt(vector.x * vector.x + vector.y * vector.y + vector.z * vector.z);
	if (length <= 0.0001f) {
		return { 0.0f, 0.0f, 1.0f };
	}
	return { vector.x / length, vector.y / length, vector.z / length };
}

}

namespace DirectXGame {

void NormalBullet::InitializeForward(
	const Vector3& startPosition,
	const Vector3& forward,
	float speed,
	float range,
	int32_t maxHits)
{
	position_ = startPosition;
	direction_ = NormalizeOrForward(forward);
	rotationY_ = std::atan2(direction_.x, direction_.z);
	speed_ = speed;
	range_ = range;
	traveled_ = 0.0f;
	remainingHits_ = (std::max)(1, maxHits);
	active_ = true;

	Engine::Base::TextureManager::GetInstance()->LoadTexture(kEnvironmentTexturePath);
	const ModelHandle bulletHandle = GameModelCache::Load("bullet.obj");
	object_ = std::make_unique<Engine::Graphics3D::Object3D>();
	object_->Initialize(Engine::Graphics3D::Object3DCommon::GetInstance());
	GameModelCache::ApplyToObject(*object_, bulletHandle);
	object_->SetSkyboxFilePath(kEnvironmentTexturePath);
	object_->SetEnvironmentReflectionStrength(0.0f);
	object_->SetEnvironmentRoughness(1.0f);
	object_->SetScale({ 1.0f, 1.0f, 1.0f });
	lightSettings_.ApplyTo(*object_);
	ApplyTransform();
	object_->Update();

	static SoundHandle sharedShotSeHandle = 0;
	if (sharedShotSeHandle == 0) {
		sharedShotSeHandle = GameAudioCache::LoadWave(kShotSePath);
	}
	if (sharedShotSeHandle != 0) {
	GameAudioCache::Play(sharedShotSeHandle);
	GameAudioCache::SetVolumeFromTuning(sharedShotSeHandle, kAudioShot, 1.0f);
}
}

void NormalBullet::Update(const Vector3&, float deltaTime)
{
	if (!active_) {
		return;
	}

	const float distance = speed_ * (deltaTime / 0.016f);
	position_.x += direction_.x * distance;
	position_.y += direction_.y * distance;
	position_.z += direction_.z * distance;
	traveled_ += distance;

	if (traveled_ >= range_) {
		active_ = false;
		return;
	}

	for (auto it = hitCooldowns_.begin(); it != hitCooldowns_.end();) {
		it->second -= deltaTime;
		if (it->second <= 0.0f) {
			it = hitCooldowns_.erase(it);
		} else {
			++it;
		}
	}

	ApplyTransform();
	if (object_) {
		object_->Update();
	}
}

void NormalBullet::Draw()
{
	if (active_ && object_) {
		object_->Draw();
	}
}

float NormalBullet::GetCollisionRadius() const
{
	return object_ ? object_->GetScaledModelBoundingRadius(kBulletFallbackCollisionRadius) : kBulletFallbackCollisionRadius;
}

Engine::Math::AABB NormalBullet::GetCollisionAabb() const
{
	return object_ ? object_->GetScaledModelAabb(kBulletFallbackCollisionRadius) : Engine::Math::AABB{
		{ position_.x - kBulletFallbackCollisionRadius, position_.y - kBulletFallbackCollisionRadius, position_.z - kBulletFallbackCollisionRadius },
		{ position_.x + kBulletFallbackCollisionRadius, position_.y + kBulletFallbackCollisionRadius, position_.z + kBulletFallbackCollisionRadius },
	};
}

Engine::Math::OBB NormalBullet::GetCollisionObb() const
{
	return object_ ? object_->GetScaledModelObb(kBulletFallbackCollisionRadius) : Engine::Math::OBB{
		position_,
		{ Vector3{ 1.0f, 0.0f, 0.0f }, Vector3{ 0.0f, 1.0f, 0.0f }, Vector3{ 0.0f, 0.0f, 1.0f } },
		Vector3{ kBulletFallbackCollisionRadius, kBulletFallbackCollisionRadius, kBulletFallbackCollisionRadius },
	};
}

bool NormalBullet::CanHitEnemy(void* enemyPtr)
{
	const auto it = hitCooldowns_.find(enemyPtr);
	return it == hitCooldowns_.end() || it->second <= 0.0f;
}

void NormalBullet::RegisterHit(void* enemyPtr)
{
	hitCooldowns_[enemyPtr] = kHitInterval;
}

bool NormalBullet::ConsumeHit()
{
	--remainingHits_;
	if (remainingHits_ <= 0) {
		active_ = false;
		return false;
	}
	return true;
}

void NormalBullet::SetLightSettings(const GameLightSettings& lightSettings)
{
	lightSettings_ = lightSettings;
	if (object_) {
		lightSettings_.ApplyTo(*object_);
	}
}

void NormalBullet::ApplyTransform()
{
	if (!object_) {
		return;
	}
	object_->SetRotate({ 0.0f, rotationY_, 0.0f });
	object_->SetTranslate(position_);
}

} // namespace DirectXGame
