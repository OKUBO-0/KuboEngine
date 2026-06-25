#pragma once

#include "game/directxgame/core/GameInputBindings.h"
#include <functional>

namespace DirectXGame {

class GameSession;
class EnemyManager;
class GameParticleEffects;
class DebugContext;
class GameplayFlowController;
class GameplayHudPresentation;
class GridPlane;
class PauseBuildHud;
class Player;
class PlayerManager;
class SceneTransitionPresentation;
class SkyDome;

enum class DebugUIAction {
	None,
	RequestResult,
	ForceBoss,
	BackToTitle,
};

namespace DebugUI {

DebugUIAction Update(
		DebugContext& debugContext,
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
		const GameSession* sessionContext,
		GameInputBindings::NavigationInputDevice navigationInputDevice,
		bool uiInitialized,
		float deathPresentationElapsed,
	const std::function<void()>& emitLevelUpConfetti);

}

}
