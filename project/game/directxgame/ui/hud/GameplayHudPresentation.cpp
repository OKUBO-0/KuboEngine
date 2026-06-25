#include "game/directxgame/ui/hud/GameplayHudPresentation.h"
#include "Input.h"
#include "game/directxgame/core/GameplayFlowController.h"
#include "game/directxgame/enemy/EnemyManager.h"
#include "game/directxgame/core/GameTextureCache.h"
#include "game/directxgame/core/GameSpriteFactory.h"
#include "game/directxgame/player/Player.h"
#include "game/directxgame/player/PlayerManager.h"
#include "game/directxgame/ui/common/DigitSpriteUtil.h"
#include <algorithm>

namespace {

float Clamp01(float value)
{
	return std::clamp(value, 0.0f, 1.0f);
}

void PreloadGameplayHudTextures()
{
	DirectXGame::GameTextureCache::LoadBatch({
		"white1x1.png",
		"ui/number/numbers.png",
		"ui/number/colon.png",
		"ui/game/start.png",
		"ui/game/death.png",
		"ui/game/pause.png",
		"ui/game/pause_arrow.png",
		"ui/game/lv_label.png",
		"ui/game/minimap_player.png",
		"ui/game/minimap_enemy.png",
		"ui/game/minimap_orb.png",
		"ui/game/minimap_bg.png",
		"ui/controls/key_W.png",
		"ui/controls/key_a.png",
		"ui/controls/key_s.png",
		"ui/controls/key_d.png",
		"ui/controls/key_esc.png",
		"ui/game/normal/icon.png",
		"ui/game/orbit/icon.png",
		"ui/game/drone/icon.png",
		"ui/game/lightning/icon.png",
		"ui/game/lvup_attack_icon.png",
		});
}

}

