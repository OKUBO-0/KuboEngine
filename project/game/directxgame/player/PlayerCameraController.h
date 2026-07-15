#pragma once

#include "Vector3.h"

namespace Engine::CameraSystem {
class Camera;
}

namespace DirectXGame {

class PlayerCameraController final {
public:
	struct CameraPose {
		Vector3 position{};
		Vector3 rotation{};
	};

	enum class Mode {
		WorldBack,
		PlayerBack,
		WorldFront,
		TopDown,
		Megabonk,
	};

	void Update(
		Engine::CameraSystem::Camera* camera,
		const Vector3& playerPosition,
		float playerRotationY,
		bool advanceFollow);
	CameraPose CalculatePose(
		const Vector3& focusPosition,
		float playerRotationY) const;
	CameraPose CalculatePoseFacingTarget(
		const Vector3& playerPosition,
		const Vector3& targetPosition) const;
	void ResetFocus(const Vector3& playerPosition);
	void SyncToTarget(
		const Vector3& playerPosition,
		const Vector3& targetPosition);
	void RequestShake(float duration, float strength);

	float GetHeight() const { return height_; }
	float GetDistance() const { return distance_; }
	float GetPitch() const { return pitch_; }
	float GetYaw() const { return yaw_; }
	float GetLookSmoothing() const { return lookSmoothing_; }
	float GetFollowSmoothness() const { return followSmoothness_; }
	Mode GetMode() const { return mode_; }
	bool UsesCameraRelativeMovement() const { return mode_ == Mode::Megabonk; }
	bool UsesMouseLook() const { return mode_ == Mode::Megabonk; }

	void SetHeight(float height);
	void SetDistance(float distance);
	void SetPitch(float pitch) { pitch_ = pitch; targetPitch_ = pitch; }
	void SetYaw(float yaw) { yaw_ = yaw; targetYaw_ = yaw; }
	void SetLookSmoothing(float smoothing) { lookSmoothing_ = smoothing; }
	void SetFollowSmoothness(float smoothness) { followSmoothness_ = smoothness; }
	void SetMode(Mode mode) { mode_ = mode; }

private:
	float height_ = 70.0f;
	float distance_ = 90.0f;
	float pitch_ = 0.52f;
	float targetPitch_ = 0.52f;
	float yaw_ = 0.0f;
	float targetYaw_ = 0.0f;
	float lookSmoothing_ = 0.32f;
	float followSmoothness_ = 8.0f;
	float combatDistance_ = 90.0f;
	float combatHeight_ = 70.0f;
	Vector3 focusPosition_{ 0.0f, 0.0f, 0.0f };
	bool followInitialized_ = false;
	Mode mode_ = Mode::Megabonk;
	float shakeTimer_ = 0.0f;
	float shakeDuration_ = 0.0f;
	float shakeStrength_ = 0.0f;
	float shakeCooldownTimer_ = 0.0f;
	float shakePhase_ = 0.0f;
};

} // namespace DirectXGame
