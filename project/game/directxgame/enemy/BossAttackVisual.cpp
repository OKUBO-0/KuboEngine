#include "BossAttackVisual.h"

#include "GameModelCache.h"
#include "Object3D.h"
#include "Object3DCommon.h"
#include <algorithm>
#include <cmath>

namespace {

constexpr char kEnvironmentTexturePath[] =
	"Resources/textures/skybox/test.dds";

void ConfigureObject(Engine::Graphics3D::Object3D& object)
{
	object.Initialize(Engine::Graphics3D::Object3DCommon::GetInstance());
	DirectXGame::GameModelCache::ApplyToObject(
		object,
		DirectXGame::GameModelCache::Load("cube.obj"));
	object.SetSkyboxFilePath(kEnvironmentTexturePath);
	object.SetLighting(false);
	object.SetEnvironmentReflectionStrength(0.0f);
	object.SetEnvironmentRoughness(1.0f);
}

}

namespace DirectXGame {

BossRushTelegraph::BossRushTelegraph() = default;
BossRushTelegraph::~BossRushTelegraph() = default;

void BossRushTelegraph::EnsureInitialized()
{
	if (object_) {
		return;
	}
	object_ = std::make_unique<Engine::Graphics3D::Object3D>();
	ConfigureObject(*object_);
}

void BossRushTelegraph::Update(const BossAttackTelegraph& telegraph)
{
	visible_ = telegraph.type == BossAttackType::Rush;
	if (!visible_) {
		return;
	}
	EnsureInitialized();
	const float range = (std::max)(1.0f, telegraph.range);
	const Vector3 center = telegraph.position + telegraph.direction * (range * 0.5f);
	object_->SetScale({ 1.8f, 0.035f, range * 0.5f });
	object_->SetRotate({ 0.0f, std::atan2(telegraph.direction.x, telegraph.direction.z), 0.0f });
	object_->SetTranslate({ center.x, 0.08f, center.z });
	object_->SetColor({
		1.0f,
		0.0f,
		0.0f,
		0.22f + telegraph.progress * 0.72f,
	});
	object_->Update();
}

void BossRushTelegraph::Draw() const
{
	if (visible_ && object_) {
		object_->Draw();
	}
}

BossSlamCube::BossSlamCube() = default;
BossSlamCube::~BossSlamCube() = default;

void BossSlamCube::Initialize(const Vector3& position, float delay)
{
	position_ = position;
	delay_ = (std::max)(0.0f, delay);
	elapsedTime_ = 0.0f;
	active_ = true;
	object_ = std::make_unique<Engine::Graphics3D::Object3D>();
	ConfigureObject(*object_);
	object_->SetScale({ 1.5f, 3.2f, 1.5f });
	object_->SetColor({ 1.0f, 0.0f, 0.0f, 0.9f });
	object_->SetTranslate({ position_.x, -3.2f, position_.z });
	object_->Update();
}

bool BossSlamCube::Update(float deltaTime)
{
	if (!active_) {
		return false;
	}
	elapsedTime_ += (std::max)(0.0f, deltaTime);
	if (elapsedTime_ < delay_) {
		return true;
	}
	constexpr float kRiseDuration = 0.18f;
	constexpr float kHoldDuration = 0.22f;
	constexpr float kFallDuration = 0.2f;
	const float localTime = elapsedTime_ - delay_;
	if (localTime >= kRiseDuration + kHoldDuration + kFallDuration) {
		active_ = false;
		return false;
	}
	float heightProgress = 1.0f;
	if (localTime < kRiseDuration) {
		heightProgress = localTime / kRiseDuration;
	} else if (localTime > kRiseDuration + kHoldDuration) {
		heightProgress = 1.0f -
			(localTime - kRiseDuration - kHoldDuration) / kFallDuration;
	}
	heightProgress = std::clamp(heightProgress, 0.0f, 1.0f);
	object_->SetTranslate({ position_.x, -3.2f + heightProgress * 4.9f, position_.z });
	object_->Update();
	return true;
}

void BossSlamCube::Draw() const
{
	if (active_ && object_ && elapsedTime_ >= delay_) {
		object_->Draw();
	}
}

} // namespace DirectXGame