namespace DirectXGame {

void GameplayHudPresentation::Initialize(
	const PlayerManager* playerManager)
{
	PreloadGameplayHudTextures();
	coinDigitTexture_ = GameTextureCache::Load("ui/number/numbers.png");

	timer_.Initialize();
	hpGauge_.Initialize();
	expGauge_.Initialize();
	keyUi_.Initialize();
	pauseMiniMap_.Initialize();
	gameplayMiniMap_.Initialize();
	gameplayMiniMap_.ConfigureAsScaledCopy(
		pauseMiniMap_,
		0.28f,
		{ 1062.0f, 506.0f },
		true);
	gameplayMiniMap_.SetIconSizeMultiplier(1.75f);

	startOverlay_.Initialize("ui/game/start.png", { 0.0f, 0.0f });
	startOverlay_.SetSize({ 1280.0f, 720.0f });
	hitFlashOverlay_.Initialize("white1x1.png", { 0.0f, 0.0f });
	hitFlashOverlay_.SetSize({ 1280.0f, 720.0f });
	hitFlashOverlay_.SetColor({ 1.0f, 0.12f, 0.08f, 1.0f });
	hitFlashOverlay_.SetAlpha(0.0f);
	hitFlashOverlay_.SetVisible(false);
	deathOverlay_.Initialize("ui/game/death.png", { 0.0f, 0.0f });
	deathOverlay_.SetSize({ 1280.0f, 720.0f });
	deathOverlay_.SetAlpha(0.0f);
	deathOverlay_.SetVisible(false);
	for (int32_t index = 0; index < kCoinDigitCount; ++index) {
		coinDigits_[index] = GameSpriteFactory::Create(
			coinDigitTexture_,
			{ coinDigitPosition_.x + coinDigitSize_.x * static_cast<float>(index),
				coinDigitPosition_.y });
		coinDigits_[index]->SetSize(coinDigitSize_);
		coinDigits_[index]->SetTextureSize({ 24.0f, 32.0f });
	}

	hpGauge_.SetHP(
		playerManager ? playerManager->GetHP() : 1,
		playerManager ? playerManager->GetMaxHP() : 1);
	expGauge_.SetEXP(
		playerManager ? playerManager->GetEXP() : 0,
		playerManager ? playerManager->GetNextLevelEXP() : 1);
	expGauge_.SetLevel(playerManager ? playerManager->GetLevel() : 1);
	previousHp_ = playerManager ? playerManager->GetHP() : 0;
}

void GameplayHudPresentation::Update(
	float deltaTime,
	bool gameplayFrozen,
	const GameplayFlowController& flow,
	Player* player,
	PlayerManager* playerManager,
	EnemyManager* enemyManager,
	Engine::InputSystem::Input* input,
	float deathOverlayAlpha,
	int32_t runCoins)
{
	animationTime_ += deltaTime;
	UpdateCoinDisplay(runCoins);

	if (playerManager) {
		const int32_t currentHp = playerManager->GetHP();
		if (currentHp < previousHp_) {
			TriggerHitFlash(0.28f);
		}
		previousHp_ = currentHp;
		hpGauge_.SetHP(
			playerManager->GetHP(),
			playerManager->GetMaxHP());
		if (flow.Is(GameplayState::LevelUp)) {
			expGauge_.SetEXP(
				playerManager->GetNextLevelEXP(),
				playerManager->GetNextLevelEXP());
			expGauge_.SetLevel(
				(std::max)(1, playerManager->GetLevel() - 1));
		} else {
			expGauge_.SetEXP(
				playerManager->GetEXP(),
				playerManager->GetNextLevelEXP());
			expGauge_.SetLevel(playerManager->GetLevel());
		}
	}

	expGauge_.SetLevelUpSelectionActive(
		flow.Is(GameplayState::LevelUp));
	if (hitFlashTimer_ > 0.0f) {
		hitFlashTimer_ =
			(std::max)(0.0f, hitFlashTimer_ - deltaTime);
		hitFlashOverlay_.SetVisible(true);
		hitFlashOverlay_.SetAlpha(
			0.36f * Clamp01(hitFlashTimer_ / 0.28f));
	} else {
		hitFlashOverlay_.SetVisible(false);
	}
	if (flow.Is(GameplayState::Dead)) {
		deathOverlay_.SetVisible(true);
		deathOverlay_.SetAlpha(deathOverlayAlpha);
	} else {
		deathOverlay_.SetVisible(false);
	}

	hpGauge_.Update();
	expGauge_.Update();
	if (flow.IsCursorHidden()) {
		keyUi_.Update(input);
	}
	if (flow.IsCursorHidden() && player && enemyManager) {
		gameplayMiniMap_.Update(player, *enemyManager);
	}
	if (flow.Is(GameplayState::Playing) && !gameplayFrozen) {
		timer_.Update(deltaTime);
	}
}

void GameplayHudPresentation::Draw(
	const GameplayFlowController& flow)
{
	timer_.Draw();
	hpGauge_.Draw();
	expGauge_.Draw();
	if (flow.IsWorldHudVisible()) {
		DrawCoinDisplay();
		gameplayMiniMap_.Draw();
		keyUi_.Draw();
	}
	if (flow.Is(GameplayState::Start) && flow.IsIntroFinished()) {
		startOverlay_.Draw();
	}
	hitFlashOverlay_.Draw();
	deathOverlay_.Draw();
}

void GameplayHudPresentation::TriggerHitFlash(float duration)
{
	hitFlashTimer_ = (std::max)(hitFlashTimer_, duration);
}

void GameplayHudPresentation::UpdateCoinDisplay(int32_t runCoins)
{
	const int32_t clampedCoins = std::clamp(runCoins, 0, 999999);
	int32_t divisor = 100000;
	bool nonZeroSeen = false;
	for (int32_t index = 0; index < kCoinDigitCount; ++index) {
		if (!coinDigits_[index]) {
			divisor /= 10;
			continue;
		}
		const int32_t digit = divisor > 0 ? (clampedCoins / divisor) % 10 : 0;
		nonZeroSeen = nonZeroSeen || digit > 0 || index == kCoinDigitCount - 1;
		DigitSpriteUtil::SetDigitSprite(
			*coinDigits_[index],
			24.0f,
			{ 24.0f, 32.0f },
			digit);
		coinDigits_[index]->SetPosition({
			coinDigitPosition_.x + coinDigitSize_.x * static_cast<float>(index),
			coinDigitPosition_.y });
		coinDigits_[index]->SetSize(coinDigitSize_);
		coinDigits_[index]->SetColor({
			1.0f,
			0.86f,
			0.22f,
			nonZeroSeen ? 1.0f : 0.0f });
		divisor /= 10;
	}
}

void GameplayHudPresentation::DrawCoinDisplay()
{
	for (const std::unique_ptr<Engine::Graphics2D::Sprite>& digit : coinDigits_) {
		if (!digit) {
			continue;
		}
		digit->Update();
		digit->Draw();
	}
}

}
