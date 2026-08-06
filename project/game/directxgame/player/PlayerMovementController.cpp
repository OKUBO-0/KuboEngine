#include "PlayerMovementController.h"

#include "GameAudioCache.h"
#include "Input.h"
#include "GameInputBindings.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace DirectXGame {
namespace {

constexpr char kDodgeSePath[] = "se/player_dodge.wav";
constexpr char kAudioPlayerDodge[] = "player.dodge";

}

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
	UpdateDashAndJump(deltaTime, moveInput, rotationY, cameraYaw, cameraRelativeMovement);

	Vector3 moveDirection =
		ResolveMoveDirection(moveInput, cameraYaw, cameraRelativeMovement);
	if (std::sqrt(
			moveDirection.x * moveDirection.x +
			moveDirection.z * moveDirection.z) <= 0.001f &&
		GameInputBindings::IsMouseMoveForwardPushed(input)) {
		moveDirection = {
			std::sin(rotationY),
			0.0f,
			std::cos(rotationY),
		};
	}
	const float moveDistance =
		moveSpeedPerSecond * deltaTime;
	position.x += moveDirection.x * moveDistance;
	position.z += moveDirection.z * moveDistance;
	if (dodgeTimer_ > 0.0f) {
		position.x += dodgeDirection_.x * kDodgeSpeed * deltaTime;
		position.z += dodgeDirection_.z * kDodgeSpeed * deltaTime;
	}
	if (jumpTimer_ > 0.0f) {
		const float progress = std::clamp(jumpTimer_ / kJumpDuration, 0.0f, 1.0f);
		position.y = std::sin(progress * std::numbers::pi_v<float>) * kJumpHeight;
	} else {
		position.y = 0.0f;
	}
	if (cameraRelativeMovement &&
		std::abs(moveInput.x) + std::abs(moveInput.y) > 0.001f) {
		rotationY = std::atan2(moveDirection.x, moveDirection.z);
	}
}

void PlayerMovementController::UpdateDashAndJump(
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
	jumpTimer_ = (std::max)(0.0f, jumpTimer_ - deltaTime);

	Engine::InputSystem::Input* input =
		Engine::InputSystem::Input::GetInstance();
	dashing_ = dodgeTimer_ > 0.0f;
	if (suppressNextDodgeTrigger_) {
		suppressNextDodgeTrigger_ = false;
		return;
	}
	Vector3 direction =
		ResolveMoveDirection(moveInput, cameraYaw, cameraRelativeMovement);
	const float length = std::sqrt(
		direction.x * direction.x +
		direction.z * direction.z);
	if (length > 0.001f) {
		rotationY = std::atan2(direction.x, direction.z);
	}
	if (GameInputBindings::IsDashTriggered(input) &&
		dodgeCooldownTimer_ <= 0.0f) {
		if (length > 0.001f) {
			dodgeDirection_ = direction;
		} else {
			dodgeDirection_ = {
				std::sin(rotationY),
				0.0f,
				std::cos(rotationY),
			};
		}
		dodgeTimer_ = kDodgeDuration;
		dodgeCooldownTimer_ = kDodgeCooldown;
		dashing_ = true;
		static SoundHandle dodgeSeHandle = GameAudioCache::LoadWave(kDodgeSePath);
		GameAudioCache::PlayTuned(dodgeSeHandle, kAudioPlayerDodge, 0.42f, 0.10f);
	}
	if (GameInputBindings::IsJumpTriggered(input) && jumpTimer_ <= 0.0f) {
		jumpTimer_ = kJumpDuration;
		static SoundHandle dodgeSeHandle = GameAudioCache::LoadWave(kDodgeSePath);
		GameAudioCache::PlayTuned(dodgeSeHandle, kAudioPlayerDodge, 0.34f, 0.12f);
	}
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

void PlayerMovementController::ResetActionState()
{
	dodgeTimer_ = 0.0f;
	jumpTimer_ = 0.0f;
	dashing_ = false;
}

}
