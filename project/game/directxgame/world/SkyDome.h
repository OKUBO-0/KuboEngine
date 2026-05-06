#pragma once

#include "Object3D.h"
#include "game/directxgame/core/GameLightSettings.h"
#include <memory>

namespace DirectXGame {

class SkyDome {
public:
	void Initialize();
	void Update();
	void Draw();
	void SetLightSettings(const GameLightSettings& lightSettings);

private:
	std::unique_ptr<Engine::Graphics3D::Object3D> skyObject_;
	GameLightSettings lightSettings_{};
	float animationTime_ = 0.0f;
};

} // namespace DirectXGame
