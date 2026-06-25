#pragma once

#include "game/directxgame/core/GameInputBindings.h"
#include <string_view>

namespace DirectXGame {

struct DebugInputState {
	GameInputBindings::NavigationInputDevice navigationDevice =
		GameInputBindings::NavigationInputDevice::Keyboard;
	std::string_view sceneState;
	std::string_view pendingScene;
	bool gameplayUpdateRuns = false;
	bool pausedSafety = true;
	bool levelUpSafety = true;
	bool deadSafety = true;
};

namespace DebugUI::Input {

void Draw(
		bool* open,
		Engine::InputSystem::Input* input,
		const DebugInputState& state);

}

}
