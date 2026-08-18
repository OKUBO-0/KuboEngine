#pragma once

#include "Camera.h"
#include "GameSession.h"
#include "Vector3.h"
#include "PlayerAimController.h"
#include "PlayerCameraController.h"
#include "PlayerMovementController.h"
#include "PlayerPresentationController.h"
#include "PlayerView.h"
#include <memory>

namespace DirectXGame {

class Player {
public:
	using CameraMode = PlayerCameraController::Mode;
	using AimInputDevice = PlayerAimController::InputDevice;

	void Initialize();
	void SetCharacterId(CharacterId characterId);
	void Update(float deltaTime);
	void Draw();
	void DrawShadow();

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
	float GetCameraHeight() const { return cameraController_.GetHeight(); }
	float GetCameraDistance() const { return cameraController_.GetDistance(); }
	float GetCameraPitch() const { return cameraController_.GetPitch(); }
	float GetCameraYaw() const { return cameraController_.GetYaw(); }
	float GetCameraLookSmoothing() const { return cameraController_.GetLookSmoothing(); }
	float GetCameraFollowSmoothness() const { return cameraController_.GetFollowSmoothness(); }
	CameraMode GetCameraMode() const { return cameraController_.GetMode(); }
	PlayerCameraController::CameraPose CalculateCameraPoseFacingTarget(
		const Vector3& targetPosition) const
	{
		return cameraController_.CalculatePoseFacingTarget(
			position_,
			targetPosition);
	}
	bool IsMouseAimEnabled() const { return aimController_.IsMouseAimEnabled(); }
	AimInputDevice GetAimInputDevice() const { return aimController_.GetInputDevice(); }
	void SetCameraHeight(float height) { cameraController_.SetHeight(height); }
	void SetCameraDistance(float distance) { cameraController_.SetDistance(distance); }
	void SetCameraPitch(float pitch) { cameraController_.SetPitch(pitch); }
	void SetCameraLookSmoothing(float smoothing) { cameraController_.SetLookSmoothing(smoothing); }
	void SetCameraFollowSmoothness(float smoothness) { cameraController_.SetFollowSmoothness(smoothness); }
	void SetCameraMode(CameraMode mode) { cameraController_.SetMode(mode); }
	void SetMouseAimEnabled(bool enabled) { aimController_.SetMouseAimEnabled(enabled); }
	void SyncCameraToTarget(const Vector3& targetPosition)
	{
		cameraController_.SyncToTarget(position_, targetPosition);
	}
	void SetVisible(bool visible) { visible_ = visible; }
	void StartIntroPresentation();
	void UpdateIntroPresentation(float elapsedTime, float duration);
	void BeginCinematicPresentation();
	void UpdateCinematicPresentation();
	void StartDeathPresentation();
	void UpdateDeathPresentation(float elapsedTime, float duration);
	void NotifyHitReact();
	bool IsDodging() const { return movementController_.IsDodging(); }
	bool IsDashing() const { return movementController_.IsDashing(); }
	bool IsJumping() const { return movementController_.IsJumping(); }
	float GetDodgeCooldownRatio() const;
	void RequestCameraShake(float duration, float strength);
	void SuppressNextDodgeTrigger()
	{
		movementController_.SuppressNextDodgeTrigger();
	}

private:
	void InitializeCamera();
	void ApplyTransforms();

	Vector3 position_{ 0.0f, 0.0f, 0.0f };
	Vector3 cinematicPosition_{ 0.0f, 0.0f, 0.0f };
	float rotationY_ = 0.0f;
	bool visible_ = true;
	bool visualMoving_ = false;
	float visualMovingHoldTimer_ = 0.0f;
	float moveSpeedPerSecond_ = 30.0f;
	PlayerAimController aimController_{};
	PlayerCameraController cameraController_{};
	PlayerMovementController movementController_{};
	PlayerPresentationController presentationController_{};
	PlayerView view_{};

	std::unique_ptr<Engine::CameraSystem::Camera> camera_;
};

} // namespace DirectXGame
