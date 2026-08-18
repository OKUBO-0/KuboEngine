#include "BossInkProjectile.h"

#include "GameModelCache.h"
#include "Object3D.h"
#include "Object3DCommon.h"
#include <algorithm>
#include <cmath>
#include <string_view>

namespace {

constexpr char kEnvironmentTexturePath[] =
	"Resources/textures/skybox/test.dds";
constexpr float kFadeOutDuration = 0.55f;
}

namespace DirectXGame {

BossInkProjectile::BossInkProjectile() = default;
BossInkProjectile::~BossInkProjectile() = default;

void BossInkProjectile::Initialize(
	const Vector3& position,
	const Vector3& direction,
	float speed,
	float lifetime,
	float collisionRadius,
	const Vector4& color,
	const char* modelPath)
{
	position_ = position;
	position_.y = 1.35f;
	speed_ = (std::max)(0.1f, speed);
	collisionRadius_ = (std::max)(0.1f, collisionRadius);
	color_ = color;
	direction_ = direction;
	const float length = std::sqrt(
		direction_.x * direction_.x + direction_.z * direction_.z);
	if (length > 0.001f) {
		direction_.x /= length;
		direction_.z /= length;
	}
	direction_.y = 0.0f;
	lifetime_ = (std::max)(0.1f, lifetime);
	elapsedTime_ = 0.0f;
	active_ = true;

	object_ = std::make_unique<Engine::Graphics3D::Object3D>();
	object_->Initialize(Engine::Graphics3D::Object3DCommon::GetInstance());
	cubeModel_ = modelPath && std::string_view(modelPath) == "cube.obj";
	GameModelCache::ApplyToObject(*object_, GameModelCache::Load(
		modelPath && *modelPath ? modelPath : "bullet.obj"));
	object_->SetSkyboxFilePath(kEnvironmentTexturePath);
	object_->SetLighting(false);
	object_->SetEnvironmentReflectionStrength(0.0f);
	object_->SetEnvironmentRoughness(1.0f);
	const float scale = cubeModel_
		? std::clamp(collisionRadius_ * 1.18f, 0.72f, 2.4f)
		: 1.15f;
	object_->SetScale({ scale, scale, scale });
	const float angle = std::atan2(direction_.x, direction_.z);
	object_->SetRotate({ 0.0f, angle, 0.0f });
	object_->SetColor(color_);
	object_->SetTranslate(position_);
	object_->Update();
}

void BossInkProjectile::Update(float deltaTime)
{
	if (!active_) {
		return;
	}
	const float elapsed = (std::max)(0.0f, deltaTime);
	elapsedTime_ += elapsed;
	position_.x += direction_.x * speed_ * elapsed;
	position_.z += direction_.z * speed_ * elapsed;
	lifetime_ -= elapsed;
	active_ = lifetime_ > 0.0f;
	if (object_) {
		const float angle = std::atan2(direction_.x, direction_.z);
		const float fade = std::clamp(lifetime_ / kFadeOutDuration, 0.0f, 1.0f);
		const float fadeScale = 0.76f + fade * 0.24f;
		if (cubeModel_) {
			const float scale = std::clamp(collisionRadius_ * 1.18f * fadeScale, 0.001f, 2.4f);
			object_->SetScale({ scale, scale, scale });
			object_->SetRotate({ 0.0f, angle, 0.0f });
		} else {
			object_->SetRotate({ 0.0f, angle, 0.0f });
		}
		object_->SetColor({ color_.x, color_.y, color_.z, color_.w * fade });
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
