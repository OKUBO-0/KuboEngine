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
constexpr float kEnemyModelBasePitch = 0.0f;
constexpr float kDefaultOctopusModelGroundOffsetY = 4.7f;
constexpr char kOctopusModelGroundOffsetKey[] =
	"enemy.octopusModelGroundOffsetY";
constexpr float kEnemyFixedAnimationDeltaTime = 1.0f / 60.0f;
constexpr float kEnemyImpactMotionDuration = 0.18f;
constexpr float kEnemyAttackMotionDuration = 0.82f;
constexpr float kEnemyJumpMotionDuration = 0.72f;
constexpr float kEnemyLandMotionDuration = 0.42f;
constexpr float kSimplifiedModelBaseScale = 1.0f;

float gOctopusModelGroundOffsetY = kDefaultOctopusModelGroundOffsetY;

struct EnemyMotionProfile {
	float cycleSpeed;
	float bobHeight;
	float rollAmount;
	float leanAmount;
	float squashAmount;
};

struct EnemyColliderProfile {
	float radius;
	float height;
};

EnemyColliderProfile GetEnemyColliderProfile(int32_t type)
{
	switch (type) {
	case static_cast<int32_t>(DirectXGame::EnemyType::Tackler):
		return { 1.25f, 3.4f };
	case static_cast<int32_t>(DirectXGame::EnemyType::Bomb):
		return { 1.4f, 3.8f };
	case static_cast<int32_t>(DirectXGame::EnemyType::Fast):
		return { 1.55f, 2.4f };
	case static_cast<int32_t>(DirectXGame::EnemyType::Heavy):
		return { 1.9f, 4.4f };
	case static_cast<int32_t>(DirectXGame::EnemyType::Gold):
		return { 2.15f, 5.2f };
	case static_cast<int32_t>(DirectXGame::EnemyType::Boss):
		return { 3.7f, 7.2f };
	case static_cast<int32_t>(DirectXGame::EnemyType::Standard):
	default:
		return { 1.35f, 3.6f };
	}
}

EnemyMotionProfile GetEnemyMotionProfile(int32_t type)
{
	switch (type) {
	case static_cast<int32_t>(DirectXGame::EnemyType::Tackler):
		return { 6.5f, 0.035f, 0.025f, 0.020f, 0.006f };
	case static_cast<int32_t>(DirectXGame::EnemyType::Bomb):
		return { 4.6f, 0.025f, 0.015f, 0.012f, 0.004f };
	case static_cast<int32_t>(DirectXGame::EnemyType::Fast):
		return { 8.0f, 0.035f, 0.035f, 0.025f, 0.006f };
	case static_cast<int32_t>(DirectXGame::EnemyType::Heavy):
		return { 3.2f, 0.025f, 0.012f, 0.010f, 0.004f };
	case static_cast<int32_t>(DirectXGame::EnemyType::Gold):
		return { 3.6f, 0.020f, 0.010f, 0.008f, 0.003f };
	case static_cast<int32_t>(DirectXGame::EnemyType::Boss):
		return { 2.8f, 0.025f, 0.010f, 0.010f, 0.003f };
	case static_cast<int32_t>(DirectXGame::EnemyType::Standard):
	default:
		return { 4.8f, 0.030f, 0.018f, 0.014f, 0.005f };
	}
}

const char* GetEnemyMoveClipName(int32_t type)
{
	switch (type) {
	case static_cast<int32_t>(DirectXGame::EnemyType::Tackler):
	case static_cast<int32_t>(DirectXGame::EnemyType::Fast):
	case static_cast<int32_t>(DirectXGame::EnemyType::Gold):
		return "run";
	case static_cast<int32_t>(DirectXGame::EnemyType::Standard):
	case static_cast<int32_t>(DirectXGame::EnemyType::Bomb):
	case static_cast<int32_t>(DirectXGame::EnemyType::Heavy):
	case static_cast<int32_t>(DirectXGame::EnemyType::Boss):
	default:
		return "walk";
	}
}

float GetEnemyAnimationSpeed(int32_t type, bool impact)
{
	if (impact) {
		return 0.85f;
	}
	switch (type) {
	case static_cast<int32_t>(DirectXGame::EnemyType::Tackler):
	case static_cast<int32_t>(DirectXGame::EnemyType::Fast):
		return 0.60f;
	case static_cast<int32_t>(DirectXGame::EnemyType::Standard):
	case static_cast<int32_t>(DirectXGame::EnemyType::Bomb):
		return 0.42f;
	case static_cast<int32_t>(DirectXGame::EnemyType::Heavy):
	case static_cast<int32_t>(DirectXGame::EnemyType::Gold):
		return 0.48f;
	case static_cast<int32_t>(DirectXGame::EnemyType::Boss):
	default:
		return 0.58f;
	}
}

