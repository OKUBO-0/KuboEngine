#pragma once

#include "game/directxgame/core/GameInputBindings.h"
#include <functional>

namespace DirectXGame {

class DirectXGameSessionContext;
class EnemyManager;
class GameParticleEffects;
class GameplayDebugContext;
class GameplayFlowController;
class GameplayHudPresentation;
class GridPlane;
class PauseBuildHud;
class Player;
class PlayerManager;
class SceneTransitionPresentation;
class SkyDome;

enum class GameplayDebugUiAction {
	None,
	RequestResult,
	ForceBoss,
	BackToTitle,
};

class GameplayDebugUiController final {
public:
	static GameplayDebugUiAction Update(
		GameplayDebugContext& debugContext,
		const GameplayFlowController& gameplayFlow,
		const SceneTransitionPresentation& sceneTransition,
		GameplayHudPresentation& gameplayHud,
		GameParticleEffects& particleEffects,
		PauseBuildHud& pauseBuildHud,
		Player* player,
		PlayerManager* playerManager,
		EnemyManager* enemyManager,
		GridPlane* gridPlane,
		SkyDome* skyDome,
		const DirectXGameSessionContext* sessionContext,
		GameInputBindings::NavigationInputDevice navigationInputDevice,
		bool uiInitialized,
		float deathPresentationElapsed,
		const std::function<void()>& emitLevelUpConfetti);
};

}
