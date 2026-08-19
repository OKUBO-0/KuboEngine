#include "NormalBullet.h"
#include "GameAudioCache.h"
#include "GameModelCache.h"
#include "Object3DCommon.h"
#include <algorithm>
#include <cmath>

namespace {

constexpr char kEnvironmentTexturePath[] = "Resources/textures/skybox/test.dds";
constexpr char kShotSePath[] = "se/shot.wav";
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
	int32_t maxHits,
	float scale,
	MovementMode movementMode,
	const VisualStyle& visualStyle)
{
	position_ = startPosition;
	previousPosition_ = startPosition;
	direction_ = NormalizeOrForward(forward);
	rotationY_ = std::atan2(direction_.x, direction_.z);
	speed_ = speed;
	range_ = range;
	traveled_ = 0.0f;
	remainingHits_ = (std::max)(1, maxHits);
	scale_ = (std::max)(0.05f, scale);
	spinAngle_ = 0.0f;
	active_ = true;
	movementMode_ = movementMode;
	returning_ = false;
	visualStyle_ = visualStyle;
	hitCooldowns_.clear();

	const char* requestedModel = visualStyle_.modelPath
		? visualStyle_.modelPath
		: "bullet.obj";
	if (!object_ || modelPath_ != requestedModel) {
		const ModelHandle bulletHandle = GameModelCache::Load(requestedModel);
		object_ = std::make_unique<Engine::Graphics3D::Object3D>();
		object_->Initialize(Engine::Graphics3D::Object3DCommon::GetInstance());
		GameModelCache::ApplyToObject(*object_, bulletHandle);
		object_->SetSkyboxFilePath(kEnvironmentTexturePath);
		object_->SetEnvironmentReflectionStrength(0.0f);
		object_->SetEnvironmentRoughness(1.0f);
		modelPath_ = requestedModel;
	}
	object_->SetColor(visualStyle_.color);
	object_->SetTextureInfluence(0.35f);
	object_->SetScale({
		scale_ * visualStyle_.scaleMultiplier.x,
		scale_ * visualStyle_.scaleMultiplier.y,
		scale_ * visualStyle_.scaleMultiplier.z,
		});
	ApplyTransform();
	object_->Update();

	static SoundHandle sharedShotSeHandle{};
	if (!sharedShotSeHandle) {
		sharedShotSeHandle = GameAudioCache::LoadWave(kShotSePath);
	}
	GameAudioCache::PlayTuned(sharedShotSeHandle, kAudioShot, 0.34f, 0.035f);
}

void NormalBullet::Update(const Vector3& playerPosition, float deltaTime)
{
	if (!active_) {
		return;
	}

	previousPosition_ = position_;
	if (movementMode_ == MovementMode::ReturnToPlayer) {
		if (!returning_ && traveled_ >= range_ * 0.5f) {
			returning_ = true;
		}
		if (returning_) {
			RedirectToward(playerPosition);
			const float dx = playerPosition.x - position_.x;
			const float dz = playerPosition.z - position_.z;
			if (dx * dx + dz * dz <= 1.0f) {
				active_ = false;
				return;
			}
		}
	}
	const float distance = speed_ * (deltaTime / 0.016f);
	position_.x += direction_.x * distance;
	position_.y += direction_.y * distance;
	position_.z += direction_.z * distance;
	if (movementMode_ == MovementMode::ReturnToPlayer ||
		visualStyle_.spinAroundY) {
		spinAngle_ += 0.24f * (deltaTime / 0.016f);
	}
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
		Engine::Graphics3D::Object3D::SubmitForDraw(object_.get());
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

void NormalBullet::RedirectToward(const Vector3& targetPosition)
{
	direction_ = NormalizeOrForward(targetPosition - position_);
	rotationY_ = std::atan2(direction_.x, direction_.z);
}

void NormalBullet::ApplyTransform()
{
	if (!object_) {
		return;
	}
	if (movementMode_ == MovementMode::ReturnToPlayer) {
		if (visualStyle_.spinAroundY) {
			object_->SetRotate({
				visualStyle_.rotationOffset.x,
				rotationY_ + spinAngle_ + visualStyle_.rotationOffset.y,
				visualStyle_.rotationOffset.z,
				});
		} else {
			object_->SetRotate({
				visualStyle_.rotationOffset.x,
				rotationY_ + spinAngle_ * 0.22f + visualStyle_.rotationOffset.y,
				spinAngle_ + visualStyle_.rotationOffset.z,
				});
		}
	} else if (visualStyle_.spinAroundY) {
		object_->SetRotate({
			visualStyle_.rotationOffset.x,
			rotationY_ + spinAngle_ + visualStyle_.rotationOffset.y,
			visualStyle_.rotationOffset.z,
			});
	} else {
		object_->SetRotate({
			visualStyle_.rotationOffset.x,
			rotationY_ + visualStyle_.rotationOffset.y,
			visualStyle_.rotationOffset.z,
			});
	}
	object_->SetTranslate({
		position_.x,
		position_.y + visualStyle_.yOffset,
		position_.z,
		});
}

} // namespace DirectXGame
