#pragma once

#include "UILabel.h"
#include "ExpGauge.h"
#include "HpGauge.h"
#include "KeyUI.h"
#include "MiniMap.h"
#include "Timer.h"
#include "BitmapText.h"
#include "GameInputBindings.h"
#include "Sprite.h"
#include "GameTextureCache.h"
#include "UIPanel.h"
#include "PassiveItemType.h"
#include <array>
#include <memory>
#include <vector>

namespace Engine::InputSystem {
class Input;
}

namespace DirectXGame {

class EnemyManager;
class GameplayFlowController;
class Player;
class PlayerManager;

class GameplayHudPresentation final {
public:
	void Initialize(const PlayerManager* playerManager);
	void Update(
		float deltaTime,
		bool gameplayFrozen,
		const GameplayFlowController& flow,
		Player* player,
		PlayerManager* playerManager,
		EnemyManager* enemyManager,
		Engine::InputSystem::Input* input,
		float deathOverlayAlpha,
		int32_t runCoins);
	void Draw(const GameplayFlowController& flow);
	void DrawDeathForeground(const GameplayFlowController& flow);
	void TriggerHitFlash(float duration);
	void DebugDrawImGui();
	void SaveLayout() const;

	float GetAnimationTime() const { return animationTime_; }
	float GetHitFlashTimer() const { return hitFlashTimer_; }
	Timer& GetTimer() { return timer_; }
	HpGauge& GetHpGauge() { return hpGauge_; }
	ExpGauge& GetExpGauge() { return expGauge_; }
	KeyUI& GetKeyUi() { return keyUi_; }
	MiniMap& GetPauseMiniMap() { return pauseMiniMap_; }

private:
	void UpdateCoinDisplay(int32_t runCoins);
	void UpdateKillDisplay(int32_t killCount);
	void UpdateBossHpBar(
		float deltaTime,
		const GameplayFlowController& flow,
		EnemyManager* enemyManager);
	void DrawCoinDisplay();
	void DrawKillDisplay();
	void DrawBossHpBar();
	void UpdateBuildStrip(const PlayerManager* playerManager);
	void DrawTopHud();
	void DrawBuildStrip();
	void ApplyCounterLayout();
	void ApplyGameplayMiniMapLayout();
	void ApplyBossHpBarLayout();

	Timer timer_;
	HpGauge hpGauge_;
	ExpGauge expGauge_;
	KeyUI keyUi_;
	MiniMap pauseMiniMap_;
	MiniMap gameplayMiniMap_;
	UILabel introTopBar_;
	UILabel introBottomBar_;
	UILabel hitFlashOverlay_;
	BitmapText deathGameOverText_;
	BitmapText deathPromptText_;
	UILabel bossHpFrame_;
	UILabel bossHpBackground_;
	UILabel bossHpFill_;
	BitmapText bossHpText_;
	UIPanel escPanel_;
	std::array<UIPanel, 4> escPanelBorders_;
	BitmapText escText_;
	UIPanel coinPanel_;
	UIPanel killPanel_;
	std::array<UIPanel, 8> counterPanelBorders_;
	UILabel coinIcon_;
	UILabel killIcon_;
	TextureHandle coinDigitTexture_ = 0;
	static constexpr int32_t kCoinDigitCount = 6;
	std::array<std::unique_ptr<Engine::Graphics2D::Sprite>, kCoinDigitCount> coinDigits_;
	std::array<std::unique_ptr<Engine::Graphics2D::Sprite>, kCoinDigitCount> killDigits_;
	static constexpr size_t kHudWeaponIconCount = 10;
	static constexpr size_t kHudPassiveItemSlotCount = 6;
	static constexpr size_t kHudBuildIconCount =
		kHudWeaponIconCount + kHudPassiveItemSlotCount;
	static constexpr size_t kHudMaxLevelPipsPerIcon = 8;
	std::array<std::unique_ptr<Engine::Graphics2D::Sprite>, kHudBuildIconCount> buildIcons_;
	std::array<UIPanel, kHudBuildIconCount * kHudMaxLevelPipsPerIcon> buildLevelPips_;
	std::vector<int32_t> weaponAcquisitionOrder_{ 0 };
	std::array<bool, kHudWeaponIconCount> weaponAcquisitionRecorded_{
		true, false, false, false, false, false, false, false, false, false };
	std::array<PassiveItemType, kHudPassiveItemSlotCount> displayedBuildItemTypes_{
		PassiveItemType::Count, PassiveItemType::Count,
		PassiveItemType::Count, PassiveItemType::Count,
		PassiveItemType::Count, PassiveItemType::Count };
	Vector2 coinDigitPosition_{ 70.0f, 58.0f };
	Vector2 killDigitPosition_{ 150.0f, 58.0f };
	Vector2 coinDigitSize_{ 14.0f, 20.0f };
	Vector2 gameplayMiniMapPosition_{ 1102.0f, 96.0f };
	Vector2 bossHpPosition_{ 340.0f, 118.0f };
	Vector2 bossHpSize_{ 600.0f, 24.0f };
	float gameplayMiniMapScale_ = 0.30f;
	bool layoutDebugEnabled_ = false;
	int32_t previousHp_ = 0;
	float displayedBossHp_ = 0.0f;
	int32_t bossHpTarget_ = 0;
	int32_t bossMaxHp_ = 1;
	bool bossHpVisible_ = false;
	float hitFlashTimer_ = 0.0f;
	float animationTime_ = 0.0f;
	GameInputBindings::NavigationInputDevice deathPromptDevice_ =
		GameInputBindings::NavigationInputDevice::Keyboard;
};

}
