#pragma once

#include "Camera.h"
#include "Object3D.h"
#include "Vector3.h"
#include "game/directxgame/core/GameLightSettings.h"
#include <memory>

namespace DirectXGame {

class Player {
public:
	enum class CameraMode {
		WorldBack,
		PlayerBack,
		WorldFront,
		TopDown,
	};

	void Initialize();
	void Update(float deltaTime);
	void Draw();

	const Vector3& GetWorldPosition() const { return position_; }
	float GetWorldRotationY() const { return rotationY_; }
	Engine::CameraSystem::Camera& GetCamera() { return *camera_; }
	float GetMoveSpeed() const { return moveSpeedPerSecond_; }
	void SetMoveSpeed(float moveSpeedPerSecond) { moveSpeedPerSecond_ = moveSpeedPerSecond; }
	float GetCameraHeight() const { return cameraHeight_; }
	float GetCameraDistance() const { return cameraDistance_; }
	float GetCameraPitch() const { return cameraPitch_; }
	CameraMode GetCameraMode() const { return cameraMode_; }
	bool IsMouseAimEnabled() const { return mouseAimEnabled_; }
	void SetCameraHeight(float height) { cameraHeight_ = height; }
	void SetCameraDistance(float distance) { cameraDistance_ = distance; }
	void SetCameraPitch(float pitch) { cameraPitch_ = pitch; }
	void SetCameraMode(CameraMode mode) { cameraMode_ = mode; }
	void SetMouseAimEnabled(bool enabled) { mouseAimEnabled_ = enabled; }
	void SetLightSettings(const GameLightSettings& lightSettings);
	void SetVisible(bool visible) { visible_ = visible; }

private:
	void InitializeCamera();
	void InitializeObjects();
	void UpdateMovement(float deltaTime);
	void UpdateAim(float deltaTime);
	void UpdateAimIndicator();
	void UpdateCamera();
	void ApplyTransforms();

	Vector3 position_{ 0.0f, 0.0f, 0.0f };
	float rotationY_ = 0.0f;
	bool visible_ = true;
	float moveSpeedPerSecond_ = 30.0f;
	float cameraHeight_ = 80.0f;
	float cameraDistance_ = 45.0f;
	float cameraPitch_ = 1.0f;
	CameraMode cameraMode_ = CameraMode::WorldBack;
	bool mouseAimEnabled_ = true;

	std::unique_ptr<Engine::CameraSystem::Camera> camera_;
	std::unique_ptr<Engine::Graphics3D::Object3D> playerObject_;
	std::unique_ptr<Engine::Graphics3D::Object3D> aimIndicatorObject_;
	GameLightSettings lightSettings_{};
};

} // namespace DirectXGame
