#include "GridPlane.h"
#include "GameModelCache.h"
#include "Object3DCommon.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {

constexpr char kEnvironmentTexturePath[] = "Resources/textures/skybox/test.dds";
constexpr float kGroundY = 0.0f;
constexpr float kLandmarkSpacing = 20.0f;
constexpr float kLandmarkYOffset = 0.06f;
constexpr float kLandmarkScaleBoost = 1.25f;

enum class LandmarkKind : int {
	Grass = 0,
	GrassSmall,
	Rock,
	Bush,
	Flowers,
	Mushroom,
	Plant,
	Tree,
	DeadTree,
	Count,
};

constexpr std::array<const char*, static_cast<size_t>(LandmarkKind::Count)> kLandmarkModels{
	"cube_world/grass.glb",
	"cube_world/grass_small.glb",
	"cube_world/rock.glb",
	"cube_world/bush.glb",
	"cube_world/flowers.glb",
	"cube_world/mushroom.glb",
	"cube_world/plant.glb",
	"cube_world/tree.glb",
	"cube_world/dead_tree.glb",
};

uint32_t HashCell(int x, int z)
{
	uint32_t h = static_cast<uint32_t>(x) * 0x45d9f3bu;
	h ^= static_cast<uint32_t>(z) * 0x27d4eb2du;
	h ^= h >> 15;
	h *= 0x85ebca6bu;
	h ^= h >> 13;
	return h;
}

float Hash01(uint32_t hash, uint32_t shift)
{
	return static_cast<float>((hash >> shift) & 0xffu) / 255.0f;
}

Vector4 GroundTileColor(int tileX, int tileZ, float centerDistance)
{
	const uint32_t hash = HashCell(tileX, tileZ);
	Vector4 color{ 0.23f, 0.43f, 0.19f, 1.0f };
	const float warmGrass = Hash01(hash, 0) - 0.5f;
	const float darkGrass = Hash01(hash, 8) - 0.5f;
	color.x += warmGrass * 0.025f + darkGrass * 0.012f;
	color.y += warmGrass * 0.035f - darkGrass * 0.018f;
	color.z += warmGrass * 0.018f + darkGrass * 0.010f;
	const float edgeShade = std::clamp(1.0f - centerDistance * 0.006f, 0.97f, 1.0f);
	color.x *= edgeShade;
	color.y *= edgeShade;
	color.z *= edgeShade;
	return color;
}

Vector4 LandmarkColor(uint32_t hash)
{
	return {
		0.88f + Hash01(hash, 8) * 0.10f,
		0.88f + Hash01(hash, 16) * 0.10f,
		0.88f + Hash01(hash, 24) * 0.10f,
		1.0f,
	};
}

LandmarkKind SelectLandmarkKind(uint32_t hash)
{
	const uint32_t bucket = hash % 100u;
	if (bucket < 7u) {
		return (hash & 0x20u) ? LandmarkKind::Tree : LandmarkKind::DeadTree;
	}
	if (bucket < 20u) {
		return LandmarkKind::Rock;
	}
	if (bucket < 34u) {
		return LandmarkKind::Bush;
	}
	if (bucket < 48u) {
		return LandmarkKind::Flowers;
	}
	if (bucket < 58u) {
		return LandmarkKind::Mushroom;
	}
	if (bucket < 70u) {
		return LandmarkKind::Plant;
	}
	return (hash & 0x40u) ? LandmarkKind::Grass : LandmarkKind::GrassSmall;
}

Vector3 LandmarkScale(LandmarkKind kind, uint32_t hash)
{
	const float variation = 0.86f + Hash01(hash, 20) * 0.28f;
	switch (kind) {
	case LandmarkKind::Tree:
	case LandmarkKind::DeadTree:
		return { 165.0f * kLandmarkScaleBoost * variation, 165.0f * kLandmarkScaleBoost * variation, 165.0f * kLandmarkScaleBoost * variation };
	case LandmarkKind::Rock:
		return { 88.0f * kLandmarkScaleBoost * variation, 88.0f * kLandmarkScaleBoost * variation, 88.0f * kLandmarkScaleBoost * variation };
	case LandmarkKind::Bush:
		return { 84.0f * kLandmarkScaleBoost * variation, 84.0f * kLandmarkScaleBoost * variation, 84.0f * kLandmarkScaleBoost * variation };
	case LandmarkKind::Flowers:
	case LandmarkKind::Mushroom:
	case LandmarkKind::Plant:
		return { 72.0f * kLandmarkScaleBoost * variation, 72.0f * kLandmarkScaleBoost * variation, 72.0f * kLandmarkScaleBoost * variation };
	case LandmarkKind::Grass:
	case LandmarkKind::GrassSmall:
	default:
		return { 66.0f * kLandmarkScaleBoost * variation, 66.0f * kLandmarkScaleBoost * variation, 66.0f * kLandmarkScaleBoost * variation };
	}
}

