#include "PlayerControlState.h"
#include <cmath>
#include "GamePlayScene.h"
#include "Input.h"

namespace Engine::Scene {

namespace {

constexpr float kMoveVelocity = 0.05f;
constexpr float kScaleStep = 0.01f;
constexpr float kRotateStep = 0.01f;
constexpr float kStickDeadZone = 0.001f;

std::unique_ptr<PlayerControlState> CreateStateFromInput(Engine::InputSystem::Input& input)
{
	const float stickX = input.GetGamePadStickX();
	const float stickY = input.GetGamePadStickY();
	if (std::abs(stickX) > kStickDeadZone || std::abs(stickY) > kStickDeadZone) {
		return std::make_unique<MovePlayerControlState>();
	}

	if (input.GetGamePadTrigger() > 0 || input.GetGamePadTrigger(true) > 0) {
		return std::make_unique<ScalePlayerControlState>();
	}

	if (input.PushGamePadButton(XINPUT_GAMEPAD_LEFT_SHOULDER) ||
		input.PushGamePadButton(XINPUT_GAMEPAD_RIGHT_SHOULDER)) {
		return std::make_unique<RotatePlayerControlState>();
	}

	return std::make_unique<IdlePlayerControlState>();
}

}

std::unique_ptr<PlayerControlState> IdlePlayerControlState::Update(GamePlayScene& scene, Engine::InputSystem::Input& input)
{
	scene.UpdateToggleFlag(input);
	return CreateStateFromInput(input);
}

const char* IdlePlayerControlState::GetName() const
{
	return "Idle";
}

std::unique_ptr<PlayerControlState> MovePlayerControlState::Update(GamePlayScene& scene, Engine::InputSystem::Input& input)
{
	scene.UpdateToggleFlag(input);
	scene.ApplyMoveInput(input.GetGamePadStickX() * kMoveVelocity, input.GetGamePadStickY() * kMoveVelocity);

	if (std::abs(input.GetGamePadStickX()) <= kStickDeadZone &&
		std::abs(input.GetGamePadStickY()) <= kStickDeadZone) {
		return CreateStateFromInput(input);
	}

	return nullptr;
}

const char* MovePlayerControlState::GetName() const
{
	return "Move";
}

std::unique_ptr<PlayerControlState> ScalePlayerControlState::Update(GamePlayScene& scene, Engine::InputSystem::Input& input)
{
	scene.UpdateToggleFlag(input);

	float scaleDelta = 0.0f;
	if (input.GetGamePadTrigger() > 0) {
		scaleDelta -= kScaleStep;
	}
	if (input.GetGamePadTrigger(true) > 0) {
		scaleDelta += kScaleStep;
	}

	if (scaleDelta != 0.0f) {
		scene.ApplyScaleInput(scaleDelta);
		return nullptr;
	}

	return CreateStateFromInput(input);
}

const char* ScalePlayerControlState::GetName() const
{
	return "Scale";
}

std::unique_ptr<PlayerControlState> RotatePlayerControlState::Update(GamePlayScene& scene, Engine::InputSystem::Input& input)
{
	scene.UpdateToggleFlag(input);

	float rotateDelta = 0.0f;
	if (input.PushGamePadButton(XINPUT_GAMEPAD_LEFT_SHOULDER)) {
		rotateDelta += kRotateStep;
	}
	if (input.PushGamePadButton(XINPUT_GAMEPAD_RIGHT_SHOULDER)) {
		rotateDelta -= kRotateStep;
	}

	if (rotateDelta != 0.0f) {
		scene.ApplyRotateInput(rotateDelta);
		return nullptr;
	}

	return CreateStateFromInput(input);
}

const char* RotatePlayerControlState::GetName() const
{
	return "Rotate";
}

}
