#pragma once

namespace DirectXGame {

class EnemyManager;

enum class GameplayDebugAction {
	None,
	GoToResult,
	ForceTimeUp,
	BackToTitle,
};

class GameplaySceneDebugPanel final {
public:
	static void DrawSettings(
		bool* open,
		bool& freezeGameplay,
		const char* stateName);
	static GameplayDebugAction Draw(
		bool* open,
		EnemyManager* enemyManager);
};

}
