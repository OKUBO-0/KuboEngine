#pragma once

#include "BaseScene.h"
#include "Camera.h"
#include "ImGuizmoManager.h"
#include "Object3D.h"
#include "game/directxgame/core/GameAudioCache.h"
#include "game/directxgame/core/GameInputBindings.h"
#include "game/directxgame/core/GameLightSettings.h"
#include "game/directxgame/enemy/EnemyManager.h"
#include "game/directxgame/effects/CurtainTransition.h"
#include "game/directxgame/player/Player.h"
#include "game/directxgame/player/PlayerManager.h"
#include "game/directxgame/ui/common/UILabel.h"
#include "game/directxgame/ui/gauge/ExpGauge.h"
#include "game/directxgame/ui/gauge/HpGauge.h"
#include "game/directxgame/ui/hud/KeyUI.h"
#include "game/directxgame/ui/hud/MiniMap.h"
#include "game/directxgame/ui/hud/Timer.h"
#include "game/directxgame/world/GridPlane.h"
#include "game/directxgame/world/SkyDome.h"
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

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
	void InitializeDebugCamera();
	void InitializeWorld();
	void InitializeUi();
	void InitializePauseBuildUi();
	void InitializeParticles();
	void LoadDebugTuning();
	void SaveDebugTuning() const;
	void SaveSoftCapTelemetrySnapshot() const;
	void ApplyParticleBehaviorTuning();
	void UpdateGamePlay(float deltaTime);
	void UpdateEffects();
	void UpdateUi(float deltaTime);
	void UpdatePauseBuildUi();
	void UpdateLevelUpAnimation(float deltaTime);
	void DrawUi();
	void DrawPauseBuildUi();
	void SavePauseBuildLayout() const;
	void EnterPlaying();
	void TogglePause();
	void RequestLevelUp();
	void RequestSceneChange(const char* sceneId);
	void RequestResultScene();
	void RecordResultSummary();
	void UpdateCurtain(float deltaTime);
	void BuildLevelUpChoices();
	void ApplySelectedLevelUpChoice();
	void ApplyLevelUpLayout();
	int32_t GetHoveredPauseMenuIndex() const;
	bool IsMousePauseConfirm(int32_t hoveredMenuIndex) const;
	int32_t GetHoveredLevelUpChoiceIndex() const;
	bool IsMouseLevelUpConfirm(int32_t hoveredChoiceIndex) const;
	void SpawnLevelUpConfetti();
	void MoveMenuSelection(int32_t delta);
	void QueueDebugDraw();
	void QueueEffectDraw();
	void UpdatePlayerLight();
	void ApplyLightSettingsToWorld();
	void UpdateDebugCamera();
	void UpdateDebugUi();
	void ApplyPostEffect() const;
	const char* GetGameStateName() const;

	enum class GameState {
		Start,
		Playing,
		Paused,
		LevelUp,
		Dead,
	};

	enum class UpgradeType {
		Normal,
		Orbit,
		Drone,
		Lightning,
		Attack,
		MaxHP,
		MoveSpeed,
		Heal,
	};

	enum class LevelUpAnimationState {
		Hidden,
		Entering,
		Idle,
		Exiting,
	};

	struct LevelUpChoice {
		std::string texturePath;
		std::string iconPath;
		std::function<void()> apply;
	};

	struct PauseBuildLayout {
		Vector2 position{ 52.0f, 116.0f };
		float stepX = 112.0f;
		Vector2 iconSize{ 96.0f, 54.0f };
		bool visible = true;
		bool debugEnabled = false;
	};

	struct PauseMenuLayout {
		std::array<Vector2, 2> hitboxPositions{
			Vector2{ 840.0f, 294.0f },
			Vector2{ 840.0f, 462.0f },
		};
		Vector2 hitboxSize{ 280.0f, 92.0f };
	};

	struct ParticleTuning {
		int32_t playerDamageSparkCount = 18;
		int32_t playerDamageRippleCount = 1;
		int32_t enemyHitSparkCount = 10;
		int32_t enemyDeathSparkCount = 24;
		int32_t enemyDeathSmokeCount = 10;
		int32_t expSparkCount = 8;
		int32_t lightningSparkCount = 14;
		int32_t lightningRippleCount = 1;
		int32_t levelUpConfettiCount = 22;
		int32_t playerDeathSparkCount = 48;
		int32_t playerDeathSmokeCount = 18;
		int32_t playerDeathRippleCount = 2;
		float sparkLifetime = 0.35f;
		float sparkVelocityScale = 1.0f;
		float sparkScaleMultiplier = 1.0f;
		float smokeLifetime = 0.72f;
		float smokeScaleMultiplier = 1.0f;
		float rippleLifetime = 0.42f;
		float rippleExpandSpeed = 4.5f;
		float confettiVelocityScale = 0.55f;
		float confettiScaleMultiplier = 0.55f;
	};

	struct DebugWindowVisibility {
		bool windowSwitcher = false;
		bool sceneView = true;
		bool objectView = false;
		bool particleView = false;
		bool statisticsView = true;
		bool offscreenSettings = false;
		bool lightSettings = false;
		bool gizmo = false;
		bool objectManager = false;
		bool motionEditor = false;
		bool spriteManager = false;
		bool colliderTagManager = false;
		bool audio = false;
		bool keyInputDebug = true;
		bool sceneSettings = true;
		bool sceneSpecificDebug = false;
		bool objectSettings = false;
	};

	enum class DebugGizmoTarget {
		Player,
		PointLight,
	};

	std::shared_ptr<DirectXGameSessionContext> sessionContext_;
	std::unique_ptr<Player> player_;
	std::unique_ptr<PlayerManager> playerManager_;
	std::unique_ptr<EnemyManager> enemyManager_;
	std::unique_ptr<GridPlane> gridPlane_;
	std::unique_ptr<SkyDome> skyDome_;
	std::unique_ptr<CurtainTransition> curtain_;
	GameLightSettings lightSettings_{};
	Engine::CameraSystem::Camera debugCamera_{};
	Vector3 debugCameraPosition_{ 0.0f, 85.0f, -85.0f };
	Vector3 debugCameraRotation_{ 0.82f, 0.0f, 0.0f };
	Vector3 playerLightOffset_ = GameLightDefaults::kPlayerLightOffset;
	bool debugCameraEnabled_ = false;
	bool lightDebugDrawEnabled_ = true;
	bool playerLightFollowsPlayer_ = true;

	GameState gameState_ = GameState::Start;
