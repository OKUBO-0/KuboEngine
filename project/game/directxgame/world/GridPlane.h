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
	size_t GetVisibleLandmarkCount() const { return visibleLandmarkCount_; }
	size_t GetShadowLandmarkCount() const { return shadowLandmarkCount_; }

private:
	static float SnapToTile(float value);

	static constexpr int kTileRadius = 6;
	static constexpr int kTileCountPerAxis = kTileRadius * 2 + 1;
	static constexpr int kLandmarkRadius = 5;
	static constexpr int kLandmarkCountPerAxis = kLandmarkRadius * 2 + 1;
	std::array<std::unique_ptr<Engine::Graphics3D::Object3D>, kTileCountPerAxis * kTileCountPerAxis> tiles_;
	std::array<std::unique_ptr<Engine::Graphics3D::Object3D>, kLandmarkCountPerAxis * kLandmarkCountPerAxis> landmarks_;
	std::unique_ptr<Engine::Graphics3D::Object3D> farGround_;
	std::array<int, kLandmarkCountPerAxis * kLandmarkCountPerAxis> landmarkModelKinds_{};
	std::array<bool, kLandmarkCountPerAxis * kLandmarkCountPerAxis> landmarkVisible_{};
	std::array<bool, kLandmarkCountPerAxis * kLandmarkCountPerAxis> landmarkShadowVisible_{};
	size_t visibleLandmarkCount_ = 0;
	size_t shadowLandmarkCount_ = 0;
	float animationTime_ = 0.0f;

	static constexpr float kGroundScale = 10.0f;
	static constexpr float kFarGroundScale = 540.0f;
	static constexpr float kTileSpan = kGroundScale * 2.0f;
};

} // namespace DirectXGame
