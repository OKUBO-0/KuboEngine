#pragma once

#include "Object3D.h"
#include "Vector3.h"
#include "game/directxgame/core/GameLightSettings.h"
#include <array>
#include <memory>

namespace DirectXGame {

class GridPlane {
public:
	void Initialize();
	void Update(const Vector3& focusPosition);
	void Draw();
	void SetLightSettings(const GameLightSettings& lightSettings);

private:
	static float SnapToTile(float value);

	std::array<std::unique_ptr<Engine::Graphics3D::Object3D>, 9> tiles_;
	GameLightSettings lightSettings_{};
	float animationTime_ = 0.0f;

	static constexpr float kGroundScale = 160.0f;
	static constexpr float kTileSpan = kGroundScale * 2.0f;
};

} // namespace DirectXGame