#ifdef _DEBUG
	bool debugFreezeGameplay_ = false;
#endif
	Timer timer_;
	HpGauge hpGauge_;
	ExpGauge expGauge_;
	KeyUI keyUI_;
	MiniMap miniMap_;
	MiniMap gameplayMiniMap_;
	UILabel startOverlay_;
	UILabel pauseOverlay_;
	UILabel pauseCursor_;
	UILabel levelUpOverlay_;
	UILabel hitFlashOverlay_;
	UILabel deathOverlay_;
	std::array<UILabel, 3> levelUpChoiceSprites_;
	std::array<UILabel, 3> levelUpChoiceIcons_;
	std::array<UILabel, 5> pauseBuildIcons_;
	std::vector<LevelUpChoice> levelUpChoices_;
	PauseBuildLayout pauseBuildLayout_{};
	PauseMenuLayout pauseMenuLayout_{};
	ParticleTuning particleTuning_{};
	SoundHandle startSeHandle_ = 0;
	SoundHandle pauseSeHandle_ = 0;
	SoundHandle levelUpSeHandle_ = 0;
	SoundHandle gameOverSeHandle_ = 0;
	int32_t menuSelection_ = 0;
	int32_t previousHp_ = 0;
	int32_t previousEffectHp_ = 0;
	int32_t previousEffectTotalExp_ = 0;
	float hitFlashTimer_ = 0.0f;
	float deathTimer_ = 0.0f;
	float startIntroTimer_ = 0.0f;
	float previousLightningEffectTimer_ = 0.0f;
	uint32_t debugSoftCapTelemetryStartFrame_ = 0;
	bool debugSoftCapAutoLogEnabled_ = false;
	int32_t debugSoftCapAutoLogIntervalSeconds_ = 60;
	uint32_t debugSoftCapNextAutoLogFrame_ = 0;
	uint32_t debugSoftCapLastAutoLogFrame_ = UINT32_MAX;
	float levelUpSlideOffsetX_ = 1280.0f;
	Vector2 levelUpChoiceSize_{ 1280.0f, 720.0f };
	float levelUpChoiceStepY_ = 140.0f;
	Vector2 levelUpChoiceHitboxOffset_{ 465.0f, 214.0f };
	Vector2 levelUpChoiceHitboxSize_{ 435.0f, 68.0f };
	float uiAnimationTime_ = 0.0f;
	std::string pendingSceneId_;
	LevelUpAnimationState levelUpAnimationState_ = LevelUpAnimationState::Hidden;
	bool levelUpSelectionPending_ = false;
	GameInputBindings::NavigationInputDevice navigationInputDevice_ =
		GameInputBindings::NavigationInputDevice::Keyboard;
	bool uiInitialized_ = false;
	bool gameOverSePlayed_ = false;
	bool deathEffectEmitted_ = false;
	bool deathCurtainStarted_ = false;
	bool startIntroFinished_ = false;
	bool debugDrawEnabled_ = false;
	DebugWindowVisibility debugWindows_{};
	DebugGizmoTarget debugGizmoTarget_ = DebugGizmoTarget::Player;
	Engine::Editor::ImGuizmoManager::Operation debugGizmoOperation_ =
		Engine::Editor::ImGuizmoManager::Operation::Translate;
};

}
