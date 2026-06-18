#include "game/directxgame/player/PlayerView.h"

#include "Object3DCommon.h"
#include "TextureManager.h"
#include "game/directxgame/core/GameModelCache.h"

namespace {

constexpr char kEnvironmentTexturePath[] =
	"Resources/textures/skybox/test.dds";
constexpr float kPlayerModelScale = 1.0f;
constexpr float kPlayerFallbackCollisionRadius = 1.0f;

}

namespace DirectXGame {

void PlayerView::Initialize()
{
	Engine::Base::TextureManager::GetInstance()->LoadTexture(
		kEnvironmentTexturePath);

	const ModelHandle playerHandle = GameModelCache::Load("cube.obj");
	playerObject_ = std::make_unique<Engine::Graphics3D::Object3D>();
	playerObject_->Initialize(
		Engine::Graphics3D::Object3DCommon::GetInstance());
	GameModelCache::ApplyToObject(*playerObject_, playerHandle);
	playerObject_->SetSkyboxFilePath(kEnvironmentTexturePath);
	playerObject_->SetEnvironmentReflectionStrength(0.0f);
	playerObject_->SetEnvironmentRoughness(1.0f);
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
}

void PlayerView::Update()
{
	if (playerObject_) {
		playerObject_->Update();
	}
	if (aimIndicatorObject_) {
		aimIndicatorObject_->Update();
	}
}

void PlayerView::Draw(bool playerVisible, bool aimIndicatorVisible)
{
	if (aimIndicatorVisible && aimIndicatorObject_) {
		aimIndicatorObject_->Draw();
	}
	if (playerVisible && playerObject_) {
		playerObject_->Draw();
	}
}

void PlayerView::DrawShadow(bool playerVisible)
{
	if (playerVisible && playerObject_) {
		playerObject_->DrawShadow();
	}
}

float PlayerView::GetCollisionRadius() const
{
	return playerObject_
		? playerObject_->GetScaledModelBoundingRadius(
			kPlayerFallbackCollisionRadius)
		: kPlayerFallbackCollisionRadius;
}

Engine::Math::AABB PlayerView::GetCollisionAabb(
	const Vector3& fallbackPosition) const
{
	if (playerObject_) {
		return playerObject_->GetScaledModelAabb(
			kPlayerFallbackCollisionRadius);
	}
	return {
		{
			fallbackPosition.x - kPlayerFallbackCollisionRadius,
			fallbackPosition.y - kPlayerFallbackCollisionRadius,
			fallbackPosition.z - kPlayerFallbackCollisionRadius,
		},
		{
			fallbackPosition.x + kPlayerFallbackCollisionRadius,
			fallbackPosition.y + kPlayerFallbackCollisionRadius,
			fallbackPosition.z + kPlayerFallbackCollisionRadius,
		},
	};
}

Engine::Math::OBB PlayerView::GetCollisionObb(
	const Vector3& fallbackPosition) const
{
	if (playerObject_) {
		return playerObject_->GetScaledModelObb(
			kPlayerFallbackCollisionRadius);
	}
	return {
		fallbackPosition,
		{
			Vector3{ 1.0f, 0.0f, 0.0f },
			Vector3{ 0.0f, 1.0f, 0.0f },
			Vector3{ 0.0f, 0.0f, 1.0f },
		},
		Vector3{
			kPlayerFallbackCollisionRadius,
			kPlayerFallbackCollisionRadius,
			kPlayerFallbackCollisionRadius,
		},
	};
}

} // namespace DirectXGame
