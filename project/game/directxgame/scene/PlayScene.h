#pragma once

#include "BaseScene.h"
#include "GameAudioCache.h"
#include "GameInputBindings.h"
#include "DebugContext.h"
#include "GameplayFlowController.h"
#include "EnemyManager.h"
#include "BossPresentation.h"
#include "CombatEffectsPresentation.h"
#include "FloatingNumberPresentation.h"
#include "GameParticleEffects.h"
#include "PlayerDeathPresentation.h"
#include "SceneTransitionPresentation.h"
#include "Player.h"
#include "PlayerManager.h"
#include "UILabel.h"
#include "GameplayHudPresentation.h"
#include "LevelUpSelectionHud.h"
#include "PauseBuildHud.h"
#include "GridPlane.h"
#include "SkyDome.h"
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
	FloatingNumberPresentation floatingNumberPresentation_{};
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
