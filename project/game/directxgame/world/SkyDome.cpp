#include "game/directxgame/world/SkyDome.h"
#include "game/directxgame/core/GameModelCache.h"
#include "Object3DCommon.h"
#include "TextureManager.h"
#include <cmath>

namespace {

constexpr char kEnvironmentTexturePath[] = "Resources/textures/skybox/test.dds";

}

namespace DirectXGame {

void SkyDome::Initialize()
{
	Engine::Base::TextureManager::GetInstance()->LoadTexture(kEnvironmentTexturePath);

	const ModelHandle skydomeHandle = GameModelCache::Load("skydome.obj");
	skyObject_ = std::make_unique<Engine::Graphics3D::Object3D>();
	skyObject_->Initialize(Engine::Graphics3D::Object3DCommon::GetInstance());
	GameModelCache::ApplyToObject(*skyObject_, skydomeHandle);
	skyObject_->SetSkyboxFilePath(kEnvironmentTexturePath);
	skyObject_->SetEnvironmentReflectionStrength(0.0f);
	skyObject_->SetEnvironmentRoughness(1.0f);
	skyObject_->SetLighting(false);
	skyObject_->SetScale({ 100.0f, 100.0f, 100.0f });
	skyObject_->SetTranslate({ 0.0f, 0.0f, 0.0f });
	skyObject_->SetColor({ 0.62f, 0.76f, 1.0f, 1.0f });
	lightSettings_.ApplyTo(*skyObject_);
}

void SkyDome::Update()
{
	if (skyObject_) {
		animationTime_ += 1.0f / 60.0f;
		const float pulse = 0.5f + 0.5f * std::sin(animationTime_ * 0.35f);
		skyObject_->SetRotate({ 0.0f, animationTime_ * 0.015f, 0.0f });
		skyObject_->SetColor({ 0.55f + pulse * 0.08f, 0.68f + pulse * 0.08f, 0.95f, 1.0f });
		skyObject_->Update();
	}
}

void SkyDome::Draw()
{
	if (skyObject_) {
		skyObject_->Draw();
	}
}

void SkyDome::SetLightSettings(const GameLightSettings& lightSettings)
{
	lightSettings_ = lightSettings;
	if (skyObject_) {
		lightSettings_.ApplyTo(*skyObject_);
		skyObject_->SetLighting(false);
	}
}

} // namespace DirectXGame
