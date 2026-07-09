#include "PlayerMovementController.h"

#include "Input.h"
#include "GameInputBindings.h"
#include <algorithm>
#include <cmath>

namespace DirectXGame {

void PlayerMovementController::Update(
	float deltaTime,
	Vector3& position,
	float& rotationY,
	float moveSpeedPerSecond,
	float cameraYaw,
	bool cameraRelativeMovement)
{
	Engine::InputSystem::Input* input =
		Engine::InputSystem::Input::GetInstance();
	const Vector2 moveInput =
		GameInputBindings::GetMoveVector(input);
	UpdateDodge(deltaTime, moveInput, rotationY, cameraYaw, cameraRelativeMovement);
	if (IsDodging()) {
		position.x +=
			dodgeDirection_.x * kDodgeSpeed * deltaTime;
		position.z +=
			dodgeDirection_.z * kDodgeSpeed * deltaTime;
		return;
	}

	const Vector3 moveDirection =
		ResolveMoveDirection(moveInput, cameraYaw, cameraRelativeMovement);
	const float moveDistance = moveSpeedPerSecond * deltaTime;
	position.x += moveDirection.x * moveDistance;
	position.z += moveDirection.z * moveDistance;
	if (cameraRelativeMovement &&
		std::abs(moveInput.x) + std::abs(moveInput.y) > 0.001f) {
		rotationY = std::atan2(moveDirection.x, moveDirection.z);
	}
}

void PlayerMovementController::UpdateDodge(
	float deltaTime,
	const Vector2& moveInput,
	float& rotationY,
	float cameraYaw,
	bool cameraRelativeMovement)
{
	dodgeCooldownTimer_ = (std::max)(
		0.0f,
		dodgeCooldownTimer_ - deltaTime);
	dodgeTimer_ = (std::max)(0.0f, dodgeTimer_ - deltaTime);

	Engine::InputSystem::Input* input =
		Engine::InputSystem::Input::GetInstance();
	const bool dodgeTriggered =
		input &&
		!GameInputBindings::IsGameInputSuppressedByImGui() &&
		(input->TriggerKey(DIK_SPACE) ||
			input->TriggerGamePadButton(XINPUT_GAMEPAD_B));
	if (suppressNextDodgeTrigger_) {
		suppressNextDodgeTrigger_ = false;
		return;
	}
	if (!dodgeTriggered || dodgeCooldownTimer_ > 0.0f) {
		return;
	}

	Vector3 direction =
		ResolveMoveDirection(moveInput, cameraYaw, cameraRelativeMovement);
	const float length = std::sqrt(
		direction.x * direction.x +
		direction.z * direction.z);
	if (length <= 0.001f) {
		direction = {
			std::sin(rotationY),
			0.0f,
			std::cos(rotationY),
		};
	}
	dodgeDirection_ = direction;
	dodgeTimer_ = kDodgeDuration;
	dodgeCooldownTimer_ = kDodgeCooldown;
	rotationY = std::atan2(direction.x, direction.z);
}

Vector3 PlayerMovementController::ResolveMoveDirection(
	const Vector2& moveInput,
	float cameraYaw,
	bool cameraRelativeMovement) const
{
	Vector3 direction{ moveInput.x, 0.0f, moveInput.y };
	if (cameraRelativeMovement) {
		const Vector3 cameraForward{
			std::sin(cameraYaw),
			0.0f,
			std::cos(cameraYaw),
		};
		const Vector3 cameraRight{
			cameraForward.z,
			0.0f,
			-cameraForward.x,
		};
		direction =
			cameraRight * moveInput.x +
			cameraForward * moveInput.y;
	}
	const float length = std::sqrt(
		direction.x * direction.x +
		direction.z * direction.z);
	if (length > 0.001f) {
		direction.x /= length;
		direction.z /= length;
		return direction;
	}
	return { 0.0f, 0.0f, 0.0f };
}

float PlayerMovementController::GetDodgeCooldownRatio() const
{
	return std::clamp(
		dodgeCooldownTimer_ / kDodgeCooldown,
		0.0f,
		1.0f);
}

}
