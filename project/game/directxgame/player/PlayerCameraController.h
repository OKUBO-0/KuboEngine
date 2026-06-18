#pragma once

#include "Vector3.h"

namespace Engine::CameraSystem {
class Camera;
}

namespace DirectXGame {

class PlayerCameraController final {
public:
	enum class Mode {
		WorldBack,
		PlayerBack,
		WorldFront,
		TopDown,
	};

	void Update(
		Engine::CameraSystem::Camera* camera,
		const Vector3& playerPosition,
		float playerRotationY,
		bool advanceFollow);
	void ResetFocus(const Vector3& playerPosition);
	void RequestShake(float duration, float strength);
	void SetCombatTarget(float distance, float height);

	float GetHeight() const { return height_; }
	float GetDistance() const { return distance_; }
	float GetPitch() const { return pitch_; }
	float GetFollowSmoothness() const { return followSmoothness_; }
	Mode GetMode() const { return mode_; }

	void SetHeight(float height);
	void SetDistance(float distance);
	void SetPitch(float pitch) { pitch_ = pitch; }
	void SetFollowSmoothness(float smoothness) { followSmoothness_ = smoothness; }
	void SetMode(Mode mode) { mode_ = mode; }

private:
	float height_ = 80.0f;
	float distance_ = 45.0f;
	float pitch_ = 1.0f;
	float followSmoothness_ = 8.0f;
	float combatDistance_ = 45.0f;
	float combatHeight_ = 80.0f;
	float combatTargetDistance_ = 45.0f;
	float combatTargetHeight_ = 80.0f;
	Vector3 focusPosition_{ 0.0f, 0.0f, 0.0f };
	bool followInitialized_ = false;
	Mode mode_ = Mode::WorldBack;
	float shakeTimer_ = 0.0f;
	float shakeDuration_ = 0.0f;
	float shakeStrength_ = 0.0f;
	float shakeCooldownTimer_ = 0.0f;
	float shakePhase_ = 0.0f;
};

} // namespace DirectXGame
