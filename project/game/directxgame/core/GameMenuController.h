#pragma once

#include "GameInputBindings.h"

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
		state.pause = GameInputBindings::IsPauseTriggered(input);

		const bool previous = GameInputBindings::IsMenuUpTriggered(input) ||
			GameInputBindings::IsMenuLeftTriggered(input);
		const bool next = GameInputBindings::IsMenuDownTriggered(input) ||
			GameInputBindings::IsMenuRightTriggered(input);
		if (previous != next) {
			state.moveDelta = previous ? -1 : 1;
		}
		return state;
	}
};

}
