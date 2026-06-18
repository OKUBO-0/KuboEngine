#include "game/directxgame/player/PlayerMovementController.h"

#include "Input.h"
#include "game/directxgame/core/GameInputBindings.h"
#include <algorithm>
#include <cmath>

namespace DirectXGame {

void PlayerMovementController::Update(
	float deltaTime,
	Vector3& position,
	float& rotationY,
	float moveSpeedPerSecond)
{
	Engine::InputSystem::Input* input =
		Engine::InputSystem::Input::GetInstance();
	const Vector2 moveInput =
		GameInputBindings::GetMoveVector(input);
	UpdateDodge(deltaTime, moveInput, rotationY);
	if (IsDodging()) {
		position.x +=
			dodgeDirection_.x * kDodgeSpeed * deltaTime;
		position.z +=
			dodgeDirection_.z * kDodgeSpeed * deltaTime;
		return;
	}

	const float moveDistance = moveSpeedPerSecond * deltaTime;
	position.x += moveInput.x * moveDistance;
	position.z += moveInput.y * moveDistance;
}

void PlayerMovementController::UpdateDodge(
	float deltaTime,
	const Vector2& moveInput,
	float& rotationY)
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

	Vector3 direction{ moveInput.x, 0.0f, moveInput.y };
	const float length = std::sqrt(
		direction.x * direction.x +
		direction.z * direction.z);
	if (length > 0.001f) {
		direction.x /= length;
		direction.z /= length;
	} else {
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

float PlayerMovementController::GetDodgeCooldownRatio() const
{
	return std::clamp(
		dodgeCooldownTimer_ / kDodgeCooldown,
		0.0f,
		1.0f);
}

}
