#pragma once

#include "Input.h"
#include "Vector2.h"
#include <Windows.h>
#include <algorithm>
#include <cmath>

namespace DirectXGame::GameInputBindings {

enum class NavigationInputDevice {
	None,
	Mouse,
	Keyboard,
	Gamepad,
};

inline float ClampAxis(float value)
{
	return std::clamp(value, -1.0f, 1.0f);
}

inline Vector2 GetMoveVector(Engine::InputSystem::Input* input)
{
	Vector2 move{ 0.0f, 0.0f };
	if (!input) {
		return move;
	}

	if (input->PushKey(DIK_W) || input->PushKey(DIK_UP)) { move.y += 1.0f; }
	if (input->PushKey(DIK_S) || input->PushKey(DIK_DOWN)) { move.y -= 1.0f; }
	if (input->PushKey(DIK_A) || input->PushKey(DIK_LEFT)) { move.x -= 1.0f; }
	if (input->PushKey(DIK_D) || input->PushKey(DIK_RIGHT)) { move.x += 1.0f; }

	move.x += ClampAxis(input->GetGamePadStickX());
	move.y += ClampAxis(input->GetGamePadStickY());

	if (input->PushGamePadButton(XINPUT_GAMEPAD_DPAD_UP)) { move.y += 1.0f; }
	if (input->PushGamePadButton(XINPUT_GAMEPAD_DPAD_DOWN)) { move.y -= 1.0f; }
	if (input->PushGamePadButton(XINPUT_GAMEPAD_DPAD_LEFT)) { move.x -= 1.0f; }
	if (input->PushGamePadButton(XINPUT_GAMEPAD_DPAD_RIGHT)) { move.x += 1.0f; }

	const float lengthSq = move.x * move.x + move.y * move.y;
	if (lengthSq > 1.0f) {
		const float length = std::sqrt(lengthSq);
		move.x /= length;
		move.y /= length;
	}

	return move;
}

inline bool GetAimVector(Engine::InputSystem::Input* input, Vector2& outAim)
{
	if (!input) {
		return false;
	}

	const float x = ClampAxis(input->GetGamePadStickX(true));
	const float y = ClampAxis(input->GetGamePadStickY(true));
	constexpr float kAimDeadZone = 0.25f;
	if (std::abs(x) < kAimDeadZone && std::abs(y) < kAimDeadZone) {
		return false;
	}

	outAim = { x, y };
	return true;
}

inline bool HasMouseNavigationInput(Engine::InputSystem::Input* input)
{
	if (!input) {
		return false;
	}

	const auto mouseMove = input->GetMouseMove();
	return mouseMove.lX != 0 || mouseMove.lY != 0 || input->PushMouse(0) || input->PushMouse(1);
}

inline bool HasKeyboardNavigationInput(Engine::InputSystem::Input* input)
{
	return input && (
		input->PushKey(DIK_W) ||
		input->PushKey(DIK_A) ||
		input->PushKey(DIK_S) ||
		input->PushKey(DIK_D) ||
		input->PushKey(DIK_UP) ||
		input->PushKey(DIK_DOWN) ||
		input->PushKey(DIK_LEFT) ||
		input->PushKey(DIK_RIGHT) ||
		input->TriggerKey(DIK_RETURN) ||
		input->TriggerKey(DIK_SPACE) ||
		input->TriggerKey(DIK_ESCAPE) ||
		input->TriggerKey(DIK_P));
}

inline bool HasGamepadNavigationInput(Engine::InputSystem::Input* input)
{
	if (!input) {
		return false;
	}

	constexpr float kDeviceDetectDeadZone = 0.25f;
	return std::abs(input->GetGamePadStickX()) >= kDeviceDetectDeadZone ||
		std::abs(input->GetGamePadStickY()) >= kDeviceDetectDeadZone ||
		std::abs(input->GetGamePadStickX(true)) >= kDeviceDetectDeadZone ||
		std::abs(input->GetGamePadStickY(true)) >= kDeviceDetectDeadZone ||
		input->PushGamePadButton(XINPUT_GAMEPAD_DPAD_UP) ||
		input->PushGamePadButton(XINPUT_GAMEPAD_DPAD_DOWN) ||
		input->PushGamePadButton(XINPUT_GAMEPAD_DPAD_LEFT) ||
		input->PushGamePadButton(XINPUT_GAMEPAD_DPAD_RIGHT) ||
		input->TriggerGamePadButton(XINPUT_GAMEPAD_A) ||
		input->TriggerGamePadButton(XINPUT_GAMEPAD_B) ||
		input->TriggerGamePadButton(XINPUT_GAMEPAD_START) ||
		input->TriggerGamePadButton(XINPUT_GAMEPAD_BACK);
}

inline NavigationInputDevice DetectNavigationInputDevice(
	Engine::InputSystem::Input* input,
	NavigationInputDevice currentDevice = NavigationInputDevice::Keyboard)
{
	if (HasMouseNavigationInput(input)) {
		return NavigationInputDevice::Mouse;
	}
	if (HasKeyboardNavigationInput(input)) {
		return NavigationInputDevice::Keyboard;
	}
	if (HasGamepadNavigationInput(input)) {
		return NavigationInputDevice::Gamepad;
	}
	return currentDevice;
}

inline const char* ToDisplayName(NavigationInputDevice device)
{
	switch (device) {
	case NavigationInputDevice::Mouse:
		return "Mouse";
	case NavigationInputDevice::Keyboard:
		return "Keyboard";
	case NavigationInputDevice::Gamepad:
		return "Gamepad";
	case NavigationInputDevice::None:
	default:
		return "None";
	}
}

inline const char* GetConfirmLabel(NavigationInputDevice device)
{
	switch (device) {
	case NavigationInputDevice::Mouse:
		return "Left Click";
	case NavigationInputDevice::Gamepad:
		return "GamePad A";
	case NavigationInputDevice::Keyboard:
	case NavigationInputDevice::None:
	default:
		return "Enter / Space";
	}
}

inline const char* GetCancelLabel(NavigationInputDevice device)
{
	switch (device) {
	case NavigationInputDevice::Mouse:
		return "Right Click";
	case NavigationInputDevice::Gamepad:
		return "GamePad B";
	case NavigationInputDevice::Keyboard:
	case NavigationInputDevice::None:
	default:
		return "Esc";
	}
}

inline const char* GetPauseLabel(NavigationInputDevice device)
{
	switch (device) {
	case NavigationInputDevice::Gamepad:
		return "GamePad Start";
	case NavigationInputDevice::Keyboard:
	case NavigationInputDevice::Mouse:
	case NavigationInputDevice::None:
	default:
		return "Esc / P";
	}
}

inline bool IsMouseConfirmTriggered(Engine::InputSystem::Input* input)
{
	return input && input->TriggerMouse(0);
}

inline bool IsMouseCancelTriggered(Engine::InputSystem::Input* input)
{
	return input && input->TriggerMouse(1);
}

inline bool IsKeyboardMenuUpTriggered(Engine::InputSystem::Input* input)
{
	return input && (input->TriggerKey(DIK_W) || input->TriggerKey(DIK_UP));
}

inline bool IsKeyboardMenuDownTriggered(Engine::InputSystem::Input* input)
{
	return input && (input->TriggerKey(DIK_S) || input->TriggerKey(DIK_DOWN));
}

inline bool IsKeyboardConfirmTriggered(Engine::InputSystem::Input* input)
{
	return input && (input->TriggerKey(DIK_RETURN) || input->TriggerKey(DIK_SPACE));
}

inline bool IsKeyboardCancelTriggered(Engine::InputSystem::Input* input)
{
	return input && input->TriggerKey(DIK_ESCAPE);
}

inline bool IsKeyboardPauseTriggered(Engine::InputSystem::Input* input)
{
	return input && (input->TriggerKey(DIK_ESCAPE) || input->TriggerKey(DIK_P));
}

inline bool IsGamepadMenuUpTriggered(Engine::InputSystem::Input* input)
{
	return input && input->TriggerGamePadButton(XINPUT_GAMEPAD_DPAD_UP);
}

inline bool IsGamepadMenuDownTriggered(Engine::InputSystem::Input* input)
{
	return input && input->TriggerGamePadButton(XINPUT_GAMEPAD_DPAD_DOWN);
}

inline bool IsGamepadConfirmTriggered(Engine::InputSystem::Input* input)
{
	return input && input->TriggerGamePadButton(XINPUT_GAMEPAD_A);
}

inline bool IsGamepadCancelTriggered(Engine::InputSystem::Input* input)
{
	return input && input->TriggerGamePadButton(XINPUT_GAMEPAD_B);
}

inline bool IsGamepadPauseTriggered(Engine::InputSystem::Input* input)
{
	return input && (input->TriggerGamePadButton(XINPUT_GAMEPAD_START) || input->TriggerGamePadButton(XINPUT_GAMEPAD_BACK));
}

inline bool IsMenuUpTriggered(Engine::InputSystem::Input* input)
{
	return IsKeyboardMenuUpTriggered(input) || IsGamepadMenuUpTriggered(input);
}

inline bool IsMenuDownTriggered(Engine::InputSystem::Input* input)
{
	return IsKeyboardMenuDownTriggered(input) || IsGamepadMenuDownTriggered(input);
}

inline bool IsUiConfirmTriggered(Engine::InputSystem::Input* input)
{
	return IsKeyboardConfirmTriggered(input) || IsGamepadConfirmTriggered(input) || IsMouseConfirmTriggered(input);
}

inline bool IsUiCancelTriggered(Engine::InputSystem::Input* input)
{
	return IsKeyboardCancelTriggered(input) || IsGamepadCancelTriggered(input) || IsMouseCancelTriggered(input);
}

inline bool IsConfirmTriggered(Engine::InputSystem::Input* input)
{
	return IsUiConfirmTriggered(input);
}

}
