#include "game/directxgame/core/GameLightSettings.h"
#include "Object3D.h"
#include <cmath>
#include <numbers>

namespace DirectXGame {

GameLightSettings::GameLightSettings()
{
	directionalLight_.color = { 1.0f, 1.0f, 1.0f, 1.0f };
	directionalLight_.direction = { 0.0f, -1.0f, 1.0f };
	directionalLight_.intensity = 1.0f;
	directionalLight_.enable = true;

	pointLight_.color = { 1.0f, 1.0f, 1.0f, 1.0f };
	pointLight_.position = { 0.0f, 0.0f, 0.0f };
	pointLight_.intensity = 1.0f;
	pointLight_.radius = 10.0f;
	pointLight_.decay = 1.0f;
	pointLight_.enable = false;

	spotLight_.color = { 1.0f, 1.0f, 1.0f, 1.0f };
	spotLight_.position = { 0.0f, 2.0f, 0.0f };
	spotLight_.direction = { 0.0f, -1.0f, 0.0f };
	spotLight_.intensity = 4.0f;
	spotLight_.distance = 7.0f;
	spotLight_.decay = 2.0f;
	spotLight_.coneAngleCos = std::cos(std::numbers::pi_v<float> / 3.0f);
	spotLight_.cosFalloffStart = 1.0f;
	spotLight_.enable = false;
}

void GameLightSettings::ApplyTo(Engine::Graphics3D::Object3D& object) const
{
	object.SetLighting(lightingEnabled_);
	object.SetDirectionalLight(directionalLight_);
	object.SetPointLight(pointLight_);
	object.SetSpotLight(spotLight_);
}

}
