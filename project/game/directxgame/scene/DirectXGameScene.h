#pragma once

#include "BaseScene.h"
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
	void InitializeWorld();
	void InitializeUi();
	void InitializePauseBuildUi();
	void InitializeParticles();
	void UpdateGamePlay(float deltaTime);
	void UpdateEffects();
	void UpdateUi(float deltaTime);
	void UpdatePauseBuildUi();
	void UpdateLevelUpAnimation(float deltaTime);
	void UpdateLevelUpConfetti(float deltaTime);
	void DrawUi();
	void DrawPauseBuildUi();
	void DrawLevelUpConfetti();
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
	void SpawnLevelUpConfetti();
	void MoveMenuSelection(int32_t delta);
	void QueueDebugDraw();
	void QueueEffectDraw();
	void UpdateDebugUi();
	void ApplyPostEffect() const;

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
		UpgradeType type = UpgradeType::Attack;
		std::string texturePath;
	};

	struct PauseBuildLayout {
		Vector2 position{ 52.0f, 116.0f };
		float stepX = 112.0f;
		Vector2 iconSize{ 96.0f, 54.0f };
		bool visible = true;
		bool debugEnabled = false;
	};

	struct ConfettiParticle {
		std::unique_ptr<Engine::Graphics2D::Sprite> sprite;
		Vector2 position{};
		Vector2 velocity{};
		Vector2 size{};
		Vector4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
		float rotation = 0.0f;
		float angularVelocity = 0.0f;
		float lifetime = 0.0f;
		float age = 0.0f;
	};

	std::shared_ptr<DirectXGameSessionContext> sessionContext_;
	std::unique_ptr<Player> player_;
	std::unique_ptr<PlayerManager> playerManager_;
	std::unique_ptr<EnemyManager> enemyManager_;
	std::unique_ptr<GridPlane> gridPlane_;
	std::unique_ptr<SkyDome> skyDome_;
	std::unique_ptr<CurtainTransition> curtain_;
	GameLightSettings lightSettings_{};

	GameState gameState_ = GameState::Start;
	Timer timer_;
	HpGauge hpGauge_;
	ExpGauge expGauge_;
	KeyUI keyUI_;
	MiniMap miniMap_;
	UILabel startOverlay_;
	UILabel pauseOverlay_;
	UILabel pauseCursor_;
	UILabel levelUpOverlay_;
	UILabel hitFlashOverlay_;
	UILabel deathOverlay_;
	std::array<UILabel, 3> levelUpChoiceSprites_;
	std::array<UILabel, 5> pauseBuildIcons_;
	std::vector<LevelUpChoice> levelUpChoices_;
	std::vector<ConfettiParticle> levelUpConfetti_;
	PauseBuildLayout pauseBuildLayout_{};
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
	float previousLightningEffectTimer_ = 0.0f;
	float levelUpSlideOffsetX_ = 1280.0f;
	std::string pendingSceneId_;
	LevelUpAnimationState levelUpAnimationState_ = LevelUpAnimationState::Hidden;
	bool levelUpSelectionPending_ = false;
	GameInputBindings::NavigationInputDevice navigationInputDevice_ =
		GameInputBindings::NavigationInputDevice::Keyboard;
	bool uiInitialized_ = false;
	bool gameOverSePlayed_ = false;
	bool debugDrawEnabled_ = false;
};

}
