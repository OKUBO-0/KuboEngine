#pragma once

#include "game/directxgame/ui/common/UILabel.h"
#include "game/directxgame/ui/gauge/ExpGauge.h"
#include "game/directxgame/ui/gauge/HpGauge.h"
#include "game/directxgame/ui/hud/KeyUI.h"
#include "game/directxgame/ui/hud/MiniMap.h"
#include "game/directxgame/ui/hud/Timer.h"
#include "Sprite.h"
#include "game/directxgame/core/GameTextureCache.h"
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

	float GetAnimationTime() const { return animationTime_; }
	float GetHitFlashTimer() const { return hitFlashTimer_; }
	Timer& GetTimer() { return timer_; }
	HpGauge& GetHpGauge() { return hpGauge_; }
	ExpGauge& GetExpGauge() { return expGauge_; }
	KeyUI& GetKeyUi() { return keyUi_; }
	MiniMap& GetPauseMiniMap() { return pauseMiniMap_; }

private:
	void UpdateCoinDisplay(int32_t runCoins);
	void DrawCoinDisplay();

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
	Vector2 coinDigitPosition_{ 52.0f, 612.0f };
	Vector2 coinDigitSize_{ 20.0f, 26.0f };
	int32_t previousHp_ = 0;
	float hitFlashTimer_ = 0.0f;
	float animationTime_ = 0.0f;
};

}
