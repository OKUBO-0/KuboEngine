#pragma once

#include "UILabel.h"
#include "ExpGauge.h"
#include "HpGauge.h"
#include "KeyUI.h"
#include "MiniMap.h"
#include "Timer.h"
#include "Sprite.h"
#include "GameTextureCache.h"
#include <array>
#include <memory>

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
	void DrawCoinDisplay();
	void DrawKillDisplay();
	void ApplyCounterLayout();
	void ApplyGameplayMiniMapLayout();

	Timer timer_;
	HpGauge hpGauge_;
	ExpGauge expGauge_;
	KeyUI keyUi_;
	MiniMap pauseMiniMap_;
	MiniMap gameplayMiniMap_;
	UILabel startOverlay_;
	UILabel hitFlashOverlay_;
	UILabel deathOverlay_;
	TextureHandle coinDigitTexture_ = 0;
	static constexpr int32_t kCoinDigitCount = 6;
	std::array<std::unique_ptr<Engine::Graphics2D::Sprite>, kCoinDigitCount> coinDigits_;
	std::array<std::unique_ptr<Engine::Graphics2D::Sprite>, kCoinDigitCount> killDigits_;
	Vector2 coinDigitPosition_{ 70.0f, 58.0f };
	Vector2 killDigitPosition_{ 150.0f, 58.0f };
	Vector2 coinDigitSize_{ 14.0f, 20.0f };
	Vector2 gameplayMiniMapPosition_{ 1094.0f, 72.0f };
	float gameplayMiniMapScale_ = 0.21f;
	bool layoutDebugEnabled_ = false;
	int32_t previousHp_ = 0;
	float hitFlashTimer_ = 0.0f;
	float animationTime_ = 0.0f;
};

}
