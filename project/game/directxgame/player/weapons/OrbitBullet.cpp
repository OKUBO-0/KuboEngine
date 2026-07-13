#include "OrbitBullet.h"
#include "GameModelCache.h"
#include "Object3DCommon.h"
#include <algorithm>
#include <cmath>

namespace {

constexpr char kEnvironmentTexturePath[] = "Resources/textures/skybox/test.dds";
constexpr float kOrbitBulletFallbackCollisionRadius = 0.85f;

}

namespace DirectXGame {

void OrbitBullet::Initialize(const Vector3& center, float radius, float angle, float angularSpeed, float scale, float hitInterval)
{
	orbitRadius_ = radius;
	angle_ = angle;
	baseAngularSpeed_ = angularSpeed;
	baseScale_ = scale;
	baseHitInterval_ = hitInterval;
	angularSpeed_ = baseAngularSpeed_;
	scale_ = baseScale_;
	hitInterval_ = baseHitInterval_;
	spinAngle_ = angle * 1.7f;
	active_ = true;
	position_ = {
		center.x + std::cos(angle_) * orbitRadius_,
		center.y,
		center.z + std::sin(angle_) * orbitRadius_,
	};
	previousPosition_ = position_;

	const ModelHandle bulletHandle = GameModelCache::Load("cube.obj");
	object_ = std::make_unique<Engine::Graphics3D::Object3D>();
	object_->Initialize(Engine::Graphics3D::Object3DCommon::GetInstance());
	GameModelCache::ApplyToObject(*object_, bulletHandle);
	object_->SetSkyboxFilePath(kEnvironmentTexturePath);
	object_->SetEnvironmentReflectionStrength(0.0f);
	object_->SetEnvironmentRoughness(1.0f);
	object_->SetColor({ 0.46f, 0.38f, 0.30f, 1.0f });

	Update(center, 0.0f);
}

void OrbitBullet::ApplyRuntimeModifiers(
	float projectileSpeedMultiplier,
	float areaSizeMultiplier,
	float attackSpeedMultiplier)
{
	angularSpeed_ = baseAngularSpeed_ * (std::max)(0.1f, projectileSpeedMultiplier);
	scale_ = baseScale_ * (std::max)(0.1f, areaSizeMultiplier);
	hitInterval_ = baseHitInterval_ / (std::max)(0.1f, attackSpeedMultiplier);
}

void OrbitBullet::Update(const Vector3& center, float deltaTime)
{
	if (!active_) {
		return;
	}

	previousPosition_ = position_;
	const float fixedStepScale = deltaTime / 0.016f;
	angle_ += angularSpeed_ * fixedStepScale;
	spinAngle_ += spinSpeed_ * fixedStepScale;
	position_ = {
		center.x + std::cos(angle_) * orbitRadius_,
		center.y,
		center.z + std::sin(angle_) * orbitRadius_,
	};

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

void OrbitBullet::Draw()
{
	if (active_ && object_) {
		object_->Draw();
	}
}

float OrbitBullet::GetCollisionRadius() const
{
	return object_ ? object_->GetScaledModelBoundingRadius(kOrbitBulletFallbackCollisionRadius) : kOrbitBulletFallbackCollisionRadius;
}

Engine::Math::AABB OrbitBullet::GetCollisionAabb() const
{
	return object_ ? object_->GetScaledModelAabb(kOrbitBulletFallbackCollisionRadius) : Engine::Math::AABB{
		{ position_.x - kOrbitBulletFallbackCollisionRadius, position_.y - kOrbitBulletFallbackCollisionRadius, position_.z - kOrbitBulletFallbackCollisionRadius },
		{ position_.x + kOrbitBulletFallbackCollisionRadius, position_.y + kOrbitBulletFallbackCollisionRadius, position_.z + kOrbitBulletFallbackCollisionRadius },
	};
}

Engine::Math::OBB OrbitBullet::GetCollisionObb() const
{
	return object_ ? object_->GetScaledModelObb(kOrbitBulletFallbackCollisionRadius) : Engine::Math::OBB{
		position_,
		{ Vector3{ 1.0f, 0.0f, 0.0f }, Vector3{ 0.0f, 1.0f, 0.0f }, Vector3{ 0.0f, 0.0f, 1.0f } },
		Vector3{ kOrbitBulletFallbackCollisionRadius, kOrbitBulletFallbackCollisionRadius, kOrbitBulletFallbackCollisionRadius },
	};
}

bool OrbitBullet::CanHitEnemy(void* enemyPtr)
{
	const auto it = hitCooldowns_.find(enemyPtr);
	return it == hitCooldowns_.end() || it->second <= 0.0f;
}

void OrbitBullet::RegisterHit(void* enemyPtr)
{
	hitCooldowns_[enemyPtr] = hitInterval_;
}

void OrbitBullet::ApplyTransform()
{
	if (!object_) {
		return;
	}
	const float wobble = std::sin(spinAngle_ * 1.7f) * 0.045f;
	object_->SetRotate({
		spinAngle_ * 0.72f,
		-angle_ + spinAngle_,
		spinAngle_ * 1.18f,
		});
	object_->SetScale({
		scale_ * (1.10f + wobble),
		scale_ * (0.86f - wobble * 0.35f),
		scale_ * (0.98f + wobble * 0.55f),
		});
	object_->SetTranslate(position_);
}

} // namespace DirectXGame
