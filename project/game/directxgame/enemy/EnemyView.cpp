#include "EnemyView.h"
#include "EnemyDefinition.h"

#include "Object3DCommon.h"
#include "GameModelCache.h"
#include <algorithm>
#include <cmath>

namespace {

constexpr char kEnvironmentTexturePath[] =
	"Resources/textures/skybox/test.dds";
constexpr float kEnemyFallbackCollisionRadius = 1.35f;
constexpr float kFloatingShadowGroundY = -1.84f;
constexpr float kOctopusModelGroundOffsetY = 2.12f;

}

namespace DirectXGame {

void EnemyView::Initialize()
{
	object_ = std::make_unique<Engine::Graphics3D::Object3D>();
	object_->Initialize(
		Engine::Graphics3D::Object3DCommon::GetInstance());
	object_->SetSkyboxFilePath(kEnvironmentTexturePath);
	object_->SetEnvironmentReflectionStrength(0.08f);
	object_->SetEnvironmentRoughness(0.65f);
}

void EnemyView::Update(
	const Vector3& position,
	float rotationY,
	bool hitFlash,
	bool phaseFlash)
{
	if (object_) {
		object_->SetColor(hitFlash || phaseFlash
			? Vector4{ 8.0f, 8.0f, 8.0f, 1.0f }
			: behaviorColor_);
		ApplyTransform(position, rotationY);
		object_->Update();
	}
	UpdateFloatingShadow(position, rotationY);
}

void EnemyView::Draw(bool visible)
{
	if (!visible || !object_) {
		return;
	}
	if (floatingEnabled_ && floatingShadowObject_) {
		floatingShadowObject_->Draw();
	}
	object_->Draw();
}

void EnemyView::DrawShadow(bool visible)
{
	if (visible && object_) {
		object_->DrawShadow();
	}
}

void EnemyView::ApplyDeathPose(
	const Vector3& position,
	float rotationY,
	float progress)
{
	if (!object_) {
		return;
	}

	const float pulse = std::sin(progress * 3.14159265f);
	const float scale =
		behaviorScaleMultiplier_ * (1.0f + pulse * 0.18f);
	object_->SetScale({ scale, scale, scale });
	object_->SetRotate({
		progress * 1.32f,
		rotationY + progress * 0.42f,
		progress * -0.36f,
		});
	object_->SetTranslate({
		position.x,
		position.y + modelVerticalOffsetY_ - progress * 1.15f,
		position.z,
		});
	object_->SetColor({
		1.0f + pulse * 2.0f,
		0.32f + pulse * 0.35f,
		0.22f,
		1.0f,
		});
	object_->Update();
}

void EnemyView::SetModelByType(int32_t type)
{
	const char* modelName = "Enemy1.obj";
	switch (type) {
	case static_cast<int32_t>(EnemyType::Standard): modelName = "Enemy1.obj"; break;
	case static_cast<int32_t>(EnemyType::Tackler): modelName = "Enemy2.obj"; break;
	case static_cast<int32_t>(EnemyType::Bomb): modelName = "Enemy4.obj"; break;
	case static_cast<int32_t>(EnemyType::Fast): modelName = "Enemy2.obj"; break;
	case static_cast<int32_t>(EnemyType::Heavy): modelName = "Enemy3.obj"; break;
	case static_cast<int32_t>(EnemyType::Gold): modelName = "Enemy3.obj"; break;
	case static_cast<int32_t>(EnemyType::Boss): modelName = "octopus.obj"; break;
	default: break;
	}
	modelVerticalOffsetY_ =
		std::string_view(modelName) == "octopus.obj"
			? kOctopusModelGroundOffsetY
			: 0.0f;

	if (object_) {
		const ModelHandle modelHandle =
			GameModelCache::Load(modelName);
		GameModelCache::ApplyToObject(*object_, modelHandle);
	}
}

void EnemyView::SetFloatingEnabled(bool enabled)
{
	floatingEnabled_ = enabled;
	if (floatingEnabled_) {
		InitializeFloatingShadow();
	} else {
		floatingShadowObject_.reset();
	}
}

void EnemyView::SetBehaviorVisual(
	const Vector4& color,
	float scaleMultiplier)
{
	behaviorColor_ = color;
	behaviorScaleMultiplier_ = scaleMultiplier;
}

void EnemyView::ClearBehaviorVisual()
{
	behaviorColor_ = { 1.0f, 1.0f, 1.0f, 1.0f };
	behaviorScaleMultiplier_ = 1.0f;
}

float EnemyView::GetCollisionRadius() const
{
	return object_
		? object_->GetScaledModelBoundingRadius(
			kEnemyFallbackCollisionRadius)
		: kEnemyFallbackCollisionRadius;
}

Engine::Math::AABB EnemyView::GetCollisionAabb(
	const Vector3& fallbackPosition) const
{
	if (object_) {
		return object_->GetScaledModelAabb(
			kEnemyFallbackCollisionRadius);
	}
	return {
		{
			fallbackPosition.x - kEnemyFallbackCollisionRadius,
			fallbackPosition.y - kEnemyFallbackCollisionRadius,
			fallbackPosition.z - kEnemyFallbackCollisionRadius,
		},
		{
			fallbackPosition.x + kEnemyFallbackCollisionRadius,
			fallbackPosition.y + kEnemyFallbackCollisionRadius,
			fallbackPosition.z + kEnemyFallbackCollisionRadius,
		},
	};
}

Engine::Math::OBB EnemyView::GetCollisionObb(
	const Vector3& fallbackPosition) const
{
	if (object_) {
		return object_->GetScaledModelObb(
			kEnemyFallbackCollisionRadius);
	}
	return {
		fallbackPosition,
		{
			Vector3{ 1.0f, 0.0f, 0.0f },
			Vector3{ 0.0f, 1.0f, 0.0f },
			Vector3{ 0.0f, 0.0f, 1.0f },
		},
		Vector3{
			kEnemyFallbackCollisionRadius,
			kEnemyFallbackCollisionRadius,
			kEnemyFallbackCollisionRadius,
		},
	};
}

void EnemyView::InitializeFloatingShadow()
{
	if (floatingShadowObject_) {
		return;
	}

	floatingShadowObject_ =
		std::make_unique<Engine::Graphics3D::Object3D>();
	floatingShadowObject_->Initialize(
		Engine::Graphics3D::Object3DCommon::GetInstance());
	const ModelHandle planeHandle =
		GameModelCache::Load("plane.obj");
	GameModelCache::ApplyToObject(
		*floatingShadowObject_,
		planeHandle);
	floatingShadowObject_->SetSkyboxFilePath(
		kEnvironmentTexturePath);
	floatingShadowObject_->SetEnvironmentReflectionStrength(0.0f);
	floatingShadowObject_->SetEnvironmentRoughness(1.0f);
	floatingShadowObject_->SetLighting(false);
}

void EnemyView::ApplyTransform(
	const Vector3& position,
	float rotationY)
{
	if (!object_) {
		return;
	}
	object_->SetScale({
		behaviorScaleMultiplier_,
		behaviorScaleMultiplier_,
		behaviorScaleMultiplier_,
		});
	object_->SetRotate({ 0.0f, rotationY, 0.0f });
	object_->SetTranslate({
		position.x,
		position.y + modelVerticalOffsetY_,
		position.z,
		});
}

void EnemyView::UpdateFloatingShadow(
	const Vector3& position,
	float rotationY)
{
	if (!floatingEnabled_ || !floatingShadowObject_) {
		return;
	}

	const float altitude =
		(std::max)(0.0f, position.y - kFloatingShadowGroundY);
	const float shadowScale =
		std::clamp(2.15f + altitude * 0.18f, 2.15f, 3.6f);
	const float shadowAlpha =
		std::clamp(0.46f - altitude * 0.045f, 0.18f, 0.42f);
	floatingShadowObject_->SetScale({
		shadowScale * 1.35f,
		1.0f,
		shadowScale * 0.78f,
		});
	floatingShadowObject_->SetRotate({ 0.0f, rotationY, 0.0f });
	floatingShadowObject_->SetTranslate({
		position.x,
		kFloatingShadowGroundY,
		position.z,
		});
	floatingShadowObject_->SetColor({
		0.02f,
		0.025f,
		0.035f,
		shadowAlpha,
		});
	floatingShadowObject_->Update();
}

} // namespace DirectXGame
