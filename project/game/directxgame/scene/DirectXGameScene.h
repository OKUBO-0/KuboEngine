#pragma once

#include "BaseScene.h"
#include "game/directxgame/core/GameAudioCache.h"
#include "game/directxgame/core/GameInputBindings.h"
#include "game/directxgame/core/GameplayDebugContext.h"
#include "game/directxgame/core/GameplayFlowController.h"
#include "game/directxgame/enemy/EnemyManager.h"
#include "game/directxgame/effects/BossPresentation.h"
#include "game/directxgame/effects/CombatEffectsPresentation.h"
#include "game/directxgame/effects/GameParticleEffects.h"
#include "game/directxgame/effects/PlayerDeathPresentation.h"
#include "game/directxgame/effects/SceneTransitionPresentation.h"
#include "game/directxgame/player/Player.h"
#include "game/directxgame/player/PlayerManager.h"
#include "game/directxgame/ui/common/UILabel.h"
#include "game/directxgame/ui/hud/GameplayHudPresentation.h"
#include "game/directxgame/ui/hud/LevelUpSelectionHud.h"
#include "game/directxgame/ui/hud/PauseBuildHud.h"
#include "game/directxgame/world/GridPlane.h"
#include "game/directxgame/world/SkyDome.h"
#include <cstdint>
#include <memory>

namespace DirectXGame {

class DirectXGameSessionContext;

class DirectXGameScene : public Engine::Scene::BaseScene {
public:
	explicit DirectXGameScene(std::shared_ptr<DirectXGameSessionContext> sessionContext);

	void Initialize() override;
	void Finalize() override;
	void Update() override;
	void Draw() override;

private:
	void InitializeLighting();
	void InitializeWorld();
	void InitializeUi();
	void UpdateGamePlay(float deltaTime);
	void UpdateEffects();
	void UpdateUi(float deltaTime);
	void DrawUi();
	void EnterPlaying();
	void TogglePause();
	void StartBossPhase();
	void UpdateBossEntrance(float deltaTime);
	void StartBossDefeatPresentation();
	void UpdateBossDefeatPresentation(float deltaTime);
	void RequestLevelUp();
	void RequestSceneChange(const char* sceneId);
	void RequestResultScene();
	void RecordResultSummary();
	void SpawnLevelUpConfetti();
	void QueueDebugDraw();
	void UpdateDebugUi();
	void ApplyPostEffect() const;
	std::shared_ptr<DirectXGameSessionContext> sessionContext_;
	std::unique_ptr<Player> player_;
	std::unique_ptr<PlayerManager> playerManager_;
	std::unique_ptr<EnemyManager> enemyManager_;
	std::unique_ptr<GridPlane> gridPlane_;
	std::unique_ptr<SkyDome> skyDome_;
	SceneTransitionPresentation sceneTransition_{};
	GameplayDebugContext debugContext_{};

	GameplayFlowController gameplayFlow_{};
	GameplayHudPresentation gameplayHud_{};
	PauseBuildHud pauseBuildHud_{};
	LevelUpSelectionHud levelUpSelectionHud_{};
	GameParticleEffects particleEffects_{};
	CombatEffectsPresentation combatEffectsPresentation_{};
	BossPresentation bossPresentation_{};
	PlayerDeathPresentation playerDeathPresentation_{};
	SoundHandle startSeHandle_ = 0;
	SoundHandle pauseSeHandle_ = 0;
	SoundHandle levelUpSeHandle_ = 0;
	SoundHandle gameOverSeHandle_ = 0;
	GameInputBindings::NavigationInputDevice navigationInputDevice_ =
		GameInputBindings::NavigationInputDevice::Keyboard;
	bool uiInitialized_ = false;
	bool gameOverSePlayed_ = false;
};

}