bool IsLargeLandmark(LandmarkKind kind)
{
	return kind == LandmarkKind::Tree ||
		kind == LandmarkKind::DeadTree ||
		kind == LandmarkKind::Rock ||
		kind == LandmarkKind::Bush;
}

bool ShouldShowLandmark(LandmarkKind kind, int localX, int localZ, uint32_t hash)
{
	const int distance = (std::max)(std::abs(localX), std::abs(localZ));
	if (distance <= 2) {
		return true;
	}
	if (distance <= 4) {
		return IsLargeLandmark(kind) || (hash % 5u == 0u);
	}
	return (kind == LandmarkKind::Tree || kind == LandmarkKind::DeadTree) &&
		(hash % 3u == 0u);
}

bool ShouldCastLandmarkShadow(LandmarkKind kind, int localX, int localZ)
{
	const int distance = (std::max)(std::abs(localX), std::abs(localZ));
	if (distance <= 2) {
		return IsLargeLandmark(kind);
	}
	return distance <= 3 &&
		(kind == LandmarkKind::Tree || kind == LandmarkKind::DeadTree);
}

}

namespace DirectXGame {

void GridPlane::Initialize()
{
	const ModelHandle planeHandle = GameModelCache::Load("plane.obj");
	for (const char* landmarkModel : kLandmarkModels) {
		GameModelCache::Load(landmarkModel);
	}

	farGround_ = std::make_unique<Engine::Graphics3D::Object3D>();
	farGround_->Initialize(Engine::Graphics3D::Object3DCommon::GetInstance());
	GameModelCache::ApplyToObject(*farGround_, planeHandle);
	farGround_->SetSkyboxFilePath(kEnvironmentTexturePath);
	farGround_->SetEnvironmentReflectionStrength(0.0f);
	farGround_->SetEnvironmentRoughness(1.0f);
	farGround_->SetTextureInfluence(0.0f);
	farGround_->SetCastsShadow(false);
	farGround_->SetFrustumCullingEnabled(false);
	farGround_->SetScale({ kFarGroundScale, 1.0f, kFarGroundScale });
	farGround_->SetTranslate({ 0.0f, kGroundY - 0.035f, 0.0f });
	farGround_->SetColor({ 0.22f, 0.39f, 0.17f, 1.0f });

	for (std::unique_ptr<Engine::Graphics3D::Object3D>& tile : tiles_) {
		tile = std::make_unique<Engine::Graphics3D::Object3D>();
		tile->Initialize(Engine::Graphics3D::Object3DCommon::GetInstance());
		GameModelCache::ApplyToObject(*tile, planeHandle);
		tile->SetSkyboxFilePath(kEnvironmentTexturePath);
		tile->SetEnvironmentReflectionStrength(0.0f);
		tile->SetEnvironmentRoughness(1.0f);
		tile->SetTextureInfluence(0.0f);
		tile->SetCastsShadow(false);
		tile->SetScale({ kGroundScale, 1.0f, kGroundScale });
		tile->SetTranslate({ 0.0f, kGroundY, 0.0f });
		tile->SetColor({ 0.24f, 0.40f, 0.18f, 1.0f });
	}

	for (std::unique_ptr<Engine::Graphics3D::Object3D>& landmark : landmarks_) {
		landmark = std::make_unique<Engine::Graphics3D::Object3D>();
		landmark->Initialize(Engine::Graphics3D::Object3DCommon::GetInstance());
		GameModelCache::ApplyToObject(*landmark, GameModelCache::Load(kLandmarkModels[0]));
		landmark->SetSkyboxFilePath(kEnvironmentTexturePath);
		landmark->SetEnvironmentReflectionStrength(0.0f);
		landmark->SetEnvironmentRoughness(1.0f);
		landmark->SetTextureInfluence(1.0f);
		landmark->SetCastsShadow(true);
	}
	landmarkModelKinds_.fill(-1);
	landmarkVisible_.fill(false);
	landmarkShadowVisible_.fill(false);
}

void GridPlane::Update(const Vector3& focusPosition)
{
	animationTime_ += 1.0f / 60.0f;
	const float centerX = SnapToTile(focusPosition.x);
	const float centerZ = SnapToTile(focusPosition.z);
	const int centerTileX = static_cast<int>(std::floor(focusPosition.x / kTileSpan));
	const int centerTileZ = static_cast<int>(std::floor(focusPosition.z / kTileSpan));

	if (farGround_) {
		farGround_->SetTranslate({
			focusPosition.x,
			kGroundY - 0.035f,
			focusPosition.z,
			});
		farGround_->SetColor(GroundTileColor(centerTileX, centerTileZ, 0.0f));
		farGround_->Update();
	}

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
			const int tileX = centerTileX + x;
			const int tileZ = centerTileZ + z;
			const float centerDistance = std::sqrt(static_cast<float>(x * x + z * z));
			tiles_[index]->SetColor(GroundTileColor(tileX, tileZ, centerDistance));
			tiles_[index]->Update();
			++index;
		}
	}

	index = 0;
	visibleLandmarkCount_ = 0;
	shadowLandmarkCount_ = 0;
	for (int z = -kLandmarkRadius; z <= kLandmarkRadius; ++z) {
		for (int x = -kLandmarkRadius; x <= kLandmarkRadius; ++x) {
			if (!landmarks_[index]) {
				++index;
				continue;
			}

			const int landmarkX = centerTileX + x;
			const int landmarkZ = centerTileZ + z;
			const uint32_t hash = HashCell(landmarkX, landmarkZ);
			const float offsetX = (Hash01(hash, 0) - 0.5f) * 9.0f;
			const float offsetZ = (Hash01(hash, 8) - 0.5f) * 9.0f;
			const LandmarkKind kind = SelectLandmarkKind(hash);
			const bool visible = ShouldShowLandmark(kind, x, z, hash);
			const bool shadowVisible = visible && ShouldCastLandmarkShadow(kind, x, z);
			landmarkVisible_[index] = visible;
			landmarkShadowVisible_[index] = shadowVisible;
			if (visible) {
				++visibleLandmarkCount_;
			}
			if (shadowVisible) {
				++shadowLandmarkCount_;
			}
			if (!visible) {
				++index;
				continue;
			}
			const int kindIndex = static_cast<int>(kind);
			if (landmarkModelKinds_[index] != kindIndex) {
				GameModelCache::ApplyToObject(
					*landmarks_[index],
					GameModelCache::Load(kLandmarkModels[static_cast<size_t>(kind)]));
				landmarkModelKinds_[index] = kindIndex;
			}
			const Vector3 scale = LandmarkScale(kind, hash);

			landmarks_[index]->SetScale(scale);
			landmarks_[index]->SetRotate({
				0.0f,
				Hash01(hash, 12) * 6.28318531f,
				0.0f,
				});
			landmarks_[index]->SetTranslate({
				static_cast<float>(landmarkX) * kLandmarkSpacing + offsetX,
				kGroundY + kLandmarkYOffset,
				static_cast<float>(landmarkZ) * kLandmarkSpacing + offsetZ,
				});
			landmarks_[index]->SetColor(LandmarkColor(hash));
			landmarks_[index]->Update();
			++index;
		}
	}
}

