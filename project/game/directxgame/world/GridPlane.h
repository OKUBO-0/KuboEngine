#pragma once

#include "Object3D.h"
#include "Vector3.h"
#include <array>
#include <memory>

namespace DirectXGame {

class GridPlane {
public:
	void Initialize();
	void Update(const Vector3& focusPosition);
	void Draw();
	void DrawShadow();

private:
	static float SnapToTile(float value);

	static constexpr int kTileRadius = 6;
	static constexpr int kTileCountPerAxis = kTileRadius * 2 + 1;
	std::array<std::unique_ptr<Engine::Graphics3D::Object3D>, kTileCountPerAxis * kTileCountPerAxis> tiles_;
	float animationTime_ = 0.0f;

	static constexpr float kGroundScale = 10.0f;
	static constexpr float kTileSpan = kGroundScale * 2.0f;
};

} // namespace DirectXGame
