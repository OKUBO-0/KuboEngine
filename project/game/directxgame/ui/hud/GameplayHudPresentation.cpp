#include "game/directxgame/ui/hud/GameplayHudPresentation.h"
#include "Input.h"
#include "game/directxgame/core/GameplayFlowController.h"
#include "game/directxgame/enemy/EnemyManager.h"
#include "game/directxgame/player/Player.h"
#include "game/directxgame/player/PlayerManager.h"
#include <algorithm>

namespace {

float Clamp01(float value)
{
	return std::clamp(value, 0.0f, 1.0f);
}

}

namespace DirectXGame {

void GameplayHudPresentation::Initialize(
	const PlayerManager* playerManager)
{
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
	float deathOverlayAlpha)
{
	animationTime_ += deltaTime;

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
	if (flow.Is(GameplayState::Paused) && player && enemyManager) {
		pauseMiniMap_.Update(player, *enemyManager);
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
		gameplayMiniMap_.Draw();
		keyUi_.Draw();
	}
	if (flow.Is(GameplayState::Start) && flow.IsIntroFinished()) {
		startOverlay_.Draw();
	}
	hitFlashOverlay_.Draw();
	deathOverlay_.Draw();
}

void GameplayHudPresentation::DrawPauseMap()
{
	pauseMiniMap_.Draw();
}

void GameplayHudPresentation::TriggerHitFlash(float duration)
{
	hitFlashTimer_ = (std::max)(hitFlashTimer_, duration);
}

}
