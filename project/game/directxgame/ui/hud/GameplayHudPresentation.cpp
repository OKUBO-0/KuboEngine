#include "GameplayHudPresentation.h"
#include "Input.h"
#include "GameplayFlowController.h"
#include "EnemyManager.h"
#include "GameTextureCache.h"
#include "GameSpriteFactory.h"
#include "Player.h"
#include "PlayerManager.h"
#include "PassiveItemData.h"
#include "DigitSpriteUtil.h"
#include "DataPaths.h"
#include "UILayoutIO.h"
#include <algorithm>
#include <cmath>
#include <string>
#ifdef _DEBUG
#include <imgui.h>
#endif

namespace {

constexpr float kIntroBarHeight = 56.0f;
constexpr float kBossHpFrameBorder = 3.0f;
constexpr Vector4 kHudPanelColor{ 0.0f, 0.0f, 0.0f, 0.74f };
constexpr Vector4 kHudFrameColor{ 1.0f, 0.96f, 0.0f, 1.0f };

float Clamp01(float value)
{
	return std::clamp(value, 0.0f, 1.0f);
}

void PreloadGameplayHudTextures()
{
	DirectXGame::GameTextureCache::LoadBatch({
		"white1x1.png",
		"ui/game/pause.png",
		"ui/game/left_cursor.png",
		"ui/game/right_corsor.png",
		"ui/game/minimap_player.png",
		"ui/game/minimap_enemy.png",
		"ui/game/minimap_orb.png",
		"ui/game/minimap_bg.png",
		"ui/controls/key_W.png",
		"ui/controls/key_a.png",
		"ui/controls/key_s.png",
		"ui/controls/key_d.png",
		"ui/controls/key_esc.png",
		"ui/game/lvup/icon_common_unknown.png",
		"ui/game/lvup/icon_passive_scroll.png",
		"ui/game/lvup/icon_weapon_bow_arrow.png",
		"ui/game/lvup/icon_weapon_rock.png",
		"ui/game/lvup/icon_weapon_thunder_staff.png",
		"ui/game/lvup/icon_weapon_flame_staff.png",
		"ui/game/lvup/icon_weapon_sword.png",
		"ui/game/lvup/icon_weapon_aura.png",
		"ui/game/lvup/icon_stat_fire.png",
		"ui/game/lvup/icon_weapon_bone.png",
		"ui/game/lvup/icon_weapon_handgun.png",
		"ui/game/lvup/icon_weapon_boomerang.png",
		"ui/game/lvup/icon_stat_coin_gain.png",
		"ui/game/lvup/icon_stat_crit_damage.png",
		"ui/game/lvup/lightning_icon.png",
		"ui/game/lvup/attack_icon.png",
		});
}

void SetPanelLayout(DirectXGame::UIPanel& panel, const Vector2& position, const Vector2& size, const Vector4& color)
{
	panel.SetPosition(position);
	panel.SetSize(size);
	panel.SetColor(color);
}

template <size_t N>
void SetBorderLayout(
	std::array<DirectXGame::UIPanel, N>& borders,
	size_t offset,
	const Vector2& position,
	const Vector2& size,
	float thickness,
	const Vector4& color)
{
	SetPanelLayout(borders[offset + 0], position, { size.x, thickness }, color);
	SetPanelLayout(borders[offset + 1], { position.x, position.y + size.y - thickness }, { size.x, thickness }, color);
	SetPanelLayout(borders[offset + 2], position, { thickness, size.y }, color);
	SetPanelLayout(borders[offset + 3], { position.x + size.x - thickness, position.y }, { thickness, size.y }, color);
}

const char* HudWeaponIconPath(size_t index)
{
	const std::array<const char*, 10> paths{
		"ui/game/lvup/icon_weapon_bow_arrow.png",
		"ui/game/lvup/icon_weapon_rock.png",
		"ui/game/lvup/icon_weapon_thunder_staff.png",
		"ui/game/lvup/icon_weapon_flame_staff.png",
		"ui/game/lvup/icon_weapon_sword.png",
		"ui/game/lvup/icon_weapon_aura.png",
		"ui/game/lvup/icon_stat_fire.png",
		"ui/game/lvup/icon_weapon_bone.png",
		"ui/game/lvup/icon_weapon_handgun.png",
		"ui/game/lvup/icon_weapon_boomerang.png",
	};
	return paths[(std::min)(index, paths.size() - 1)];
}

const char* HudPassiveSubIconPath(DirectXGame::PassiveItemType type)
{
	switch (type) {
	case DirectXGame::PassiveItemType::Damage:
		return "ui/game/lvup/icon_stat_damage.png";
	case DirectXGame::PassiveItemType::MaxHp:
		return "ui/game/lvup/icon_stat_maxhp.png";
	case DirectXGame::PassiveItemType::MoveSpeed:
		return "ui/game/lvup/icon_stat_movespeed.png";
	case DirectXGame::PassiveItemType::AttackSpeed:
		return "ui/game/lvup/icon_stat_attackspeed.png";
	case DirectXGame::PassiveItemType::Duration:
		return "ui/game/lvup/icon_stat_duration.png";
	case DirectXGame::PassiveItemType::AreaSize:
		return "ui/game/lvup/icon_stat_area.png";
	case DirectXGame::PassiveItemType::ProjectileSpeed:
		return "ui/game/lvup/icon_stat_projectile_speed.png";
	case DirectXGame::PassiveItemType::ProjectileCount:
		return "ui/game/lvup/icon_stat_projectile_count.png";
	case DirectXGame::PassiveItemType::PickupRange:
		return "ui/game/lvup/icon_stat_pickup_range.png";
	case DirectXGame::PassiveItemType::ExpGain:
		return "ui/game/lvup/icon_stat_exp_gain.png";
	case DirectXGame::PassiveItemType::CoinGain:
		return "ui/game/lvup/icon_stat_coin_gain.png";
	case DirectXGame::PassiveItemType::CritChance:
		return "ui/game/lvup/icon_stat_crit_chance.png";
	case DirectXGame::PassiveItemType::CritDamage:
		return "ui/game/lvup/icon_stat_crit_damage.png";
	case DirectXGame::PassiveItemType::Armor:
		return "ui/game/lvup/icon_stat_armor.png";
	case DirectXGame::PassiveItemType::Evasion:
		return "ui/game/lvup/icon_stat_evasion.png";
	case DirectXGame::PassiveItemType::HpRegen:
		return "ui/game/lvup/icon_stat_hp_regen.png";
	case DirectXGame::PassiveItemType::LifeSteal:
		return "ui/game/lvup/icon_stat_lifesteal.png";
	case DirectXGame::PassiveItemType::Knockback:
		return "ui/game/lvup/icon_stat_knockback.png";
	default:
		return "ui/game/lvup/icon_stat_damage.png";
	}
}

const char* DeathPromptText(DirectXGame::GameInputBindings::NavigationInputDevice device)
{
	return device == DirectXGame::GameInputBindings::NavigationInputDevice::Gamepad
		? "--Press Any Button--"
		: "--Press Any Click--";
}

void SetCenteredText(DirectXGame::BitmapText& text, const char* value, float y, float scale, float maxWidth, const Vector4& color)
{
	text.SetText(value);
	text.SetScaleToFit(scale, maxWidth);
	text.SetPosition({ (1280.0f - text.MeasureWidth()) * 0.5f, y });
	text.SetColor(color);
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
	bossHpPosition_ = UILayoutIO::GetVector2(
		layout, "bossHpPosition", bossHpPosition_);
	bossHpSize_ = UILayoutIO::GetVector2(
		layout, "bossHpSize", bossHpSize_);

	timer_.Initialize();
	hpGauge_.Initialize();
	expGauge_.Initialize();
	keyUi_.Initialize();
	pauseMiniMap_.Initialize();
	gameplayMiniMap_.Initialize();
	ApplyGameplayMiniMapLayout();

	introTopBar_.Initialize("white1x1.png", { 0.0f, 0.0f });
	introTopBar_.SetSize({ 1280.0f, kIntroBarHeight });
	introTopBar_.SetColor({ 0.0f, 0.0f, 0.0f, 1.0f });
	introBottomBar_.Initialize(
		"white1x1.png", { 0.0f, 720.0f - kIntroBarHeight });
	introBottomBar_.SetSize({ 1280.0f, kIntroBarHeight });
	introBottomBar_.SetColor({ 0.0f, 0.0f, 0.0f, 1.0f });
	hitFlashOverlay_.Initialize("white1x1.png", { 0.0f, 0.0f });
	hitFlashOverlay_.SetSize({ 1280.0f, 720.0f });
	hitFlashOverlay_.SetColor({ 1.0f, 0.12f, 0.08f, 1.0f });
	hitFlashOverlay_.SetAlpha(0.0f);
	hitFlashOverlay_.SetVisible(false);
	deathGameOverText_.Initialize(
		"ui/font/noto_sans_jp_black.png",
		"ui/font/noto_sans_jp_black.json");
	deathPromptText_.Initialize(
		"ui/font/noto_sans_jp_black.png",
		"ui/font/noto_sans_jp_black.json");
	bossHpFrame_.Initialize("white1x1.png", bossHpPosition_);
	bossHpFrame_.SetColor({ 0.92f, 0.90f, 0.86f, 0.96f });
	bossHpFrame_.SetVisible(false);
	bossHpBackground_.Initialize("white1x1.png", bossHpPosition_);
	bossHpBackground_.SetColor({ 0.06f, 0.02f, 0.02f, 0.92f });
	bossHpBackground_.SetVisible(false);
	bossHpFill_.Initialize("white1x1.png", bossHpPosition_);
	bossHpFill_.SetColor({ 1.0f, 0.0f, 0.0f, 0.96f });
	bossHpFill_.SetVisible(false);
	bossHpText_.Initialize(
		"ui/font/noto_sans_jp_black.png",
		"ui/font/noto_sans_jp_black.json");
	bossHpText_.SetScale(0.28f);
	bossHpText_.SetAdvanceMultiplier(1.0f);
	bossHpText_.SetColor({ 1.0f, 1.0f, 1.0f, 0.98f });
	escPanel_.Initialize();
	for (UIPanel& border : escPanelBorders_) {
		border.Initialize();
	}
	coinPanel_.Initialize();
	killPanel_.Initialize();
	for (UIPanel& border : counterPanelBorders_) {
		border.Initialize();
	}
	coinIcon_.Initialize("ui/game/lvup/icon_stat_coin_gain.png", { 0.0f, 0.0f });
	coinIcon_.SetSize({ 26.0f, 26.0f });
	killIcon_.Initialize("ui/game/lvup/icon_stat_crit_damage.png", { 0.0f, 0.0f });
	killIcon_.SetSize({ 26.0f, 26.0f });
	escText_.Initialize(
		"ui/font/noto_sans_jp_black.png",
		"ui/font/noto_sans_jp_black.json");
	escText_.SetText("ESC");
	escText_.SetScale(0.22f);
	escText_.SetAdvanceMultiplier(0.92f);
	escText_.SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
	ApplyBossHpBarLayout();
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
	for (size_t index = 0; index < buildIcons_.size(); ++index) {
		const char* iconPath = index < kHudWeaponIconCount
			? HudWeaponIconPath(index)
			: "ui/game/lvup/icon_passive_scroll.png";
		buildIcons_[index] = GameSpriteFactory::Create(iconPath, {});
		buildIcons_[index]->SetTextureLeftTop({ 0.0f, 0.0f });
		buildIcons_[index]->SetTextureSize({ 512.0f, 512.0f });
		buildIcons_[index]->SetSize({ 26.0f, 26.0f });
		buildIcons_[index]->SetColor({ 1.0f, 1.0f, 1.0f, 0.0f });
	}
	for (auto& subIcon : buildItemSubIcons_) {
		subIcon = GameSpriteFactory::Create("ui/game/lvup/icon_stat_damage.png", {});
		subIcon->SetTextureLeftTop({ 0.0f, 0.0f });
		subIcon->SetTextureSize({ 512.0f, 512.0f });
		subIcon->SetSize({ 12.0f, 12.0f });
		subIcon->SetColor({ 1.0f, 1.0f, 1.0f, 0.0f });
	}
	for (UIPanel& pip : buildLevelPips_) {
		pip.Initialize();
		pip.SetColor({ 0.0f, 0.0f, 0.0f, 0.0f });
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
	UpdateKillDisplay(enemyManager ? enemyManager->GetTotalKillCount() : 0);
	UpdateBuildStrip(playerManager);

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
	if (input) {
		deathPromptDevice_ =
			GameInputBindings::DetectNavigationInputDevice(input, deathPromptDevice_);
	}
	if (flow.Is(GameplayState::Dead)) {
		const float wipeProgress = Clamp01(deathOverlayAlpha / 0.65f);
		const float textAlpha = Clamp01((wipeProgress - 0.72f) / 0.18f);
		SetCenteredText(
			deathGameOverText_,
			"GAME OVER",
			72.0f,
			0.74f,
			1120.0f,
			{ 1.0f, 0.96f, 0.0f, textAlpha });
		SetCenteredText(
			deathPromptText_,
			DeathPromptText(deathPromptDevice_),
			620.0f,
			0.34f,
			1040.0f,
			{ 1.0f, 1.0f, 1.0f, textAlpha });
	} else {
		deathGameOverText_.SetText("");
		deathPromptText_.SetText("");
	}

	hpGauge_.Update();
	expGauge_.Update();
	UpdateBossHpBar(deltaTime, flow, enemyManager);
	if (flow.IsCursorHidden() || flow.Is(GameplayState::LevelUp)) {
		keyUi_.Update(input);
	}
	if ((flow.IsCursorHidden() || flow.Is(GameplayState::LevelUp)) &&
		player && enemyManager) {
		gameplayMiniMap_.Update(player, *enemyManager);
	}
	if (flow.Is(GameplayState::Playing) && !gameplayFrozen) {
		timer_.Update(deltaTime);
	}
}

void GameplayHudPresentation::Draw(
	const GameplayFlowController& flow)
{
	if (flow.Is(GameplayState::Start)) {
		const float progress = Clamp01(
			flow.GetIntroElapsed() / GameplayFlowController::kIntroDuration);
		const float exitProgress = Clamp01((progress - 0.78f) / 0.22f);
		const float easedExit = exitProgress * exitProgress *
			(3.0f - 2.0f * exitProgress);
		introTopBar_.SetPosition({ 0.0f, -kIntroBarHeight * easedExit });
		introBottomBar_.SetPosition({
			0.0f,
			720.0f - kIntroBarHeight + kIntroBarHeight * easedExit,
			});
		introTopBar_.Draw();
		introBottomBar_.Draw();
		return;
	}
	if (!flow.Is(GameplayState::Paused)) {
		hpGauge_.Draw();
		expGauge_.Draw();
		timer_.Draw();
		DrawBossHpBar();
	}
	const bool showRunCounters =
		flow.IsWorldHudVisible() ||
		flow.Is(GameplayState::LevelUp);
	if (showRunCounters) {
		DrawTopHud();
	}
	if (flow.IsWorldHudVisible()) {
		if (!flow.Is(GameplayState::Paused)) {
			gameplayMiniMap_.Draw();
		}
		keyUi_.Draw();
		DrawBuildStrip();
	} else if (flow.Is(GameplayState::LevelUp)) {
		gameplayMiniMap_.Draw();
		keyUi_.Draw();
		DrawBuildStrip();
	}
	hitFlashOverlay_.Draw();
	if (flow.Is(GameplayState::Dead)) {
		DrawDeathForeground(flow);
	}
	if (flow.Is(GameplayState::BossIntro)) {
		const float progress = Clamp01(
			flow.GetBossIntroElapsed() /
			GameplayFlowController::kBossIntroDuration);
		const float enterProgress = Clamp01(progress / 0.16f);
		const float exitProgress = Clamp01((progress - 0.78f) / 0.22f);
		const float easedEnter = enterProgress * enterProgress *
			(3.0f - 2.0f * enterProgress);
		const float easedExit = exitProgress * exitProgress *
			(3.0f - 2.0f * exitProgress);
		introTopBar_.SetPosition({
			0.0f,
			-kIntroBarHeight + kIntroBarHeight * easedEnter -
				kIntroBarHeight * easedExit,
			});
		introBottomBar_.SetPosition({
			0.0f,
			720.0f - kIntroBarHeight * easedEnter +
				kIntroBarHeight * easedExit,
			});
		introTopBar_.Draw();
		introBottomBar_.Draw();
	}
}

void GameplayHudPresentation::DrawDeathForeground(
	const GameplayFlowController& flow)
{
	if (!flow.Is(GameplayState::Dead)) {
		return;
	}
	deathGameOverText_.Draw();
	deathPromptText_.Draw();
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

void GameplayHudPresentation::UpdateBuildStrip(const PlayerManager* playerManager)
{
	for (UIPanel& pip : buildLevelPips_) {
		pip.SetColor({ 0.0f, 0.0f, 0.0f, 0.0f });
	}
	if (!playerManager) {
		for (auto& icon : buildIcons_) {
			if (icon) {
				icon->SetColor({ 1.0f, 1.0f, 1.0f, 0.0f });
			}
		}
		for (auto& subIcon : buildItemSubIcons_) {
			if (subIcon) {
				subIcon->SetColor({ 1.0f, 1.0f, 1.0f, 0.0f });
			}
		}
		return;
	}
	const std::array<bool, kHudWeaponIconCount> acquired{
		true,
		playerManager->HasOrbitBullets(),
		playerManager->HasLightning(),
		playerManager->HasExplosiveBullets(),
		playerManager->HasSword(),
		playerManager->HasAura(),
		playerManager->HasFlameShoes(),
		playerManager->HasBone(),
		playerManager->HasHandgun(),
		playerManager->HasBoomerang(),
	};
	for (size_t index = 1; index < acquired.size(); ++index) {
		if (acquired[index] && !weaponAcquisitionRecorded_[index]) {
			weaponAcquisitionRecorded_[index] = true;
			weaponAcquisitionOrder_.push_back(static_cast<int32_t>(index));
		}
	}
	for (auto& icon : buildIcons_) {
		if (icon) {
			icon->SetColor({ 1.0f, 1.0f, 1.0f, 0.0f });
		}
	}
	for (auto& subIcon : buildItemSubIcons_) {
		if (subIcon) {
			subIcon->SetColor({ 1.0f, 1.0f, 1.0f, 0.0f });
		}
	}
	auto setPips = [this](
		size_t slot,
		int32_t level,
		int32_t maxLevel,
		const Vector2& position,
		const Vector2& size) {
		const size_t base = slot * kHudMaxLevelPipsPerIcon;
		const int32_t clampedMax = std::clamp(maxLevel, 1, static_cast<int32_t>(kHudMaxLevelPipsPerIcon));
		const int32_t clampedLevel = std::clamp(level, 0, clampedMax);
		constexpr int32_t kPipsPerRow = 4;
		const float pipSize = (std::min)(5.0f, (size.x - 3.0f) / 4.0f);
		const float gap = 1.0f;
		const int32_t visibleColumns = (std::min)(kPipsPerRow, clampedMax);
		const float rowWidth =
			pipSize * static_cast<float>(visibleColumns) +
			gap * static_cast<float>((std::max)(0, visibleColumns - 1));
		const float startX = position.x + (size.x - rowWidth) * 0.5f;
		for (int32_t index = 0; index < clampedMax; ++index) {
			UIPanel& pip = buildLevelPips_[base + static_cast<size_t>(index)];
			const int32_t column = index % kPipsPerRow;
			const int32_t row = index / kPipsPerRow;
			pip.SetPosition({
				startX + (pipSize + gap) * static_cast<float>(column),
				position.y + size.y + 3.0f + (pipSize + gap) * static_cast<float>(row),
				});
			pip.SetSize({ pipSize, pipSize });
			pip.SetColor(index < clampedLevel
				? Vector4{ 1.0f, 0.92f, 0.06f, 1.0f }
				: Vector4{ 0.08f, 0.08f, 0.02f, 0.72f });
		}
	};
	const std::array<int32_t, kHudWeaponIconCount> weaponLevels{
		playerManager->GetNormalBulletLevel(),
		playerManager->GetOrbitBulletLevel(),
		playerManager->GetLightningLevel(),
		playerManager->GetExplosiveBulletLevel(),
		playerManager->GetSwordLevel(),
		playerManager->GetAuraLevel(),
		playerManager->GetFlameShoesLevel(),
		playerManager->GetBoneLevel(),
		playerManager->GetHandgunLevel(),
		playerManager->GetBoomerangLevel(),
	};
	const Vector2 iconSize{ 26.0f, 26.0f };
	for (size_t orderIndex = 0; orderIndex < weaponAcquisitionOrder_.size(); ++orderIndex) {
		const size_t iconIndex = static_cast<size_t>(weaponAcquisitionOrder_[orderIndex]);
		if (iconIndex >= kHudWeaponIconCount || !buildIcons_[iconIndex]) {
			continue;
		}
		const Vector2 position{
			12.0f + 33.0f * static_cast<float>(orderIndex),
			112.0f,
		};
		buildIcons_[iconIndex]->SetPosition(position);
		buildIcons_[iconIndex]->SetSize(iconSize);
		buildIcons_[iconIndex]->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
		setPips(iconIndex, weaponLevels[iconIndex], 8, position, iconSize);
	}

	const std::vector<PassiveItemType>& items =
		playerManager->GetPassiveItemAcquisitionOrder();
	for (size_t slot = 0; slot < kHudPassiveItemSlotCount; ++slot) {
		const size_t iconIndex = kHudWeaponIconCount + slot;
		if (slot >= items.size() || !buildIcons_[iconIndex]) {
			continue;
		}
		const PassiveItemType type = items[slot];
		auto& subIcon = buildItemSubIcons_[slot];
		if (displayedBuildItemTypes_[slot] != type) {
			const TextureHandle texture =
				GameTextureCache::Load("ui/game/lvup/icon_passive_scroll.png");
			buildIcons_[iconIndex]->SetTexture(GameTextureCache::GetPath(texture));
			buildIcons_[iconIndex]->SetTextureLeftTop({ 0.0f, 0.0f });
			buildIcons_[iconIndex]->SetTextureSize({ 512.0f, 512.0f });
			if (subIcon) {
				const TextureHandle subTexture =
					GameTextureCache::Load(HudPassiveSubIconPath(type));
				subIcon->SetTexture(GameTextureCache::GetPath(subTexture));
				subIcon->SetTextureLeftTop({ 0.0f, 0.0f });
				subIcon->SetTextureSize({ 512.0f, 512.0f });
			}
			displayedBuildItemTypes_[slot] = type;
		}
		const Vector2 position{
			12.0f + 33.0f * static_cast<float>(slot),
			158.0f,
		};
		buildIcons_[iconIndex]->SetPosition(position);
		buildIcons_[iconIndex]->SetSize(iconSize);
		buildIcons_[iconIndex]->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
		if (subIcon) {
			const Vector2 subSize{ 12.0f, 12.0f };
			subIcon->SetPosition({
				position.x + iconSize.x - subSize.x - 1.0f,
				position.y + iconSize.y - subSize.y - 1.0f,
				});
			subIcon->SetSize(subSize);
			subIcon->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
		}
		setPips(
			iconIndex,
			playerManager->GetPassiveItemLevel(type),
			playerManager->GetPassiveItemMaxLevel(type),
			position,
			iconSize);
	}
}

void GameplayHudPresentation::DrawTopHud()
{
	escPanel_.Draw();
	for (UIPanel& border : escPanelBorders_) {
		border.Draw();
	}
	escText_.Draw();
	coinPanel_.Draw();
	killPanel_.Draw();
	for (UIPanel& border : counterPanelBorders_) {
		border.Draw();
	}
	coinIcon_.Draw();
	killIcon_.Draw();
	DrawCoinDisplay();
	DrawKillDisplay();
}

void GameplayHudPresentation::DrawBuildStrip()
{
	for (const std::unique_ptr<Engine::Graphics2D::Sprite>& icon : buildIcons_) {
		if (!icon) {
			continue;
		}
		icon->Update();
		icon->Draw();
	}
	for (const std::unique_ptr<Engine::Graphics2D::Sprite>& subIcon : buildItemSubIcons_) {
		if (!subIcon) {
			continue;
		}
		subIcon->Update();
		subIcon->Draw();
	}
	for (UIPanel& pip : buildLevelPips_) {
		pip.Draw();
	}
}

void GameplayHudPresentation::UpdateBossHpBar(
	float deltaTime,
	const GameplayFlowController& flow,
	EnemyManager* enemyManager)
{
	const bool shouldShow =
		enemyManager &&
		enemyManager->HasActiveBoss() &&
		(flow.Is(GameplayState::BossIntro) ||
			flow.Is(GameplayState::Boss) ||
			flow.Is(GameplayState::BossDefeated));
	if (!shouldShow) {
		bossHpVisible_ = false;
		bossHpFrame_.SetVisible(false);
		bossHpBackground_.SetVisible(false);
		bossHpFill_.SetVisible(false);
		bossHpTarget_ = 0;
		displayedBossHp_ = 0.0f;
		bossHpText_.SetText("");
		return;
	}

	const int32_t currentHp = (std::max)(0, enemyManager->GetBossHP());
	bossMaxHp_ = (std::max)(1, enemyManager->GetBossMaxHP());
	if (!bossHpVisible_) {
		displayedBossHp_ = 0.0f;
	}
	bossHpVisible_ = true;
	bossHpTarget_ = currentHp;

	const float difference =
		static_cast<float>(bossHpTarget_) - displayedBossHp_;
	if (std::fabs(difference) <= 0.5f) {
		displayedBossHp_ = static_cast<float>(bossHpTarget_);
	} else {
		const float fillSpeed = static_cast<float>(bossMaxHp_) * 1.15f;
		const float step = (std::max)(
			std::fabs(difference) * 7.5f,
			fillSpeed) * deltaTime;
		displayedBossHp_ += std::clamp(difference, -step, step);
	}

	const float rate = Clamp01(displayedBossHp_ / static_cast<float>(bossMaxHp_));
	const Vector2 fillPosition{
		bossHpPosition_.x + kBossHpFrameBorder,
		bossHpPosition_.y + kBossHpFrameBorder,
	};
	const Vector2 innerSize{
		(std::max)(1.0f, bossHpSize_.x - kBossHpFrameBorder * 2.0f),
		(std::max)(1.0f, bossHpSize_.y - kBossHpFrameBorder * 2.0f),
	};
	bossHpFrame_.SetVisible(true);
	bossHpBackground_.SetVisible(true);
	bossHpFill_.SetVisible(rate > 0.0f);
	bossHpFill_.SetPosition(fillPosition);
	bossHpFill_.SetSize({ innerSize.x * rate, innerSize.y });

	const int32_t displayedHpInt = std::clamp(
		static_cast<int32_t>(std::round(displayedBossHp_)),
		0,
		bossMaxHp_);
	bossHpText_.SetText(
		std::to_string(displayedHpInt) + "/" + std::to_string(bossMaxHp_));
	bossHpText_.SetScaleToFit(0.28f, bossHpSize_.x - 24.0f);
	const float textWidth = bossHpText_.MeasureWidth();
	bossHpText_.SetPosition({
		bossHpPosition_.x + (bossHpSize_.x - textWidth) * 0.5f,
		bossHpPosition_.y + 1.0f,
		});
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

void GameplayHudPresentation::DrawBossHpBar()
{
	if (!bossHpVisible_) {
		return;
	}
	bossHpFrame_.Draw();
	bossHpBackground_.Draw();
	bossHpFill_.Draw();
	bossHpText_.Draw();
}

void GameplayHudPresentation::ApplyCounterLayout()
{
	const Vector2 escPosition{ 10.0f, 46.0f };
	const Vector2 escSize{ 72.0f, 38.0f };
	SetPanelLayout(escPanel_, escPosition, escSize, kHudPanelColor);
	SetBorderLayout(escPanelBorders_, 0, escPosition, escSize, 3.0f, kHudFrameColor);
	escText_.SetScaleToFit(0.22f, escSize.x - 16.0f);
	escText_.SetPosition({
		escPosition.x + (escSize.x - escText_.MeasureWidth()) * 0.5f,
		escPosition.y + 8.0f,
		});
	const Vector2 coinPanelPosition{ 90.0f, 46.0f };
	const Vector2 counterPanelSize{ 108.0f, 38.0f };
	const Vector2 killPanelPosition{ 206.0f, 46.0f };
	SetPanelLayout(coinPanel_, coinPanelPosition, counterPanelSize, kHudPanelColor);
	SetPanelLayout(killPanel_, killPanelPosition, counterPanelSize, kHudPanelColor);
	SetBorderLayout(counterPanelBorders_, 0, coinPanelPosition, counterPanelSize, 3.0f, kHudFrameColor);
	SetBorderLayout(counterPanelBorders_, 4, killPanelPosition, counterPanelSize, 3.0f, kHudFrameColor);
	coinIcon_.SetPosition({ coinPanelPosition.x + 8.0f, coinPanelPosition.y + 6.0f });
	coinIcon_.SetSize({ 26.0f, 26.0f });
	killIcon_.SetPosition({ killPanelPosition.x + 8.0f, killPanelPosition.y + 6.0f });
	killIcon_.SetSize({ 26.0f, 26.0f });
	coinDigitPosition_ = { coinPanelPosition.x + 40.0f, coinPanelPosition.y + 9.0f };
	killDigitPosition_ = { killPanelPosition.x + 40.0f, killPanelPosition.y + 9.0f };
	coinDigitSize_ = { 10.0f, 15.0f };
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

void GameplayHudPresentation::ApplyBossHpBarLayout()
{
	bossHpFrame_.SetPosition(bossHpPosition_);
	bossHpFrame_.SetSize(bossHpSize_);
	const Vector2 innerPosition{
		bossHpPosition_.x + kBossHpFrameBorder,
		bossHpPosition_.y + kBossHpFrameBorder,
	};
	const Vector2 innerSize{
		(std::max)(1.0f, bossHpSize_.x - kBossHpFrameBorder * 2.0f),
		(std::max)(1.0f, bossHpSize_.y - kBossHpFrameBorder * 2.0f),
	};
	bossHpBackground_.SetPosition(innerPosition);
	bossHpBackground_.SetSize(innerSize);
	bossHpFill_.SetPosition(innerPosition);
	bossHpFill_.SetSize({ 1.0f, innerSize.y });
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

	bool bossHpChanged = false;
	float bossHpPosition[2]{ bossHpPosition_.x, bossHpPosition_.y };
	if (ImGui::DragFloat2(
		"Boss HP Position", bossHpPosition, 1.0f, -400.0f, 1280.0f)) {
		bossHpPosition_ = { bossHpPosition[0], bossHpPosition[1] };
		bossHpChanged = true;
	}
	float bossHpSize[2]{ bossHpSize_.x, bossHpSize_.y };
	if (ImGui::DragFloat2(
		"Boss HP Size", bossHpSize, 1.0f, 16.0f, 1280.0f)) {
		bossHpSize_ = { bossHpSize[0], bossHpSize[1] };
		bossHpChanged = true;
	}
	if (bossHpChanged) {
		ApplyBossHpBarLayout();
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
			{ "bossHpPosition", { bossHpPosition_.x, bossHpPosition_.y } },
			{ "bossHpSize", { bossHpSize_.x, bossHpSize_.y } },
		});
}

}
