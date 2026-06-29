#include "GridPlane.h"
#include "GameModelCache.h"
#include "Object3DCommon.h"
#include <algorithm>
#include <cmath>

namespace {

constexpr char kEnvironmentTexturePath[] = "Resources/textures/skybox/test.dds";
constexpr float kGroundY = 0.0f;

}

namespace DirectXGame {

void GridPlane::Initialize()
{
	const ModelHandle planeHandle = GameModelCache::Load("plane.obj");

	for (std::unique_ptr<Engine::Graphics3D::Object3D>& tile : tiles_) {
		tile = std::make_unique<Engine::Graphics3D::Object3D>();
		tile->Initialize(Engine::Graphics3D::Object3DCommon::GetInstance());
		GameModelCache::ApplyToObject(*tile, planeHandle);
		tile->SetSkyboxFilePath(kEnvironmentTexturePath);
		tile->SetEnvironmentReflectionStrength(0.0f);
		tile->SetEnvironmentRoughness(1.0f);
		tile->SetTextureInfluence(0.12f);
		tile->SetCastsShadow(false);
		tile->SetScale({ kGroundScale, 1.0f, kGroundScale });
		tile->SetTranslate({ 0.0f, kGroundY, 0.0f });
		tile->SetColor({ 0.43f, 0.44f, 0.47f, 1.0f });
	}
}

void GridPlane::Update(const Vector3& focusPosition)
{
	animationTime_ += 1.0f / 60.0f;
	const float centerX = SnapToTile(focusPosition.x);
	const float centerZ = SnapToTile(focusPosition.z);

	size_t index = 0;
	for (int z = -kTileRadius; z <= kTileRadius; ++z) {
		for (int x = -kTileRadius; x <= kTileRadius; ++x) {
			if (!tiles_[index]) {
				++index;
				continue;
			}
			tiles_[index]->SetTranslate({
				centerX + static_cast<float>(x) * kTileSpan,
				kGroundY,
				centerZ + static_cast<float>(z) * kTileSpan,
				});
			const float centerDistance = std::sqrt(static_cast<float>(x * x + z * z));
			const float edgeShade = std::clamp(1.0f - centerDistance * 0.035f, 0.88f, 1.0f);
			tiles_[index]->SetColor({
				0.43f * edgeShade,
				0.44f * edgeShade,
				0.47f * edgeShade,
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

void GridPlane::DrawShadow()
{
	for (const std::unique_ptr<Engine::Graphics3D::Object3D>& tile : tiles_) {
		if (tile) {
			tile->DrawShadow();
		}
	}
}

float GridPlane::SnapToTile(float value)
{
	return std::floor(value / kTileSpan) * kTileSpan;
}

} // namespace DirectXGame
