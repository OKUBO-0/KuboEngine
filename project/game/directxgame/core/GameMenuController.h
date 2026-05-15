#pragma once

#include "game/directxgame/core/GameInputBindings.h"

namespace DirectXGame {

struct GameMenuInputState {
	GameInputBindings::NavigationInputDevice device = GameInputBindings::NavigationInputDevice::Keyboard;
	int32_t moveDelta = 0;
	bool confirm = false;
	bool cancel = false;
	bool pause = false;
};

class GameMenuController {
public:
	static GameMenuInputState Update(
		Engine::InputSystem::Input* input,
		GameInputBindings::NavigationInputDevice currentDevice)
	{
		GameMenuInputState state{};
		state.device = GameInputBindings::DetectNavigationInputDevice(input, currentDevice);
		state.confirm = GameInputBindings::IsUiConfirmTriggered(input);
		state.cancel = GameInputBindings::IsUiCancelTriggered(input);
		state.pause = GameInputBindings::IsKeyboardPauseTriggered(input) ||
			GameInputBindings::IsGamepadPauseTriggered(input);

		if (GameInputBindings::IsMenuUpTriggered(input)) {
			state.moveDelta -= 1;
		}
		if (GameInputBindings::IsMenuDownTriggered(input)) {
			state.moveDelta += 1;
		}
		return state;
	}
};

}
