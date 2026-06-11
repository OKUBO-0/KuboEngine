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

	enum class AimInputDevice {
		KeyboardMouse,
		Gamepad,
	};

	void Initialize();
	void Update(float deltaTime);
	void Draw();

	const Vector3& GetWorldPosition() const { return position_; }
	float GetWorldRotationY() const { return rotationY_; }
	float GetCollisionRadius() const;
	Engine::Math::AABB GetCollisionAabb() const;
	Engine::Math::OBB GetCollisionObb() const;
	void SetDebugWorldPosition(const Vector3& position) { position_ = position; ApplyTransforms(); }
	void SetDebugWorldRotationY(float rotationY) { rotationY_ = rotationY; ApplyTransforms(); }
	Engine::CameraSystem::Camera& GetCamera() { return *camera_; }
	float GetMoveSpeed() const { return moveSpeedPerSecond_; }
	void SetMoveSpeed(float moveSpeedPerSecond) { moveSpeedPerSecond_ = moveSpeedPerSecond; }
	float GetCameraHeight() const { return cameraHeight_; }
	float GetCameraDistance() const { return cameraDistance_; }
	float GetCameraPitch() const { return cameraPitch_; }
	float GetCameraFollowSmoothness() const { return cameraFollowSmoothness_; }
	CameraMode GetCameraMode() const { return cameraMode_; }
	bool IsMouseAimEnabled() const { return mouseAimEnabled_; }
	AimInputDevice GetAimInputDevice() const { return aimInputDevice_; }
	void SetCameraHeight(float height)
	{
		cameraHeight_ = height;
		combatCameraHeight_ = height;
		combatCameraTargetHeight_ = height;
	}
	void SetCameraDistance(float distance)
	{
		cameraDistance_ = distance;
		combatCameraDistance_ = distance;
		combatCameraTargetDistance_ = distance;
	}
	void SetCameraPitch(float pitch) { cameraPitch_ = pitch; }
	void SetCameraFollowSmoothness(float smoothness) { cameraFollowSmoothness_ = smoothness; }
	void SetCameraMode(CameraMode mode) { cameraMode_ = mode; }
	void SetMouseAimEnabled(bool enabled) { mouseAimEnabled_ = enabled; }
	void SetLightSettings(const GameLightSettings& lightSettings);
	void SetVisible(bool visible) { visible_ = visible; }
	void StartIntroPresentation();
	void UpdateIntroPresentation(float elapsedTime, float duration);
	void StartDeathPresentation();
	void UpdateDeathPresentation(float elapsedTime, float duration);
	bool IsDodging() const { return dodgeTimer_ > 0.0f; }
	float GetDodgeCooldownRatio() const;
	void RequestCameraShake(float duration, float strength);
	void SetCombatCameraTarget(float distance, float height);
	void SuppressNextDodgeTrigger() { suppressNextDodgeTrigger_ = true; }

private:
	void InitializeCamera();
	void InitializeObjects();
	void UpdateMovement(float deltaTime);
	void UpdateAim(float deltaTime);
	void UpdateAimIndicator();
	void UpdateCamera(bool advanceFollow);
	void ApplyTransforms();
	void ApplyDeathPose(float progress);
	void UpdateDodge(float deltaTime, const Vector2& moveInput);

	Vector3 position_{ 0.0f, 0.0f, 0.0f };
	float rotationY_ = 0.0f;
	bool visible_ = true;
	float moveSpeedPerSecond_ = 30.0f;
	float cameraHeight_ = 80.0f;
	float cameraDistance_ = 45.0f;
	float cameraPitch_ = 1.0f;
	float cameraFollowSmoothness_ = 8.0f;
	float combatCameraDistance_ = 45.0f;
	float combatCameraHeight_ = 80.0f;
	float combatCameraTargetDistance_ = 45.0f;
	float combatCameraTargetHeight_ = 80.0f;
	Vector3 cameraFocusPosition_{ 0.0f, 0.0f, 0.0f };
	bool cameraFollowInitialized_ = false;
	float deathStartCameraHeight_ = 80.0f;
	float deathStartCameraDistance_ = 45.0f;
	float deathStartCameraPitch_ = 1.0f;
	bool deathPresentationActive_ = false;
	bool introPresentationActive_ = false;
	CameraMode cameraMode_ = CameraMode::WorldBack;
	bool mouseAimEnabled_ = true;
	AimInputDevice aimInputDevice_ = AimInputDevice::KeyboardMouse;
	Vector3 aimIndicatorPosition_{ 0.0f, -1.0f, 6.0f };
	bool aimIndicatorTracksMouse_ = false;
	bool suppressNextDodgeTrigger_ = false;
	Vector3 dodgeDirection_{ 0.0f, 0.0f, 1.0f };
	float dodgeTimer_ = 0.0f;
	float dodgeCooldownTimer_ = 0.0f;
	float cameraShakeTimer_ = 0.0f;
	float cameraShakeDuration_ = 0.0f;
	float cameraShakeStrength_ = 0.0f;
	float cameraShakeCooldownTimer_ = 0.0f;
	float cameraShakePhase_ = 0.0f;

	static constexpr float kDodgeDuration = 0.22f;
	static constexpr float kDodgeCooldown = 0.8f;
	static constexpr float kDodgeSpeed = 78.0f;

	std::unique_ptr<Engine::CameraSystem::Camera> camera_;
	std::unique_ptr<Engine::Graphics3D::Object3D> playerObject_;
	std::unique_ptr<Engine::Graphics3D::Object3D> aimIndicatorObject_;
	GameLightSettings lightSettings_{};
};

} // namespace DirectXGame
