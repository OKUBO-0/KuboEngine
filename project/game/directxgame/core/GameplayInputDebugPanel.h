#pragma once

#include "game/directxgame/core/GameInputBindings.h"
#include <string_view>

namespace DirectXGame {

struct GameplayInputDebugState {
	GameInputBindings::NavigationInputDevice navigationDevice =
		GameInputBindings::NavigationInputDevice::Keyboard;
	std::string_view sceneState;
	std::string_view pendingScene;
	bool gameplayUpdateRuns = false;
	bool pausedSafety = true;
	bool levelUpSafety = true;
	bool deadSafety = true;
};

class GameplayInputDebugPanel final {
public:
	static void Draw(
		bool* open,
		Engine::InputSystem::Input* input,
		const GameplayInputDebugState& state);
};

}
