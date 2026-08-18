#include "PlayerView.h"

#include "Object3DCommon.h"
#include "GameModelCache.h"

namespace {

constexpr char kEnvironmentTexturePath[] =
	"Resources/textures/skybox/test.dds";
constexpr char kPlayerModelPath[] = "cube_world/cube_guy.glb";
constexpr float kPlayerModelScale = 2.0f;
constexpr float kPlayerFallbackCollisionRadius = 1.0f;
constexpr float kPlayerContactShadowY = -1.84f;
constexpr float kPlayerHitReactDuration = 0.22f;
constexpr float kPlayerColliderRadius = 1.25f;
constexpr float kPlayerColliderHeight = 3.8f;

}

namespace DirectXGame {

void PlayerView::Initialize()
{
	const ModelHandle playerHandle = GameModelCache::Load(kPlayerModelPath);
	playerObject_ = std::make_unique<Engine::Graphics3D::Object3D>();
	playerObject_->Initialize(
		Engine::Graphics3D::Object3DCommon::GetInstance());
	GameModelCache::ApplyToObject(*playerObject_, playerHandle);
	playerObject_->SetSkyboxFilePath(kEnvironmentTexturePath);
	playerObject_->SetEnvironmentReflectionStrength(0.0f);
	playerObject_->SetEnvironmentRoughness(1.0f);
	playerObject_->SetCastsShadow(true);
	playerObject_->SetScale({
		kPlayerModelScale,
		kPlayerModelScale,
		kPlayerModelScale,
		});

	const ModelHandle indicatorHandle = GameModelCache::Load("cube.obj");
	aimIndicatorObject_ =
		std::make_unique<Engine::Graphics3D::Object3D>();
	aimIndicatorObject_->Initialize(
		Engine::Graphics3D::Object3DCommon::GetInstance());
	GameModelCache::ApplyToObject(*aimIndicatorObject_, indicatorHandle);
	aimIndicatorObject_->SetSkyboxFilePath(kEnvironmentTexturePath);
	aimIndicatorObject_->SetEnvironmentReflectionStrength(0.0f);
	aimIndicatorObject_->SetEnvironmentRoughness(1.0f);
	aimIndicatorObject_->SetLighting(false);
	aimIndicatorObject_->SetColor({ 0.15f, 1.0f, 1.0f, 1.0f });

	const ModelHandle shadowHandle = GameModelCache::Load("plane.obj");
	contactShadowObject_ = std::make_unique<Engine::Graphics3D::Object3D>();
	contactShadowObject_->Initialize(
		Engine::Graphics3D::Object3DCommon::GetInstance());
	GameModelCache::ApplyToObject(*contactShadowObject_, shadowHandle);
	contactShadowObject_->SetSkyboxFilePath(kEnvironmentTexturePath);
	contactShadowObject_->SetEnvironmentReflectionStrength(0.0f);
	contactShadowObject_->SetEnvironmentRoughness(1.0f);
	contactShadowObject_->SetLighting(false);
	contactShadowObject_->SetCastsShadow(false);
	contactShadowObject_->SetColor({ 0.02f, 0.025f, 0.035f, 0.48f });
}

void PlayerView::SetCharacterId(CharacterId characterId)
{
	characterId_ = characterId;
}

void PlayerView::SetMoving(bool moving)
{
	moving_ = moving;
}

void PlayerView::SetMotionState(bool moving, bool dashing, bool jumping)
{
	moving_ = moving;
	dashing_ = dashing;
	jumping_ = jumping;
}

void PlayerView::NotifyHitReact()
{
	hitReactTimer_ = kPlayerHitReactDuration;
}

void PlayerView::SetDeathAnimationActive(bool active)
{
	deathAnimationActive_ = active;
	if (active && playerObject_) {
		playerObject_->SetAnimationClip("death");
		playerObject_->SetAnimationSpeed(0.72f);
		playerObject_->SetAnimationLoop(false);
	}
}

void PlayerView::Update()
{
	hitReactTimer_ =
		(std::max)(0.0f, hitReactTimer_ - (1.0f / 60.0f));
	if (playerObject_) {
		const char* idleClip = playerObject_->HasAnimationClip("idle")
			? "idle"
			: "idle_hold";
		if (deathAnimationActive_ && playerObject_->HasAnimationClip("death")) {
			playerObject_->SetAnimationClip("death");
			playerObject_->SetAnimationSpeed(0.72f);
		} else if (hitReactTimer_ > 0.0f && playerObject_->HasAnimationClip("hit")) {
			playerObject_->SetAnimationClip("hit");
			playerObject_->SetAnimationSpeed(1.0f);
			playerObject_->SetAnimationLoop(false);
		} else if (jumping_ && playerObject_->HasAnimationClip("jump")) {
			playerObject_->SetAnimationClip("jump");
			playerObject_->SetAnimationSpeed(0.82f);
			playerObject_->SetAnimationLoop(false);
		} else if (dashing_ && playerObject_->HasAnimationClip("run")) {
			playerObject_->SetAnimationClip("run");
			playerObject_->SetAnimationSpeed(1.25f);
			playerObject_->SetAnimationLoop(true);
		} else {
			playerObject_->SetAnimationClip(moving_ ? "walk" : idleClip);
			playerObject_->SetAnimationSpeed(moving_ ? 0.72f : 0.45f);
			playerObject_->SetAnimationLoop(true);
		}
		playerObject_->Update();
	}
	if (aimIndicatorObject_) {
		aimIndicatorObject_->Update();
	}
	if (contactShadowObject_ && playerObject_) {
		const EulerTransform& playerTransform = playerObject_->GetTransform();
		const float jumpFade = jumping_ ? 0.0f : 1.0f;
		contactShadowObject_->SetScale({ 2.65f, 1.0f, 1.65f });
		contactShadowObject_->SetRotate({ 0.0f, playerTransform.rotate.y, 0.0f });
		contactShadowObject_->SetTranslate({
			playerTransform.translate.x,
			kPlayerContactShadowY,
			playerTransform.translate.z,
			});
		contactShadowObject_->SetColor({ 0.02f, 0.025f, 0.035f, 0.48f * jumpFade });
		contactShadowObject_->Update();
	}
}

void PlayerView::Draw(bool playerVisible, bool aimIndicatorVisible)
{
	(void)aimIndicatorVisible;
	if (playerVisible && playerObject_) {
		Engine::Graphics3D::Object3D::SubmitForDraw(playerObject_.get());
	}
}

void PlayerView::DrawShadow(bool playerVisible)
{
	if (playerVisible && playerObject_) {
		Engine::Graphics3D::Object3D::SubmitForShadow(playerObject_.get());
	}
}

float PlayerView::GetCollisionRadius() const
{
	return kPlayerColliderRadius;
}

Engine::Math::AABB PlayerView::GetCollisionAabb(
	const Vector3& fallbackPosition) const
{
	const float bottomY = fallbackPosition.y;
	const float topY = bottomY + kPlayerColliderHeight;
	return {
		{
			fallbackPosition.x - kPlayerColliderRadius,
			bottomY,
			fallbackPosition.z - kPlayerColliderRadius,
		},
		{
			fallbackPosition.x + kPlayerColliderRadius,
			topY,
			fallbackPosition.z + kPlayerColliderRadius,
		},
	};
}

Engine::Math::OBB PlayerView::GetCollisionObb(
	const Vector3& fallbackPosition) const
{
	const float bottomY = fallbackPosition.y;
	return {
		{
			fallbackPosition.x,
			bottomY + kPlayerColliderHeight * 0.5f,
			fallbackPosition.z,
		},
		{
			Vector3{ 1.0f, 0.0f, 0.0f },
			Vector3{ 0.0f, 1.0f, 0.0f },
			Vector3{ 0.0f, 0.0f, 1.0f },
		},
		Vector3{
			kPlayerColliderRadius,
			kPlayerColliderHeight * 0.5f,
			kPlayerColliderRadius,
		},
	};
}

} // namespace DirectXGame
