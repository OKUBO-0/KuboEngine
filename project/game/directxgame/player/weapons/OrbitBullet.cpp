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
	angle_ += angularSpeed_ * (deltaTime / 0.016f);
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
	object_->SetScale({ scale_, scale_, scale_ });
	object_->SetTranslate(position_);
}

} // namespace DirectXGame