Vector4 GetSimplifiedEnemyColor(int32_t type)
{
	switch (type) {
	case static_cast<int32_t>(DirectXGame::EnemyType::Tackler):
		return { 0.45f, 0.82f, 0.34f, 1.0f };
	case static_cast<int32_t>(DirectXGame::EnemyType::Bomb):
		return { 0.84f, 0.83f, 0.74f, 1.0f };
	case static_cast<int32_t>(DirectXGame::EnemyType::Fast):
		return { 0.36f, 0.45f, 0.56f, 1.0f };
	case static_cast<int32_t>(DirectXGame::EnemyType::Heavy):
		return { 0.78f, 0.88f, 0.92f, 1.0f };
	case static_cast<int32_t>(DirectXGame::EnemyType::Gold):
		return { 1.0f, 0.78f, 0.25f, 1.0f };
	case static_cast<int32_t>(DirectXGame::EnemyType::Boss):
		return { 0.88f, 0.16f, 0.10f, 1.0f };
	case static_cast<int32_t>(DirectXGame::EnemyType::Standard):
	default:
		return { 0.55f, 0.72f, 0.42f, 1.0f };
	}
}

}

namespace DirectXGame {

void EnemyView::Initialize()
{
	object_ = std::make_unique<Engine::Graphics3D::Object3D>();
	object_->Initialize(
		Engine::Graphics3D::Object3DCommon::GetInstance());
	object_->SetSkyboxFilePath(kEnvironmentTexturePath);
	object_->SetEnvironmentReflectionStrength(0.0f);
	object_->SetEnvironmentRoughness(1.0f);
}

void EnemyView::Update(
	const Vector3& position,
	float rotationY,
	bool hitFlash,
	bool phaseFlash)
{
	animationTime_ += kEnemyFixedAnimationDeltaTime;
	if (hitFlash || phaseFlash) {
		impactMotionTimer_ = kEnemyImpactMotionDuration;
	} else {
		impactMotionTimer_ =
			(std::max)(0.0f, impactMotionTimer_ - kEnemyFixedAnimationDeltaTime);
	}
	attackMotionTimer_ =
		(std::max)(0.0f, attackMotionTimer_ - kEnemyFixedAnimationDeltaTime);
	jumpMotionTimer_ =
		(std::max)(0.0f, jumpMotionTimer_ - kEnemyFixedAnimationDeltaTime);
	landMotionTimer_ =
		(std::max)(0.0f, landMotionTimer_ - kEnemyFixedAnimationDeltaTime);

	if (object_ && !simplifiedRenderEnabled_) {
		const bool impact = impactMotionTimer_ > 0.0f;
		const bool landing =
			landMotionTimer_ > 0.0f &&
			(object_->HasAnimationClip("land") ||
				object_->HasAnimationClip("attack"));
		const bool jumping =
			!landing && jumpMotionTimer_ > 0.0f &&
			(object_->HasAnimationClip("jump") ||
				object_->HasAnimationClip("fall"));
		const bool attacking =
			!landing && !jumping &&
			attackMotionTimer_ > 0.0f && object_->HasAnimationClip("attack");
		const char* moveClip = GetEnemyMoveClipName(enemyType_);
		const char* jumpClip = object_->HasAnimationClip("jump")
			? "jump"
			: "fall";
		const char* landClip = object_->HasAnimationClip("land")
			? "land"
			: "attack";
		object_->SetAnimationClip(
			landing
				? landClip
				: (jumping
				? jumpClip
				: (attacking
				? "attack"
				: (impact && object_->HasAnimationClip("hit")
				? "hit"
				: moveClip))));
		object_->SetAnimationSpeed(landing
			? 0.92f
			: (jumping
			? 0.82f
			: (attacking
			? 0.68f
			: GetEnemyAnimationSpeed(enemyType_, impact))));
		object_->SetAnimationLoop(!impact && !attacking && !jumping && !landing);
		object_->SetColor(hitFlash || phaseFlash
			? Vector4{ 8.0f, 8.0f, 8.0f, 1.0f }
			: behaviorColor_);
		ApplyTransform(position, rotationY);
		object_->Update();
	}
	if (simplifiedObject_) {
		simplifiedObject_->SetColor(hitFlash || phaseFlash
			? Vector4{ 5.0f, 5.0f, 5.0f, 1.0f }
			: GetSimplifiedEnemyColor(enemyType_));
		ApplySimplifiedTransform(position, rotationY);
		simplifiedObject_->Update();
	}
	UpdateFloatingShadow(position, rotationY);
}

void EnemyView::Draw(bool visible)
{
	if (!visible || !object_) {
		return;
	}
	DrawFloatingShadow(visible);
	DrawModel(visible);
}

void EnemyView::DrawFloatingShadow(bool visible)
{
	(void)visible;
}

void EnemyView::DrawModel(bool visible)
{
	if (!visible) {
		return;
	}
	if (simplifiedRenderEnabled_ && simplifiedObject_) {
		Engine::Graphics3D::Object3D::SubmitForDraw(simplifiedObject_.get());
		return;
	}
	if (object_) {
		Engine::Graphics3D::Object3D::SubmitForDraw(object_.get());
	}
}

void EnemyView::DrawShadow(bool visible)
{
	if (!visible) {
		return;
	}
	if (simplifiedRenderEnabled_ && simplifiedObject_) {
		Engine::Graphics3D::Object3D::SubmitForShadow(simplifiedObject_.get());
		return;
	}
	if (object_) {
		Engine::Graphics3D::Object3D::SubmitForShadow(object_.get());
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
	const float shrinkProgress =
		std::clamp((progress - 0.28f) / 0.72f, 0.0f, 1.0f);
	const float shrink =
		1.0f - shrinkProgress * shrinkProgress *
		(3.0f - 2.0f * shrinkProgress) * 0.88f;
	const float scale =
		behaviorScaleMultiplier_ * spawnScaleMultiplier_ *
		modelVisualScaleMultiplier_ * (1.0f + pulse * 0.18f) * shrink;
	const float verticalOffsetY = GetEffectiveModelVerticalOffsetY();
	object_->SetFrustumCullingEnabled(false);
	object_->SetAnimationClip("death");
	object_->SetAnimationLoop(false);
	object_->SetAnimationSpeed(0.48f);
	object_->SetScale({ scale, scale, scale });
	const bool hasDeathClip = object_->HasAnimationClip("death");
	object_->SetRotate(hasDeathClip
		? Vector3{ kEnemyModelBasePitch, rotationY, 0.0f }
		: Vector3{
			kEnemyModelBasePitch + progress * 1.32f,
			rotationY + progress * 0.42f,
			progress * -0.36f,
			});
	object_->SetTranslate({
		position.x,
		position.y + verticalOffsetY - (hasDeathClip ? 0.0f : progress * 1.15f),
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

void EnemyView::ApplyPresentationPose(
	const Vector3& position,
	float rotationY,
	bool idleMotion)
{
	if (!object_) {
		return;
	}

	object_->SetFrustumCullingEnabled(false);
	if (idleMotion) {
		const char* idleClip = object_->HasAnimationClip("idle")
			? "idle"
			: "idle_hold";
		object_->SetAnimationClip(idleClip);
		object_->SetAnimationSpeed(0.42f);
		object_->SetAnimationLoop(true);
	}
	const float visualScale = modelVisualScaleMultiplier_;
	object_->SetScale({ visualScale, visualScale, visualScale });
	object_->SetRotate({ kEnemyModelBasePitch, rotationY, 0.0f });
	object_->SetTranslate({
		position.x,
		position.y + GetEffectiveModelVerticalOffsetY(),
		position.z,
		});
	object_->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
	object_->Update();
	UpdateFloatingShadow(position, rotationY);
}

void EnemyView::NotifyAttack()
{
	attackMotionTimer_ = kEnemyAttackMotionDuration;
}

void EnemyView::NotifyJump()
{
	jumpMotionTimer_ = kEnemyJumpMotionDuration;
}

void EnemyView::NotifyLand()
{
	landMotionTimer_ = kEnemyLandMotionDuration;
	jumpMotionTimer_ = 0.0f;
}

void EnemyView::SetAnimationUpdateStride(uint32_t stride)
{
	if (object_) {
		object_->SetAnimationUpdateStride(stride);
	}
}

void EnemyView::SetFrustumCullingEnabled(bool enabled)
{
	const bool objectCullingEnabled = enabled && !forceDisableFrustumCulling_;
	if (object_) {
		object_->SetFrustumCullingEnabled(objectCullingEnabled);
	}
	if (simplifiedObject_) {
		simplifiedObject_->SetFrustumCullingEnabled(enabled);
	}
}

void EnemyView::SetSimplifiedRenderEnabled(bool enabled)
{
	simplifiedRenderEnabled_ = enabled;
	if (simplifiedRenderEnabled_) {
		InitializeSimplifiedObject();
	}
}

void EnemyView::SetModelByType(int32_t type)
{
	enemyType_ = type;
	const char* modelName = "cube_world/zombie.glb";
	modelVisualScaleMultiplier_ = 1.0f;
	modelVerticalOffsetY_ = 0.0f;
	forceDisableFrustumCulling_ = false;
	switch (type) {
	case static_cast<int32_t>(EnemyType::Standard):
		modelName = "cube_world/zombie.glb";
		modelVisualScaleMultiplier_ = 2.0f;
		break;
	case static_cast<int32_t>(EnemyType::Tackler):
		modelName = "cube_world/goblin.glb";
		modelVisualScaleMultiplier_ = 1.85f;
		break;
	case static_cast<int32_t>(EnemyType::Bomb):
		modelName = "cube_world/skeleton.glb";
		modelVisualScaleMultiplier_ = 1.9f;
		break;
	case static_cast<int32_t>(EnemyType::Fast):
		modelName = "cube_world/wolf.glb";
		modelVisualScaleMultiplier_ = 1.6f;
		break;
	case static_cast<int32_t>(EnemyType::Heavy):
		modelName = "cube_world/yeti.glb";
		modelVisualScaleMultiplier_ = 2.35f;
		forceDisableFrustumCulling_ = true;
		break;
	case static_cast<int32_t>(EnemyType::Gold):
		modelName = "cube_world/giant.glb";
		modelVisualScaleMultiplier_ = 2.6f;
		forceDisableFrustumCulling_ = true;
		break;
	case static_cast<int32_t>(EnemyType::Boss):
		modelName = "cube_world/demon.glb";
		modelVisualScaleMultiplier_ = 4.15f;
		forceDisableFrustumCulling_ = true;
		break;
	default: break;
	}
	usesOctopusGroundOffset_ = std::string_view(modelName) == "octopus.obj";
	if (usesOctopusGroundOffset_) {
		modelVerticalOffsetY_ = GetOctopusModelGroundOffsetY();
	}

	if (object_) {
		const ModelHandle modelHandle =
			GameModelCache::Load(modelName);
		GameModelCache::ApplyToObject(*object_, modelHandle);
		object_->SetEnvironmentReflectionStrength(0.0f);
		object_->SetEnvironmentRoughness(1.0f);
		object_->SetCastsShadow(true);
		object_->SetFrustumCullingEnabled(!forceDisableFrustumCulling_);
	}
	if (simplifiedObject_) {
		simplifiedObject_->SetColor(GetSimplifiedEnemyColor(enemyType_));
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

void EnemyView::LoadVisualTuning(
	const DirectXGame::UILayoutIO::LayoutMap& tuning)
{
	SetOctopusModelGroundOffsetY(UILayoutIO::GetFloat(
		tuning,
		kOctopusModelGroundOffsetKey,
		kDefaultOctopusModelGroundOffsetY));
}

void EnemyView::AppendVisualTuningEntries(
	std::vector<DirectXGame::UILayoutIO::Entry>& entries)
{
	entries.push_back({
		kOctopusModelGroundOffsetKey,
		{ GetOctopusModelGroundOffsetY() },
	});
}

float EnemyView::GetOctopusModelGroundOffsetY()
{
	return gOctopusModelGroundOffsetY;
}

void EnemyView::SetOctopusModelGroundOffsetY(float offsetY)
{
	gOctopusModelGroundOffsetY = std::clamp(offsetY, 0.0f, 8.0f);
}

float EnemyView::GetCollisionRadius() const
{
	return GetEnemyColliderProfile(enemyType_).radius;
}

Engine::Math::AABB EnemyView::GetCollisionAabb(
	const Vector3& fallbackPosition) const
{
	const EnemyColliderProfile profile =
		GetEnemyColliderProfile(enemyType_);
	const float bottomY = fallbackPosition.y;
	const float topY = bottomY + profile.height;
	return {
		{
			fallbackPosition.x - profile.radius,
			bottomY,
			fallbackPosition.z - profile.radius,
		},
		{
			fallbackPosition.x + profile.radius,
			topY,
			fallbackPosition.z + profile.radius,
		},
	};
}

Engine::Math::OBB EnemyView::GetCollisionObb(
	const Vector3& fallbackPosition) const
{
	const EnemyColliderProfile profile =
		GetEnemyColliderProfile(enemyType_);
	const float bottomY = fallbackPosition.y;
	return {
		{
			fallbackPosition.x,
			bottomY + profile.height * 0.5f,
			fallbackPosition.z,
		},
		{
			Vector3{ 1.0f, 0.0f, 0.0f },
			Vector3{ 0.0f, 1.0f, 0.0f },
			Vector3{ 0.0f, 0.0f, 1.0f },
		},
		Vector3{
			profile.radius,
			profile.height * 0.5f,
			profile.radius,
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

void EnemyView::InitializeSimplifiedObject()
{
	if (simplifiedObject_) {
		return;
	}

	simplifiedObject_ =
		std::make_unique<Engine::Graphics3D::Object3D>();
	simplifiedObject_->Initialize(
		Engine::Graphics3D::Object3DCommon::GetInstance());
	const ModelHandle cubeHandle = GameModelCache::Load("cube.obj");
	GameModelCache::ApplyToObject(*simplifiedObject_, cubeHandle);
	simplifiedObject_->SetSkyboxFilePath(kEnvironmentTexturePath);
	simplifiedObject_->SetEnvironmentReflectionStrength(0.0f);
	simplifiedObject_->SetEnvironmentRoughness(1.0f);
	simplifiedObject_->SetTextureInfluence(0.0f);
	simplifiedObject_->SetCastsShadow(true);
	simplifiedObject_->SetColor(GetSimplifiedEnemyColor(enemyType_));
}

void EnemyView::ApplyTransform(
	const Vector3& position,
	float rotationY)
{
	if (!object_) {
		return;
	}
	const float scale = behaviorScaleMultiplier_ * spawnScaleMultiplier_;
	const float visualScale = scale * modelVisualScaleMultiplier_;
	const float verticalOffsetY = GetEffectiveModelVerticalOffsetY();
	const EnemyMotionProfile profile = GetEnemyMotionProfile(enemyType_);
	const float cycle = std::sin(animationTime_ * profile.cycleSpeed);
	const float step = std::abs(cycle);
	const float impactProgress =
		impactMotionTimer_ > 0.0f
			? impactMotionTimer_ / kEnemyImpactMotionDuration
			: 0.0f;
	const float impactPulse = std::sin(impactProgress * 3.14159265f);
	const float squash = 1.0f - step * profile.squashAmount + impactPulse * 0.035f;
	object_->SetScale({
		visualScale,
		visualScale * squash,
		visualScale,
		});
	object_->SetRotate({
		kEnemyModelBasePitch + profile.leanAmount + impactPulse * -0.10f,
		rotationY,
		cycle * profile.rollAmount,
		});
	object_->SetTranslate({
		position.x,
		position.y + verticalOffsetY + step * profile.bobHeight,
		position.z,
		});
}

void EnemyView::ApplySimplifiedTransform(
	const Vector3& position,
	float rotationY)
{
	if (!simplifiedObject_) {
		return;
	}
	const EnemyColliderProfile collider = GetEnemyColliderProfile(enemyType_);
	const float scale = behaviorScaleMultiplier_ * spawnScaleMultiplier_;
	const float bob =
		std::abs(std::sin(animationTime_ * GetEnemyMotionProfile(enemyType_).cycleSpeed)) *
		0.04f;
	simplifiedObject_->SetScale({
		collider.radius * kSimplifiedModelBaseScale * scale,
		collider.height * 0.5f * kSimplifiedModelBaseScale * scale,
		collider.radius * kSimplifiedModelBaseScale * scale,
		});
	simplifiedObject_->SetRotate({
		0.0f,
		rotationY,
		0.0f,
		});
	simplifiedObject_->SetTranslate({
		position.x,
		position.y + collider.height * 0.5f * scale + bob,
		position.z,
		});
}

float EnemyView::GetEffectiveModelVerticalOffsetY() const
{
	return usesOctopusGroundOffset_
		? GetOctopusModelGroundOffsetY()
		: modelVerticalOffsetY_;
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
		std::clamp(0.56f - altitude * 0.045f, 0.24f, 0.52f);
	floatingShadowObject_->SetScale({
		shadowScale * 1.42f,
		1.0f,
		shadowScale * 0.84f,
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
