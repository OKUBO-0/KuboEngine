#pragma once

namespace DirectXGame {

class EnemyManager;

enum class SceneAction {
	None,
	GoToResult,
	ForceTimeUp,
	BackToTitle,
};

namespace DebugUI::Scene {

SceneAction Draw(
		bool* open,
		EnemyManager* enemyManager);

}

}
