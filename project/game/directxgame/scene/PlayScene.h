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
	// 初期化は大きく「ライト」「ワールド」「UI」に分ける。
	// PlayScene は各サブシステムを接続する場に留め、個別ロジックは Manager / Presentation へ委譲する。
	void InitializeLighting();
	void InitializeWorld();
	void InitializeUi();

	// 1フレーム更新の上位制御。
	// 入力、ゲーム進行、演出、UI、遷移判定の順序をここで固定し、再現性のある進行にする。
	bool UpdateSceneStressTelemetry();
	bool UpdatePendingSceneTransition(float deltaTime);
	void UpdateGameplayPhase(float deltaTime, bool gameplayFrozen);
	void UpdateGamePlay(float deltaTime);
	void UpdateEffects();
	void UpdateUi(float deltaTime);
	void DrawUi();

	// シーン状態遷移。
	// ポーズ、ボス演出、レベルアップ、リザルト遷移を GameplayFlowController と同期させる。
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

	// 画面効果とデバッグ表示。
	// ゲーム本体の状態は変えず、視覚演出や検証用描画だけを扱う。
	void SpawnLevelUpConfetti();
	void QueueDebugDraw();
	void UpdateDebugUI();
	void ApplyPostEffect() const;

	// Title / Play / Result をまたぐスコアや選択キャラクターを保持する共有文脈。
	std::shared_ptr<GameSession> sessionContext_;

	// ゲーム実体の所有権。生存期間は PlayScene と同じで、外部へ所有権は渡さない。
	std::unique_ptr<Player> player_;
	std::unique_ptr<PlayerManager> playerManager_;
	std::unique_ptr<EnemyManager> enemyManager_;
	std::unique_ptr<GridPlane> gridPlane_;
	std::unique_ptr<SkyDome> skyDome_;

	// 表示・演出系。ゲーム状態を直接所有せず、Manager の状態を読み取って描画へ変換する。
	SceneTransitionPresentation sceneTransition_{};
	DebugContext debugContext_{};

	// 進行制御と HUD。入力停止、レベルアップ待ち、ポーズなどの状態を集約する。
	GameplayFlowController gameplayFlow_{};
	GameplayHudPresentation gameplayHud_{};
	PauseBuildHud pauseBuildHud_{};
	LevelUpSelectionHud levelUpSelectionHud_{};
	GameParticleEffects particleEffects_{};
	CombatEffectsPresentation combatEffectsPresentation_{};
	FloatingNumberPresentation floatingNumberPresentation_{};
	BossPresentation bossPresentation_{};
	PlayerDeathPresentation playerDeathPresentation_{};

	SoundHandle gameBgmHandle_{};
	SoundHandle bossBgmHandle_{};
	SoundHandle startSeHandle_{};
	SoundHandle pauseSeHandle_{};
	SoundHandle levelUpSeHandle_{};
	SoundHandle gameOverSeHandle_{};
	SoundHandle bossPhaseSeHandle_{};
	SoundHandle bossDefeatSeHandle_{};

	// UI 入力と一度だけ行う初期化/再生制御の状態。
	GameInputBindings::NavigationInputDevice navigationInputDevice_ =
		GameInputBindings::NavigationInputDevice::Keyboard;
	bool uiInitialized_ = false;
	bool gameOverSePlayed_ = false;
	bool bossBgmPlaying_ = false;
};

}
