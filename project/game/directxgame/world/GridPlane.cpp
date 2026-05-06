#include "game/directxgame/world/GridPlane.h"
#include "game/directxgame/core/GameModelCache.h"
#include "Object3DCommon.h"
#include "TextureManager.h"
#include <algorithm>
#include <cmath>

namespace {

constexpr char kEnvironmentTexturePath[] = "Resources/textures/skybox/test.dds";

}

namespace DirectXGame {

void GridPlane::Initialize()
{
	Engine::Base::TextureManager::GetInstance()->LoadTexture(kEnvironmentTexturePath);
	const ModelHandle planeHandle = GameModelCache::Load("plane.obj");

	for (std::unique_ptr<Engine::Graphics3D::Object3D>& tile : tiles_) {
		tile = std::make_unique<Engine::Graphics3D::Object3D>();
		tile->Initialize(Engine::Graphics3D::Object3DCommon::GetInstance());
		GameModelCache::ApplyToObject(*tile, planeHandle);
		tile->SetSkyboxFilePath(kEnvironmentTexturePath);
		tile->SetEnvironmentReflectionStrength(0.0f);
		tile->SetEnvironmentRoughness(1.0f);
		tile->SetScale({ kGroundScale, 1.0f, kGroundScale });
		tile->SetTranslate({ 0.0f, -2.0f, 0.0f });
		tile->SetColor({ 0.72f, 0.86f, 0.78f, 1.0f });
		lightSettings_.ApplyTo(*tile);
	}
}

void GridPlane::Update(const Vector3& focusPosition)
{
	animationTime_ += 1.0f / 60.0f;
	const float centerX = SnapToTile(focusPosition.x);
	const float centerZ = SnapToTile(focusPosition.z);

	size_t index = 0;
	for (int z = -1; z <= 1; ++z) {
		for (int x = -1; x <= 1; ++x) {
			if (!tiles_[index]) {
				++index;
				continue;
			}
			tiles_[index]->SetTranslate({
				centerX + static_cast<float>(x) * kTileSpan,
				-2.0f,
				centerZ + static_cast<float>(z) * kTileSpan,
				});
			const float centerDistance = std::sqrt(static_cast<float>(x * x + z * z));
			const float pulse = 0.5f + 0.5f * std::sin(animationTime_ * 0.7f + static_cast<float>(index));
			const float edgeShade = std::clamp(1.0f - centerDistance * 0.08f, 0.78f, 1.0f);
			tiles_[index]->SetColor({
				(0.56f + pulse * 0.05f) * edgeShade,
				(0.74f + pulse * 0.08f) * edgeShade,
				(0.62f + pulse * 0.05f) * edgeShade,
				1.0f,
				});
			tiles_[index]->Update();
			++index;
		}
	}
}

void GridPlane::Draw()
{
	for (const std::unique_ptr<Engine::Graphics3D::Object3D>& tile : tiles_) {
		if (tile) {
			tile->Draw();
		}
	}
}

void GridPlane::SetLightSettings(const GameLightSettings& lightSettings)
{
	lightSettings_ = lightSettings;
	for (const std::unique_ptr<Engine::Graphics3D::Object3D>& tile : tiles_) {
		if (tile) {
			lightSettings_.ApplyTo(*tile);
		}
	}
}

float GridPlane::SnapToTile(float value)
{
	return std::floor(value / kTileSpan) * kTileSpan;
}

} // namespace DirectXGame
