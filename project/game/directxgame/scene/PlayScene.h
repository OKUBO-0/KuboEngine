#pragma once

#include "BaseScene.h"
#include "game/directxgame/core/GameAudioCache.h"
#include "game/directxgame/core/GameInputBindings.h"
#include "game/directxgame/core/DebugContext.h"
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

class GameSession;

class PlayScene : public Engine::Scene::BaseScene {
public:
	explicit PlayScene(std::shared_ptr<GameSession> sessionContext);

	void Initialize() override;
	void Finalize() override;
	void Update() override;
	void Draw() override;

private:
	void InitializeLighting();
	void InitializeWorld();
	void InitializeUi();
	bool UpdateSceneStressTelemetry();
	bool UpdatePendingSceneTransition(float deltaTime);
	void UpdateGameplayPhase(float deltaTime, bool gameplayFrozen);
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
	void UpdateDebugUI();
	void ApplyPostEffect() const;
	std::shared_ptr<GameSession> sessionContext_;
	std::unique_ptr<Player> player_;
	std::unique_ptr<PlayerManager> playerManager_;
	std::unique_ptr<EnemyManager> enemyManager_;
	std::unique_ptr<GridPlane> gridPlane_;
	std::unique_ptr<SkyDome> skyDome_;
	SceneTransitionPresentation sceneTransition_{};
	DebugContext debugContext_{};

	GameplayFlowController gameplayFlow_{};
	GameplayHudPresentation gameplayHud_{};
	PauseBuildHud pauseBuildHud_{};
	LevelUpSelectionHud levelUpSelectionHud_{};
	GameParticleEffects particleEffects_{};
	CombatEffectsPresentation combatEffectsPresentation_{};
	BossPresentation bossPresentation_{};
	PlayerDeathPresentation playerDeathPresentation_{};
	SoundHandle startSeHandle_{};
	SoundHandle pauseSeHandle_{};
	SoundHandle levelUpSeHandle_{};
	SoundHandle gameOverSeHandle_{};
	GameInputBindings::NavigationInputDevice navigationInputDevice_ =
		GameInputBindings::NavigationInputDevice::Keyboard;
	bool uiInitialized_ = false;
	bool gameOverSePlayed_ = false;
};

}
