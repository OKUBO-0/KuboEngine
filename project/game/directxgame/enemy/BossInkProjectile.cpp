#include "BossInkProjectile.h"

#include "GameModelCache.h"
#include "Object3D.h"
#include "Object3DCommon.h"
#include <algorithm>
#include <cmath>

namespace {

constexpr char kEnvironmentTexturePath[] =
	"Resources/textures/skybox/test.dds";
constexpr float kSpeed = 14.5f;
constexpr float kLifetime = 6.0f;

}

namespace DirectXGame {

BossInkProjectile::BossInkProjectile() = default;
BossInkProjectile::~BossInkProjectile() = default;

void BossInkProjectile::Initialize(
	const Vector3& position,
	const Vector3& direction)
{
	position_ = position;
	position_.y = 1.0f;
	direction_ = direction;
	const float length = std::sqrt(
		direction_.x * direction_.x + direction_.z * direction_.z);
	if (length > 0.001f) {
		direction_.x /= length;
		direction_.z /= length;
	}
	direction_.y = 0.0f;
	lifetime_ = kLifetime;
	active_ = true;

	object_ = std::make_unique<Engine::Graphics3D::Object3D>();
	object_->Initialize(Engine::Graphics3D::Object3DCommon::GetInstance());
	GameModelCache::ApplyToObject(*object_, GameModelCache::Load("bullet.obj"));
	object_->SetSkyboxFilePath(kEnvironmentTexturePath);
	object_->SetLighting(false);
	object_->SetEnvironmentReflectionStrength(0.0f);
	object_->SetEnvironmentRoughness(1.0f);
	object_->SetScale({ 1.15f, 1.15f, 1.15f });
	object_->SetColor({ 1.0f, 0.82f, 0.18f, 0.98f });
	object_->SetTranslate(position_);
	object_->Update();
}

void BossInkProjectile::Update(float deltaTime)
{
	if (!active_) {
		return;
	}
	const float elapsed = (std::max)(0.0f, deltaTime);
	position_.x += direction_.x * kSpeed * elapsed;
	position_.z += direction_.z * kSpeed * elapsed;
	lifetime_ -= elapsed;
	active_ = lifetime_ > 0.0f;
	if (object_) {
		object_->SetTranslate(position_);
		object_->Update();
	}
}

void BossInkProjectile::Draw() const
{
	if (active_ && object_) {
		Engine::Graphics3D::Object3D::SubmitForDraw(object_.get());
	}
}

} // namespace DirectXGame
