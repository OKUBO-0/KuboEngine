#include "EnemyDeathBomb.h"

#include "GameModelCache.h"
#include "Object3D.h"
#include "Object3DCommon.h"
#include "ResourcePaths.h"
#include <algorithm>
#include <cmath>
#include <filesystem>

namespace {

constexpr char kEnvironmentTexturePath[] =
	"Resources/textures/skybox/test.dds";
constexpr char kDeathBombModelPath[] = "quaternius_items/bomb.glb";
constexpr char kDeathBombFallbackModelPath[] = "quaternius_weapons/rock.glb";

const char* ResolveDeathBombModelPath()
{
	const std::filesystem::path fullPath =
		std::filesystem::path(DirectXGame::ResourcePaths::GetModelResourceRoot()) /
		kDeathBombModelPath;
	return std::filesystem::exists(fullPath)
		? kDeathBombModelPath
		: kDeathBombFallbackModelPath;
}

}

namespace DirectXGame {

void EnemyDeathBomb::Initialize(
	const Vector3& position,
	float delay,
	float radius,
	int32_t damage)
{
	position_ = position;
	delay_ = (std::max)(0.1f, delay);
	radius_ = (std::max)(0.5f, radius);
	damage_ = (std::max)(1, damage);
	elapsedTime_ = 0.0f;
	exploded_ = false;

	object_ = std::make_unique<Engine::Graphics3D::Object3D>();
	object_->Initialize(Engine::Graphics3D::Object3DCommon::GetInstance());
	GameModelCache::ApplyToObject(
		*object_,
		GameModelCache::Load(ResolveDeathBombModelPath()));
	object_->SetSkyboxFilePath(kEnvironmentTexturePath);
	object_->SetEnvironmentReflectionStrength(0.0f);
	object_->SetEnvironmentRoughness(1.0f);
	object_->SetCastsShadow(true);
	object_->SetScale({ 1.75f, 1.75f, 1.75f });
	object_->SetTranslate({ position_.x, position_.y + 1.05f, position_.z });
	object_->SetColor({ 1.0f, 0.34f, 0.04f, 0.96f });
	object_->Update();
}

bool EnemyDeathBomb::Update(float deltaTime)
{
	if (exploded_) {
		return false;
	}

	elapsedTime_ += (std::max)(0.0f, deltaTime);
	const float progress = std::clamp(elapsedTime_ / delay_, 0.0f, 1.0f);
	const float pulse = 0.88f + std::sin(elapsedTime_ * 22.0f) * 0.12f;
	if (object_) {
		const float markerScale = (std::max)(1.45f, radius_ * (0.30f + progress * 0.10f)) * pulse;
		object_->SetScale({ markerScale, markerScale, markerScale });
		object_->SetRotate({ 0.0f, elapsedTime_ * 4.0f, 0.0f });
		object_->SetTranslate({ position_.x, position_.y + 1.05f, position_.z });
		object_->SetColor({ 1.0f, 0.34f - progress * 0.18f, 0.03f, 0.95f });
		object_->Update();
	}

	if (elapsedTime_ < delay_) {
		return false;
	}
	exploded_ = true;
	return true;
}

void EnemyDeathBomb::Draw() const
{
	if (object_ && !exploded_) {
		Engine::Graphics3D::Object3D::SubmitForDraw(object_.get());
	}
}

} // namespace DirectXGame