void GridPlane::Draw()
{
	if (farGround_) {
		Engine::Graphics3D::Object3D::SubmitForDraw(farGround_.get());
	}
	for (const std::unique_ptr<Engine::Graphics3D::Object3D>& tile : tiles_) {
		if (tile) {
			Engine::Graphics3D::Object3D::SubmitForDraw(tile.get());
		}
	}
	for (const std::unique_ptr<Engine::Graphics3D::Object3D>& landmark : landmarks_) {
		const size_t landmarkIndex = &landmark - landmarks_.data();
		if (landmark && landmarkVisible_[landmarkIndex]) {
			Engine::Graphics3D::Object3D::SubmitForDraw(landmark.get());
		}
	}
}

void GridPlane::DrawShadow()
{
	for (const std::unique_ptr<Engine::Graphics3D::Object3D>& tile : tiles_) {
		if (tile) {
			Engine::Graphics3D::Object3D::SubmitForShadow(tile.get());
		}
	}
	for (const std::unique_ptr<Engine::Graphics3D::Object3D>& landmark : landmarks_) {
		const size_t landmarkIndex = &landmark - landmarks_.data();
		if (landmark && landmarkShadowVisible_[landmarkIndex]) {
			Engine::Graphics3D::Object3D::SubmitForShadow(landmark.get());
		}
	}
}

float GridPlane::SnapToTile(float value)
{
	return std::floor(value / kTileSpan) * kTileSpan;
}

} // namespace DirectXGame
