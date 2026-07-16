#include "ExpOrb.h"
#include "GameAudioCache.h"
#include "GameModelCache.h"
#include "Object3DCommon.h"
#include <algorithm>
#include <cmath>
#include <random>

namespace {

constexpr char kEnvironmentTexturePath[] = "Resources/textures/skybox/test.dds";
constexpr char kPickupSePath[] = "se/exp_pickup.wav";
constexpr char kAudioExpPickup[] = "combat.expPickup";
constexpr float kFrameDeltaBaseline = 0.016f;
constexpr float kAttractRadiusSq = 70.0f;
constexpr float kCollectRadiusSq = 4.0f;
constexpr float kAttractBaseSpeed = 36.0f;
constexpr float kAttractDistanceSpeed = 4.0f;
constexpr float kHorizontalScatterDamping = 0.95f;
constexpr float kVerticalDamping = 0.95f;

}

namespace DirectXGame {

void ExpOrb::Initialize(const Vector3& position, int32_t expValue)
{
	position_ = position;
	position_.y = 0.7f;
	expValue_ = expValue;
	active_ = true;
	spin_ = 0.0f;

	static std::mt19937 rng{ std::random_device{}() };
	static std::uniform_real_distribution<float> distribution(-0.5f, 0.5f);
	velocity_ = { distribution(rng) * 0.05f, 0.05f, distribution(rng) * 0.05f };

	const ModelHandle modelHandle = GameModelCache::Load("ExpOrb.obj");
	object_ = std::make_unique<Engine::Graphics3D::Object3D>();
	object_->Initialize(Engine::Graphics3D::Object3DCommon::GetInstance());
	GameModelCache::ApplyToObject(*object_, modelHandle);
	object_->SetSkyboxFilePath(kEnvironmentTexturePath);
	object_->SetEnvironmentReflectionStrength(0.15f);
	object_->SetEnvironmentRoughness(0.45f);
	object_->SetColor({ 0.45f, 1.0f, 0.45f, 1.0f });
	ApplyTransform();
	object_->Update();
}

void ExpOrb::Update(
	const Vector3& playerPosition,
	float deltaTime,
	float pickupRangeMultiplier)
{
	if (!active_) {
		return;
	}
	const float rangeMultiplier = (std::max)(1.0f, pickupRangeMultiplier);
	const float attractRadiusSq = kAttractRadiusSq * rangeMultiplier * rangeMultiplier;
	const float collectRadiusSq = kCollectRadiusSq * rangeMultiplier * rangeMultiplier;

	const float velocityScale = deltaTime / kFrameDeltaBaseline;
	const float dx = playerPosition.x - position_.x;
	const float dz = playerPosition.z - position_.z;
	const float distanceSq = dx * dx + dz * dz;

	if (distanceSq < attractRadiusSq) {
		const float distance = std::sqrt(distanceSq);
		if (distance > 0.001f) {
			const float attractSpeed = kAttractBaseSpeed + distance * kAttractDistanceSpeed;
			const float attractStep = (std::min)(distance, attractSpeed * deltaTime);
			position_.x += (dx / distance) * attractStep;
			position_.z += (dz / distance) * attractStep;
		}
		velocity_.x *= kHorizontalScatterDamping;
		velocity_.z *= kHorizontalScatterDamping;
	} else {
		position_.x += velocity_.x * velocityScale;
		position_.z += velocity_.z * velocityScale;
		velocity_.x *= kHorizontalScatterDamping;
		velocity_.z *= kHorizontalScatterDamping;
	}

	position_.y += velocity_.y * velocityScale;
	position_.y = (std::max)(0.35f, position_.y);
	velocity_.y *= kVerticalDamping;
	spin_ += deltaTime * 3.0f;

	const float collectDx = playerPosition.x - position_.x;
	const float collectDz = playerPosition.z - position_.z;
	if (collectDx * collectDx + collectDz * collectDz < collectRadiusSq) {
		Collect();
		return;
	}

	ApplyTransform();
	if (object_) {
		object_->Update();
	}
}

void ExpOrb::Draw()
{
	if (active_ && object_) {
		object_->Draw();
	}
}

void ExpOrb::Collect()
{
	static SoundHandle sharedPickupSeHandle{};
	if (!sharedPickupSeHandle) {
		sharedPickupSeHandle = GameAudioCache::LoadWave(kPickupSePath);
	}
	GameAudioCache::PlayTuned(sharedPickupSeHandle, kAudioExpPickup, 0.45f, 0.025f);
	active_ = false;
}

void ExpOrb::ApplyTransform()
{
	if (!object_) {
		return;
	}
	object_->SetScale({ 0.7f, 0.7f, 0.7f });
	object_->SetRotate({ 0.0f, spin_, 0.0f });
	object_->SetTranslate(position_);
}

} // namespace DirectXGame
