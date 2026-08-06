#pragma once

#include "Vector2.h"
#include "Vector3.h"

namespace DirectXGame {

class PlayerMovementController final {
public:
	void Update(
		float deltaTime,
		Vector3& position,
		float& rotationY,
		float moveSpeedPerSecond,
		float cameraYaw,
		bool cameraRelativeMovement);
	bool IsDodging() const { return false; }
	bool IsDashing() const { return dashing_; }
	bool IsJumping() const { return jumpTimer_ > 0.0f; }
	float GetDodgeCooldownRatio() const;
	void ResetActionState();
	void SuppressNextDodgeTrigger()
	{
		suppressNextDodgeTrigger_ = true;
	}

private:
	void UpdateDashAndJump(
		float deltaTime,
		const Vector2& moveInput,
		float& rotationY,
		float cameraYaw,
		bool cameraRelativeMovement);
	Vector3 ResolveMoveDirection(
		const Vector2& moveInput,
		float cameraYaw,
		bool cameraRelativeMovement) const;

	Vector3 dodgeDirection_{ 0.0f, 0.0f, 1.0f };
	float dodgeTimer_ = 0.0f;
	float dodgeCooldownTimer_ = 0.0f;
	float jumpTimer_ = 0.0f;
	bool dashing_ = false;
	bool suppressNextDodgeTrigger_ = false;

	static constexpr float kDodgeDuration = 0.20f;
	static constexpr float kDodgeCooldown = 0.85f;
	static constexpr float kDodgeSpeed = 34.0f;
	static constexpr float kJumpDuration = 0.72f;
	static constexpr float kJumpHeight = 3.0f;
};

}
