#include "game/directxgame/enemy/ExpOrb.h"
#include "game/directxgame/core/GameAudioCache.h"
#include "game/directxgame/core/GameModelCache.h"
#include "Object3DCommon.h"
#include "TextureManager.h"
#include <algorithm>
#include <cmath>
#include <random>

namespace {

constexpr char kEnvironmentTexturePath[] = "Resources/textures/skybox/test.dds";
constexpr char kPickupSePath[] = "audio/se/se_exp.wav";
constexpr char kAudioExpPickup[] = "combat.expPickup";

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

	Engine::Base::TextureManager::GetInstance()->LoadTexture(kEnvironmentTexturePath);
	const ModelHandle modelHandle = GameModelCache::Load("ExpOrb.obj");
	object_ = std::make_unique<Engine::Graphics3D::Object3D>();
	object_->Initialize(Engine::Graphics3D::Object3DCommon::GetInstance());
	GameModelCache::ApplyToObject(*object_, modelHandle);
	object_->SetSkyboxFilePath(kEnvironmentTexturePath);
	object_->SetEnvironmentReflectionStrength(0.15f);
	object_->SetEnvironmentRoughness(0.45f);
	object_->SetColor({ 0.45f, 1.0f, 0.45f, 1.0f });
	lightSettings_.ApplyTo(*object_);
	ApplyTransform();
	object_->Update();
}

void ExpOrb::Update(const Vector3& playerPosition, float deltaTime)
{
	if (!active_) {
		return;
	}

	const float velocityScale = deltaTime / 0.016f;
	const float dx = playerPosition.x - position_.x;
	const float dz = playerPosition.z - position_.z;
	const float distanceSq = dx * dx + dz * dz;

	if (distanceSq < 70.0f) {
		const float distance = std::sqrt(distanceSq);
		if (distance > 0.001f) {
			velocity_.x += (dx / distance) * 0.05f * velocityScale;
			velocity_.z += (dz / distance) * 0.05f * velocityScale;
		}
	}

	position_.x += velocity_.x * velocityScale;
	position_.y += velocity_.y * velocityScale;
	position_.z += velocity_.z * velocityScale;
	position_.y = (std::max)(0.35f, position_.y);
	velocity_.x *= 0.95f;
	velocity_.y *= 0.95f;
	velocity_.z *= 0.95f;
	spin_ += deltaTime * 3.0f;

	if (distanceSq < 4.0f) {
		static SoundHandle sharedPickupSeHandle = 0;
		if (sharedPickupSeHandle == 0) {
			sharedPickupSeHandle = GameAudioCache::LoadWave(kPickupSePath);
		}
		if (sharedPickupSeHandle != 0) {
			GameAudioCache::Play(sharedPickupSeHandle);
			GameAudioCache::SetVolumeFromTuning(sharedPickupSeHandle, kAudioExpPickup, 1.0f);
		}
		active_ = false;
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

void ExpOrb::SetLightSettings(const GameLightSettings& lightSettings)
{
	lightSettings_ = lightSettings;
	if (object_) {
		lightSettings_.ApplyTo(*object_);
	}
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
