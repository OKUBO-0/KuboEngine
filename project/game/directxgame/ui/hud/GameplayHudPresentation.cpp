#include "GameplayHudPresentation.h"
#include "Input.h"
#include "GameplayFlowController.h"
#include "EnemyManager.h"
#include "GameTextureCache.h"
#include "GameSpriteFactory.h"
#include "Player.h"
#include "PlayerManager.h"
#include "DigitSpriteUtil.h"
#include "DataPaths.h"
#include "UILayoutIO.h"
#include <algorithm>
#ifdef _DEBUG
#include <imgui.h>
#endif

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
		"ui/game/left_cursor.png",
		"ui/game/right_corsor.png",
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
		"ui/game/lvup/normal_icon.png",
		"ui/game/lvup/orbit_icon.png",
		"ui/game/lvup/drone_icon.png",
		"ui/game/lvup/lightning_icon.png",
		"ui/game/lvup/attack_icon.png",
		});
}

}

namespace DirectXGame {

void GameplayHudPresentation::Initialize(
	const PlayerManager* playerManager)
{
	PreloadGameplayHudTextures();
	coinDigitTexture_ = GameTextureCache::Load("ui/number/numbers.png");
	const UILayoutIO::LayoutMap layout = UILayoutIO::LoadOrDefault(DataPaths::kHudLayout, {});
	coinDigitPosition_ = UILayoutIO::GetVector2(layout, "coinDigitPosition", coinDigitPosition_);
	killDigitPosition_ = UILayoutIO::GetVector2(layout, "killDigitPosition", killDigitPosition_);
	coinDigitSize_ = UILayoutIO::GetVector2(layout, "counterDigitSize", coinDigitSize_);
	gameplayMiniMapPosition_ = UILayoutIO::GetVector2(
		layout, "gameplayMiniMapPosition", gameplayMiniMapPosition_);
	gameplayMiniMapScale_ = UILayoutIO::GetFloat(
		layout, "gameplayMiniMapScale", gameplayMiniMapScale_);

	timer_.Initialize();
	hpGauge_.Initialize();
	expGauge_.Initialize();
	keyUi_.Initialize();
	pauseMiniMap_.Initialize();
	gameplayMiniMap_.Initialize();
	ApplyGameplayMiniMapLayout();

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
		killDigits_[index] = GameSpriteFactory::Create(
			coinDigitTexture_,
			{ killDigitPosition_.x + coinDigitSize_.x * static_cast<float>(index),
				killDigitPosition_.y });
		killDigits_[index]->SetSize(coinDigitSize_);
		killDigits_[index]->SetTextureSize({ 24.0f, 32.0f });
	}
	ApplyCounterLayout();

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
	UpdateKillDisplay(enemyManager ? enemyManager->GetTotalKillCount() : 0);

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
	hpGauge_.Draw();
	expGauge_.Draw();
	timer_.Draw();
	const bool showRunCounters =
		flow.IsWorldHudVisible() ||
		flow.Is(GameplayState::Paused);
	if (showRunCounters) {
		DrawCoinDisplay();
		DrawKillDisplay();
	}
	if (flow.IsWorldHudVisible()) {
		if (!flow.Is(GameplayState::Paused)) {
			gameplayMiniMap_.Draw();
		}
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

void GameplayHudPresentation::UpdateKillDisplay(int32_t killCount)
{
	const int32_t clampedKills = std::clamp(killCount, 0, 999999);
	int32_t divisor = 100000;
	bool nonZeroSeen = false;
	for (int32_t index = 0; index < kCoinDigitCount; ++index) {
		if (!killDigits_[index]) {
			divisor /= 10;
			continue;
		}
		const int32_t digit = divisor > 0 ? (clampedKills / divisor) % 10 : 0;
		nonZeroSeen = nonZeroSeen || digit > 0 || index == kCoinDigitCount - 1;
		DigitSpriteUtil::SetDigitSprite(
			*killDigits_[index],
			24.0f,
			{ 24.0f, 32.0f },
			digit);
		killDigits_[index]->SetPosition({
			killDigitPosition_.x + coinDigitSize_.x * static_cast<float>(index),
			killDigitPosition_.y });
		killDigits_[index]->SetSize(coinDigitSize_);
		killDigits_[index]->SetColor({
			1.0f,
			0.38f,
			0.28f,
			nonZeroSeen ? 1.0f : 0.0f });
		divisor /= 10;
	}
}

void GameplayHudPresentation::DrawKillDisplay()
{
	for (const std::unique_ptr<Engine::Graphics2D::Sprite>& digit : killDigits_) {
		if (!digit) {
			continue;
		}
		digit->Update();
		digit->Draw();
	}
}

void GameplayHudPresentation::ApplyCounterLayout()
{
	for (int32_t index = 0; index < kCoinDigitCount; ++index) {
		const float offsetX = coinDigitSize_.x * static_cast<float>(index);
		if (coinDigits_[index]) {
			coinDigits_[index]->SetPosition({ coinDigitPosition_.x + offsetX, coinDigitPosition_.y });
			coinDigits_[index]->SetSize(coinDigitSize_);
		}
		if (killDigits_[index]) {
			killDigits_[index]->SetPosition({ killDigitPosition_.x + offsetX, killDigitPosition_.y });
			killDigits_[index]->SetSize(coinDigitSize_);
		}
	}
}

void GameplayHudPresentation::ApplyGameplayMiniMapLayout()
{
	gameplayMiniMap_.ConfigureAsScaledCopy(
		pauseMiniMap_, gameplayMiniMapScale_, gameplayMiniMapPosition_, true);
	gameplayMiniMap_.SetIconSizeMultiplier(1.45f);
}

void GameplayHudPresentation::DebugDrawImGui()
{
#ifdef _DEBUG
	if (!ImGui::CollapsingHeader("HUD Counters / Gameplay Mini Map")) {
		return;
	}

	ImGui::Checkbox("Enable HUD Debug##GameplayHud", &layoutDebugEnabled_);
	if (!layoutDebugEnabled_) {
		return;
	}

	bool countersChanged = false;
	float coinPosition[2]{ coinDigitPosition_.x, coinDigitPosition_.y };
	if (ImGui::DragFloat2("Coin Position", coinPosition, 1.0f, -400.0f, 1280.0f)) {
		coinDigitPosition_ = { coinPosition[0], coinPosition[1] };
		countersChanged = true;
	}
	float killPosition[2]{ killDigitPosition_.x, killDigitPosition_.y };
	if (ImGui::DragFloat2("Kill Position", killPosition, 1.0f, -400.0f, 1280.0f)) {
		killDigitPosition_ = { killPosition[0], killPosition[1] };
		countersChanged = true;
	}
	float digitSize[2]{ coinDigitSize_.x, coinDigitSize_.y };
	if (ImGui::DragFloat2("Counter Digit Size", digitSize, 1.0f, 4.0f, 64.0f)) {
		coinDigitSize_ = { digitSize[0], digitSize[1] };
		countersChanged = true;
	}
	if (countersChanged) {
		ApplyCounterLayout();
	}

	bool miniMapChanged = false;
	float miniMapPosition[2]{ gameplayMiniMapPosition_.x, gameplayMiniMapPosition_.y };
	if (ImGui::DragFloat2(
		"Gameplay MiniMap Position", miniMapPosition, 1.0f, -400.0f, 1280.0f)) {
		gameplayMiniMapPosition_ = { miniMapPosition[0], miniMapPosition[1] };
		miniMapChanged = true;
	}
	miniMapChanged |= ImGui::DragFloat(
		"Gameplay MiniMap Scale", &gameplayMiniMapScale_, 0.01f, 0.05f, 1.0f);
	if (miniMapChanged) {
		ApplyGameplayMiniMapLayout();
	}

	if (ImGui::Button("Save Gameplay HUD Layout")) {
		SaveLayout();
	}
#endif
}

void GameplayHudPresentation::SaveLayout() const
{
	UILayoutIO::Save(DataPaths::kHudLayout,
		{
			{ "coinDigitPosition", { coinDigitPosition_.x, coinDigitPosition_.y } },
			{ "killDigitPosition", { killDigitPosition_.x, killDigitPosition_.y } },
			{ "counterDigitSize", { coinDigitSize_.x, coinDigitSize_.y } },
			{ "gameplayMiniMapPosition", { gameplayMiniMapPosition_.x, gameplayMiniMapPosition_.y } },
			{ "gameplayMiniMapScale", { gameplayMiniMapScale_ } },
		});
}

}
