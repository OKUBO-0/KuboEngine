#include "PauseBuildHud.h"
#include "DataPaths.h"
#include "ScreenUtil.h"
#include "UILayoutIO.h"
#include "PlayerManager.h"
#include "PassiveItemData.h"
#include "Input.h"
#include "GameSpriteFactory.h"
#include "GameTextureCache.h"
#include "DigitSpriteUtil.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#ifdef _DEBUG
#include <imgui.h>
#endif

namespace {

constexpr float kScreenWidth = 1280.0f;
constexpr float kScreenHeight = 720.0f;
constexpr float kVignetteBandSize = 45.0f;
constexpr char kSelectSePath[] = "se/ui_select.wav";
constexpr char kDecideSePath[] = "se/ui_decide.wav";
constexpr char kBackSePath[] = "se/ui_back.wav";
constexpr char kAudioUiSelect[] = "ui.select";
constexpr char kAudioUiDecide[] = "ui.decide";
constexpr char kAudioUiBack[] = "ui.back";
constexpr Vector4 kPanelColor{ 0.0f, 0.0f, 0.0f, 0.78f };
constexpr Vector4 kFrameColor{ 1.0f, 0.96f, 0.0f, 1.0f };
constexpr Vector4 kDimFrameColor{ 0.55f, 0.52f, 0.0f, 0.72f };
constexpr float kBorderThickness = 3.0f;

bool IsPointInRect(const Vector2& point, const Vector2& position, const Vector2& size)
{
	return point.x >= position.x && point.x <= position.x + size.x &&
		point.y >= position.y && point.y <= position.y + size.y;
}

void ApplyIconTextureRegion(
	Engine::Graphics2D::Sprite& sprite,
	std::string_view relativePath)
{
	const bool usesNewIconLayout =
		relativePath.find("/icon_") != std::string_view::npos;
	sprite.SetTextureLeftTop(
		usesNewIconLayout ? Vector2{ 0.0f, 0.0f }
			: Vector2{ 662.0f, 308.0f });
	sprite.SetTextureSize(
		usesNewIconLayout ? Vector2{ 512.0f, 512.0f }
			: Vector2{ 84.0f, 84.0f });
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
	const Vector2& position,
	const Vector2& size,
	float thickness,
	const Vector4& color)
{
	static_assert(N >= 4);
	SetPanelLayout(borders[0], position, { size.x, thickness }, color);
	SetPanelLayout(borders[1], { position.x, position.y + size.y - thickness }, { size.x, thickness }, color);
	SetPanelLayout(borders[2], position, { thickness, size.y }, color);
	SetPanelLayout(borders[3], { position.x + size.x - thickness, position.y }, { thickness, size.y }, color);
}

const char* WeaponIconPath(size_t index)
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

const char* PassiveSubIconPath(DirectXGame::PassiveItemType type)
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

}

namespace DirectXGame {

void PauseBuildHud::Initialize()
{
	overlay_.Initialize("ui/game/pause.png", { 0.0f, 0.0f });
	overlay_.SetSize({ 1280.0f, 720.0f });
	overlay_.SetAlpha(0.0f);
	leftCursor_.Initialize("ui/game/left_cursor.png", { 0.0f, 0.0f });
	rightCursor_.Initialize("ui/game/right_corsor.png", { 0.0f, 0.0f });
	leftCursor_.SetSize({ 1280.0f, 720.0f });
	rightCursor_.SetSize({ 1280.0f, 720.0f });
	vignetteBase_.Initialize();
	vignetteBase_.SetPosition({ 0.0f, 0.0f });
	vignetteBase_.SetSize({ kScreenWidth, kScreenHeight });
	vignetteBase_.SetColor({ 0.0f, 0.0f, 0.0f, 0.16f });
	for (auto& layer : vignettePanels_) {
		for (UIPanel& panel : layer) {
			panel.Initialize();
		}
	}
	menuPanel_.Initialize();
	for (UIPanel& border : menuPanelBorders_) {
		border.Initialize();
	}
	for (UIPanel& background : menuOptionBackgrounds_) {
		background.Initialize();
	}
	for (UIPanel& border : menuOptionBorders_) {
		border.Initialize();
	}
	for (UIPanel& panel : cursorPanels_) {
		panel.Initialize();
	}
	buildPanel_.Initialize();
	statusPanel_.Initialize();
	for (UIPanel& border : buildPanelBorders_) {
		border.Initialize();
	}
	for (UIPanel& border : statusPanelBorders_) {
		border.Initialize();
	}
	for (UIPanel& pip : levelPips_) {
		pip.Initialize();
		pip.SetColor({ 0.0f, 0.0f, 0.0f, 0.0f });
	}

	const UILayoutIO::LayoutMap values =
		UILayoutIO::LoadOrDefault(DataPaths::kPauseLayout, {});
	layout_.position = UILayoutIO::GetVector2(
		values, "pauseBuildPosition", layout_.position);
	layout_.stepX = UILayoutIO::GetFloat(
		values, "pauseBuildStepX", layout_.stepX);
	layout_.stepY = UILayoutIO::GetFloat(
		values, "pauseBuildStepY", layout_.stepY);
	layout_.iconSize = UILayoutIO::GetVector2(
		values, "pauseBuildIconSize", layout_.iconSize);
	layout_.statusPosition = UILayoutIO::GetVector2(
		values, "pauseStatusPosition", layout_.statusPosition);
	layout_.statusLineStep = UILayoutIO::GetFloat(
		values, "pauseStatusLineStep", layout_.statusLineStep);
	layout_.statusScale = UILayoutIO::GetFloat(
		values, "pauseStatusScale", layout_.statusScale);
	layout_.visible = UILayoutIO::GetFloat(
		values, "pauseBuildVisible", layout_.visible ? 1.0f : 0.0f) > 0.5f;
	for (int32_t index = 0; index < 3; ++index) {
		menuLayout_.hitboxPositions[static_cast<size_t>(index)] = UILayoutIO::GetVector2(
			values, "menuHitbox" + std::to_string(index), menuLayout_.hitboxPositions[static_cast<size_t>(index)]);
		menuLayout_.hitboxSizes[static_cast<size_t>(index)] = UILayoutIO::GetVector2(
			values, "menuHitboxSize" + std::to_string(index), menuLayout_.hitboxSizes[static_cast<size_t>(index)]);
		menuLayout_.leftCursorOffsets[static_cast<size_t>(index)] = UILayoutIO::GetVector2(
			values, "leftCursorOffset" + std::to_string(index), menuLayout_.leftCursorOffsets[static_cast<size_t>(index)]);
		menuLayout_.rightCursorOffsets[static_cast<size_t>(index)] = UILayoutIO::GetVector2(
			values, "rightCursorOffset" + std::to_string(index), menuLayout_.rightCursorOffsets[static_cast<size_t>(index)]);
	}

	for (size_t index = 0; index < icons_.size(); ++index) {
		const char* iconPath = index < kWeaponIconCount
			? WeaponIconPath(index)
			: "ui/game/lvup/icon_passive_scroll.png";
		icons_[index] = GameSpriteFactory::Create(iconPath, {});
		ApplyIconTextureRegion(*icons_[index], iconPath);
		icons_[index]->SetSize(layout_.iconSize);
		icons_[index]->SetColor({ 1.0f, 1.0f, 1.0f, 0.0f });
		levelDigits_[index] = GameSpriteFactory::Create("ui/number/numbers.png", {});
		levelDigits_[index]->SetSize({ 18.0f, 24.0f });
		levelDigits_[index]->SetTextureSize({ 24.0f, 32.0f });
		levelDigits_[index]->SetColor({ 1.0f, 1.0f, 1.0f, 0.0f });
	}
	for (auto& subIcon : itemSubIcons_) {
		subIcon = GameSpriteFactory::Create("ui/game/lvup/icon_stat_damage.png", {});
		subIcon->SetTextureLeftTop({ 0.0f, 0.0f });
		subIcon->SetTextureSize({ 512.0f, 512.0f });
		subIcon->SetSize({ 18.0f, 18.0f });
		subIcon->SetColor({ 1.0f, 1.0f, 1.0f, 0.0f });
	}
	for (size_t index = 0; index < statusTexts_.size(); ++index) {
		statusTexts_[index].Initialize(
			"ui/font/noto_sans_jp_black.png",
			"ui/font/noto_sans_jp_black.json");
		statusTexts_[index].SetScale(layout_.statusScale);
		statusTexts_[index].SetColor(index == 0
			? Vector4{ 1.0f, 0.9f, 0.15f, 1.0f }
			: Vector4{ 1.0f, 1.0f, 1.0f, 1.0f });
		statusValueTexts_[index].Initialize(
			"ui/font/noto_sans_jp_black.png",
			"ui/font/noto_sans_jp_black.json");
		statusValueTexts_[index].SetScale(layout_.statusScale);
		statusValueTexts_[index].SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
	}
	const std::array<const char*, 3> menuLabels{ "RESUME", "RESTART", "TITLE" };
	for (size_t index = 0; index < menuOptionTexts_.size(); ++index) {
		menuOptionTexts_[index].Initialize(
			"ui/font/noto_sans_jp_black.png",
			"ui/font/noto_sans_jp_black.json");
		menuOptionTexts_[index].SetText(menuLabels[index]);
		menuOptionTexts_[index].SetScale(0.34f);
		menuOptionTexts_[index].SetAdvanceMultiplier(0.96f);
		menuOptionTexts_[index].SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
	}
	pauseTitleText_.Initialize(
		"ui/font/noto_sans_jp_black.png",
		"ui/font/noto_sans_jp_black.json");
	pauseTitleText_.SetText("--PAUSE--");
	pauseTitleText_.SetScale(0.86f);
	pauseTitleText_.SetAdvanceMultiplier(0.92f);
	pauseTitleText_.SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
	selectSeHandle_ = GameAudioCache::LoadWave(kSelectSePath);
	decideSeHandle_ = GameAudioCache::LoadWave(kDecideSePath);
	backSeHandle_ = GameAudioCache::LoadWave(kBackSePath);
	ApplyLayout();
}

void PauseBuildHud::Start()
{
	selection_ = 0;
	currentLeftCursorPosition_ = menuLayout_.leftCursorOffsets[0];
	currentRightCursorPosition_ = menuLayout_.rightCursorOffsets[0];
	cursorPositionInitialized_ = true;
	leftCursor_.SetPosition(currentLeftCursorPosition_);
	rightCursor_.SetPosition(currentRightCursorPosition_);
}

void PauseBuildHud::TrackAcquisitions(const PlayerManager& playerManager)
{
	const std::array<bool, kWeaponIconCount> acquired{
		true,
		playerManager.HasOrbitBullets(),
		playerManager.HasLightning(),
		playerManager.HasExplosiveBullets(),
		playerManager.HasSword(),
		playerManager.HasAura(),
		playerManager.HasFlameShoes(),
		playerManager.HasBone(),
		playerManager.HasHandgun(),
		playerManager.HasBoomerang(),
	};
	for (int32_t index = 1; index < static_cast<int32_t>(acquired.size()); ++index) {
		if (!acquired[static_cast<size_t>(index)] || acquisitionRecorded_[static_cast<size_t>(index)]) {
			continue;
		}
		acquisitionRecorded_[static_cast<size_t>(index)] = true;
		weaponAcquisitionOrder_.push_back(index);
	}
}

PauseMenuAction PauseBuildHud::Update(
	const PlayerManager& playerManager,
	float animationTime,
	int32_t moveDelta,
	bool confirmTriggered,
	bool cancelTriggered,
	GameInputBindings::NavigationInputDevice inputDevice)
{
	UpdateBuildIcons(playerManager, animationTime);

	if (moveDelta != 0) {
		MoveSelection(moveDelta);
		if (selectSeHandle_) {
			GameAudioCache::PlayTuned(
				selectSeHandle_,
				kAudioUiSelect,
				0.55f,
				0.035f);
		}
	}
	const int32_t hoveredMenuIndex = GetHoveredMenuIndex();
	if (inputDevice == GameInputBindings::NavigationInputDevice::Mouse &&
		hoveredMenuIndex >= 0 &&
		hoveredMenuIndex != selection_) {
		selection_ = hoveredMenuIndex;
		if (selectSeHandle_) {
			GameAudioCache::PlayTuned(
				selectSeHandle_,
				kAudioUiSelect,
				0.55f,
				0.035f);
		}
	}
	const Vector2 leftTarget = menuLayout_.leftCursorOffsets[static_cast<size_t>(selection_)];
	const Vector2 rightTarget = menuLayout_.rightCursorOffsets[static_cast<size_t>(selection_)];
	if (!cursorPositionInitialized_) {
		currentLeftCursorPosition_ = leftTarget;
		currentRightCursorPosition_ = rightTarget;
		cursorPositionInitialized_ = true;
	}
	constexpr float kCursorEasing = 0.18f;
	currentLeftCursorPosition_.x += (leftTarget.x - currentLeftCursorPosition_.x) * kCursorEasing;
	currentLeftCursorPosition_.y += (leftTarget.y - currentLeftCursorPosition_.y) * kCursorEasing;
	currentRightCursorPosition_.x += (rightTarget.x - currentRightCursorPosition_.x) * kCursorEasing;
	currentRightCursorPosition_.y += (rightTarget.y - currentRightCursorPosition_.y) * kCursorEasing;
	leftCursor_.SetPosition(currentLeftCursorPosition_);
	rightCursor_.SetPosition(currentRightCursorPosition_);
	const float cursorPulse =
		0.5f + 0.5f * std::sin(animationTime * 8.0f);
	leftCursor_.SetAlpha(0.0f);
	rightCursor_.SetAlpha(0.0f);
	const Vector2 cursorPosition =
		menuLayout_.hitboxPositions[static_cast<size_t>(selection_)];
	const Vector2 cursorSize =
		menuLayout_.hitboxSizes[static_cast<size_t>(selection_)];
	const float cursorAlpha = 0.78f + cursorPulse * 0.22f;
	const float corner = 18.0f;
	const float thickness = 4.0f;
	const Vector4 cursorColor{ 1.0f, 1.0f, 0.55f, cursorAlpha };
	const std::array<Vector2, 8> cursorPositions{
		Vector2{ cursorPosition.x, cursorPosition.y },
		Vector2{ cursorPosition.x, cursorPosition.y },
		Vector2{ cursorPosition.x + cursorSize.x - corner, cursorPosition.y },
		Vector2{ cursorPosition.x + cursorSize.x - thickness, cursorPosition.y },
		Vector2{ cursorPosition.x, cursorPosition.y + cursorSize.y - thickness },
		Vector2{ cursorPosition.x, cursorPosition.y + cursorSize.y - corner },
		Vector2{ cursorPosition.x + cursorSize.x - corner, cursorPosition.y + cursorSize.y - thickness },
		Vector2{ cursorPosition.x + cursorSize.x - thickness, cursorPosition.y + cursorSize.y - corner },
	};
	const std::array<Vector2, 8> cursorSizes{
		Vector2{ corner, thickness },
		Vector2{ thickness, corner },
		Vector2{ corner, thickness },
		Vector2{ thickness, corner },
		Vector2{ corner, thickness },
		Vector2{ thickness, corner },
		Vector2{ corner, thickness },
		Vector2{ thickness, corner },
	};
	for (size_t index = 0; index < cursorPanels_.size(); ++index) {
		SetPanelLayout(cursorPanels_[index], cursorPositions[index], cursorSizes[index], cursorColor);
	}
	for (size_t index = 0; index < menuOptionBackgrounds_.size(); ++index) {
		const bool selected = index == static_cast<size_t>(selection_);
		menuOptionBackgrounds_[index].SetColor(selected
			? Vector4{ 0.12f, 0.12f, 0.02f, 0.38f }
			: Vector4{ 0.0f, 0.0f, 0.0f, 0.0f });
		menuOptionTexts_[index].SetColor(selected
			? Vector4{ 1.0f, 0.95f, 0.18f, 1.0f }
			: Vector4{ 1.0f, 1.0f, 1.0f, 0.92f });
	}

	const bool mouseConfirm =
		hoveredMenuIndex >= 0 &&
		GameInputBindings::IsMouseConfirmTriggered(
			Engine::InputSystem::Input::GetInstance());
	const bool confirmed =
		inputDevice == GameInputBindings::NavigationInputDevice::Mouse
		? mouseConfirm
		: confirmTriggered;
	if (confirmed) {
		if (decideSeHandle_) {
			GameAudioCache::PlayTuned(decideSeHandle_, kAudioUiDecide, 0.72f);
		}
		switch (selection_) {
		case 0: return PauseMenuAction::Resume;
		case 1: return PauseMenuAction::Restart;
		case 2: return PauseMenuAction::Exit;
		default: break;
		}
	}
	if (cancelTriggered) {
		if (backSeHandle_) {
			GameAudioCache::PlayTuned(backSeHandle_, kAudioUiBack, 0.52f);
		}
		return PauseMenuAction::Resume;
	}
	return PauseMenuAction::None;
}

void PauseBuildHud::UpdateBuildIcons(
	const PlayerManager& playerManager,
	float animationTime)
{
	TrackAcquisitions(playerManager);
	UpdateStatusTexts(playerManager);
	for (auto& icon : icons_) {
		if (icon) {
			icon->SetColor({ 1.0f, 1.0f, 1.0f, 0.0f });
		}
	}
	for (auto& subIcon : itemSubIcons_) {
		if (subIcon) {
			subIcon->SetColor({ 1.0f, 1.0f, 1.0f, 0.0f });
		}
	}
	for (auto& digit : levelDigits_) {
		if (digit) {
			digit->SetColor({ 1.0f, 1.0f, 1.0f, 0.0f });
		}
	}
	for (UIPanel& pip : levelPips_) {
		pip.SetColor({ 0.0f, 0.0f, 0.0f, 0.0f });
	}
	auto setLevelPips = [this](
		size_t iconSlot,
		int32_t level,
		int32_t maxLevel,
		const Vector2& iconPosition,
		const Vector2& iconSize,
		float alpha) {
		const size_t base = iconSlot * kMaxLevelPipsPerIcon;
		const int32_t clampedMax = std::clamp(maxLevel, 1, static_cast<int32_t>(kMaxLevelPipsPerIcon));
		const int32_t clampedLevel = std::clamp(level, 0, clampedMax);
		constexpr int32_t kPipsPerRow = 4;
		const float pipSide = (std::min)(6.0f, (iconSize.x - 6.0f) / 4.0f);
		const float pipGap = 2.0f;
		const Vector2 pipSize{ pipSide, pipSide };
		const int32_t visibleColumns = (std::min)(kPipsPerRow, clampedMax);
		const float rowWidth =
			pipSide * static_cast<float>(visibleColumns) +
			pipGap * static_cast<float>((std::max)(0, visibleColumns - 1));
		const float startX = iconPosition.x + (iconSize.x - rowWidth) * 0.5f;
		for (int32_t pipIndex = 0; pipIndex < clampedMax; ++pipIndex) {
			UIPanel& pip = levelPips_[base + static_cast<size_t>(pipIndex)];
			const int32_t column = pipIndex % kPipsPerRow;
			const int32_t row = pipIndex / kPipsPerRow;
			pip.SetPosition({
				startX + (pipSide + pipGap) * static_cast<float>(column),
				iconPosition.y + iconSize.y + 5.0f + (pipSide + pipGap) * static_cast<float>(row),
				});
			pip.SetSize(pipSize);
			pip.SetColor(pipIndex < clampedLevel
				? Vector4{ 1.0f, 0.92f, 0.06f, alpha }
				: Vector4{ 0.08f, 0.08f, 0.02f, alpha * 0.82f });
		}
	};
	const std::array<int32_t, kWeaponIconCount> weaponLevels{
		playerManager.GetNormalBulletLevel(),
		playerManager.GetOrbitBulletLevel(),
		playerManager.GetLightningLevel(),
		playerManager.GetExplosiveBulletLevel(),
		playerManager.GetSwordLevel(),
		playerManager.GetAuraLevel(),
		playerManager.GetFlameShoesLevel(),
		playerManager.GetBoneLevel(),
		playerManager.GetHandgunLevel(),
		playerManager.GetBoomerangLevel(),
	};
	auto placeWeaponRow = [this, &weaponLevels, &setLevelPips](
		const std::vector<int32_t>& order, float y) {
		constexpr size_t kColumns = 5;
		for (size_t orderIndex = 0; orderIndex < order.size(); ++orderIndex) {
			const int32_t iconIndex = order[orderIndex];
			auto& icon = icons_[static_cast<size_t>(iconIndex)];
			if (!icon) {
				continue;
			}
			const float column = static_cast<float>(orderIndex % kColumns);
			const float row = static_cast<float>(orderIndex / kColumns);
			const Vector2 iconPosition{
				layout_.position.x + layout_.stepX * column,
				y + row * (layout_.iconSize.y + 26.0f),
			};
			icon->SetPosition({
				iconPosition.x,
				iconPosition.y });
			icon->SetSize(layout_.iconSize);
			icon->SetColor({ 1.0f, 1.0f, 1.0f, layout_.visible ? 1.0f : 0.0f });
			setLevelPips(
				static_cast<size_t>(iconIndex),
				weaponLevels[static_cast<size_t>(iconIndex)],
				8,
				iconPosition,
				layout_.iconSize,
				layout_.visible ? 1.0f : 0.0f);
		}
	};
	placeWeaponRow(weaponAcquisitionOrder_, layout_.position.y);

	const std::vector<PassiveItemType>& items =
		playerManager.GetPassiveItemAcquisitionOrder();
	for (size_t slot = 0; slot < kPassiveItemSlotCount; ++slot) {
		const size_t iconIndex = kWeaponIconCount + slot;
		const bool visible = slot < items.size() && layout_.visible;
		if (!visible) {
			continue;
		}
		const PassiveItemType type = items[slot];
		const int32_t level = playerManager.GetPassiveItemLevel(type);
		auto& icon = icons_[iconIndex];
		auto& subIcon = itemSubIcons_[slot];
		if (displayedItemTypes_[slot] != type) {
			const TextureHandle itemTexture =
				GameTextureCache::Load("ui/game/lvup/icon_passive_scroll.png");
			icon->SetTexture(GameTextureCache::GetPath(itemTexture));
			ApplyIconTextureRegion(*icon, "ui/game/lvup/icon_passive_scroll.png");
			if (subIcon) {
				const TextureHandle subTexture =
					GameTextureCache::Load(PassiveSubIconPath(type));
				subIcon->SetTexture(GameTextureCache::GetPath(subTexture));
				subIcon->SetTextureLeftTop({ 0.0f, 0.0f });
				subIcon->SetTextureSize({ 512.0f, 512.0f });
			}
			displayedItemTypes_[slot] = type;
		}
		const Vector2 position{
			layout_.position.x + static_cast<float>(slot) * 48.0f,
			layout_.position.y + layout_.stepY,
		};
		const Vector2 iconSize{ 42.0f, 42.0f };
		icon->SetPosition(position);
		icon->SetSize(iconSize);
		icon->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
		if (subIcon) {
			const Vector2 subSize{ 18.0f, 18.0f };
			subIcon->SetPosition({
				position.x + iconSize.x - subSize.x - 2.0f,
				position.y + iconSize.y - subSize.y - 2.0f,
				});
			subIcon->SetSize(subSize);
			subIcon->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
		}
		setLevelPips(
			iconIndex,
			level,
			playerManager.GetPassiveItemMaxLevel(type),
			position,
			iconSize,
			1.0f);
	}
	(void)animationTime;
}

void PauseBuildHud::UpdateStatusTexts(const PlayerManager& playerManager)
{
	const PlayerStats& stats = playerManager.GetStats();
	auto fixed = [](float value, int precision = 2) {
		std::ostringstream stream;
		stream << std::fixed << std::setprecision(precision) << value;
		return stream.str();
	};
	const std::array<std::string, kStatusLineCount> labels{
		"PLAYER STATS",
		"HP",
		"DAMAGE",
		"ATTACK SPD",
		"DURATION",
		"MOVE SPD",
		"PROJ SPD",
		"AREA SIZE",
		"PROJ COUNT",
		"ARMOR",
		"EVASION",
		"HP REGEN",
		"LIFESTEAL",
		"PICKUP",
		"EXP / COIN",
		"CRIT",
	};
	const std::array<std::string, kStatusLineCount> values{
		"",
		std::to_string(playerManager.GetHP()) + " / " +
			std::to_string(playerManager.GetMaxHP()),
		"x" + fixed(stats.GetDamageMultiplier()),
		"x" + fixed(stats.GetAttackSpeedMultiplier()),
		"x" + fixed(stats.GetDurationMultiplier()),
		"x" + fixed(stats.GetMovementSpeedMultiplier()),
		"x" + fixed(stats.GetProjectileSpeedMultiplier()),
		"x" + fixed(stats.GetAreaSizeMultiplier()),
		"+" + std::to_string(stats.GetProjectileCountBonus()),
		fixed(stats.GetArmorReduction() * 100.0f, 1) + "%",
		fixed(stats.GetEvasionChance() * 100.0f, 1) + "%",
		fixed(stats.GetHpRegenPerSecond(), 1) + " /s",
		fixed(stats.GetLifeStealChance() * 100.0f, 1) + "%",
		"x" + fixed(stats.GetPickupRangeMultiplier()),
		"x" + fixed(stats.GetExpGainMultiplier()) + " / x" +
			fixed(stats.GetCoinGainMultiplier()),
		fixed(stats.GetCritChance() * 100.0f, 1) +
			"% / x" + fixed(stats.GetCritDamageMultiplier()),
	};
	for (size_t index = 0; index < statusTexts_.size(); ++index) {
		statusTexts_[index].SetText(labels[index]);
		statusValueTexts_[index].SetText(values[index]);
	}
}

void PauseBuildHud::Draw()
{
	vignetteBase_.Draw();
	for (auto& layer : vignettePanels_) {
		for (UIPanel& panel : layer) {
			panel.Draw();
		}
	}
	overlay_.Draw();
	menuPanel_.Draw();
	for (UIPanel& border : menuPanelBorders_) {
		border.Draw();
	}
	for (UIPanel& background : menuOptionBackgrounds_) {
		background.Draw();
	}
	for (UIPanel& border : menuOptionBorders_) {
		border.Draw();
	}
	buildPanel_.Draw();
	for (UIPanel& border : buildPanelBorders_) {
		border.Draw();
	}
	statusPanel_.Draw();
	for (UIPanel& border : statusPanelBorders_) {
		border.Draw();
	}
	for (BitmapText& text : menuOptionTexts_) {
		text.Draw();
	}
	pauseTitleText_.Draw();
	for (UIPanel& panel : cursorPanels_) {
		panel.Draw();
	}
	if (!layout_.visible) {
		return;
	}
	for (auto& icon : icons_) {
		if (icon) {
			icon->Update();
			icon->Draw();
		}
	}
	for (auto& subIcon : itemSubIcons_) {
		if (subIcon) {
			subIcon->Update();
			subIcon->Draw();
		}
	}
	for (UIPanel& pip : levelPips_) {
		pip.Draw();
	}
	for (BitmapText& text : statusTexts_) {
		text.Draw();
	}
	for (BitmapText& text : statusValueTexts_) {
		text.Draw();
	}
}

void PauseBuildHud::SaveLayout() const
{
	UILayoutIO::Save(DataPaths::kPauseLayout,
		{
			{ "pauseBuildPosition", { layout_.position.x, layout_.position.y } },
			{ "pauseBuildStepX", { layout_.stepX } },
			{ "pauseBuildStepY", { layout_.stepY } },
			{ "pauseBuildIconSize", { layout_.iconSize.x, layout_.iconSize.y } },
			{ "pauseBuildVisible", { layout_.visible ? 1.0f : 0.0f } },
			{ "pauseStatusPosition", { layout_.statusPosition.x, layout_.statusPosition.y } },
			{ "pauseStatusLineStep", { layout_.statusLineStep } },
			{ "pauseStatusScale", { layout_.statusScale } },
			{ "menuHitbox0", { menuLayout_.hitboxPositions[0].x, menuLayout_.hitboxPositions[0].y } },
			{ "menuHitbox1", { menuLayout_.hitboxPositions[1].x, menuLayout_.hitboxPositions[1].y } },
			{ "menuHitbox2", { menuLayout_.hitboxPositions[2].x, menuLayout_.hitboxPositions[2].y } },
			{ "menuHitboxSize0", { menuLayout_.hitboxSizes[0].x, menuLayout_.hitboxSizes[0].y } },
			{ "menuHitboxSize1", { menuLayout_.hitboxSizes[1].x, menuLayout_.hitboxSizes[1].y } },
			{ "menuHitboxSize2", { menuLayout_.hitboxSizes[2].x, menuLayout_.hitboxSizes[2].y } },
			{ "leftCursorOffset0", { menuLayout_.leftCursorOffsets[0].x, menuLayout_.leftCursorOffsets[0].y } },
			{ "leftCursorOffset1", { menuLayout_.leftCursorOffsets[1].x, menuLayout_.leftCursorOffsets[1].y } },
			{ "leftCursorOffset2", { menuLayout_.leftCursorOffsets[2].x, menuLayout_.leftCursorOffsets[2].y } },
			{ "rightCursorOffset0", { menuLayout_.rightCursorOffsets[0].x, menuLayout_.rightCursorOffsets[0].y } },
			{ "rightCursorOffset1", { menuLayout_.rightCursorOffsets[1].x, menuLayout_.rightCursorOffsets[1].y } },
			{ "rightCursorOffset2", { menuLayout_.rightCursorOffsets[2].x, menuLayout_.rightCursorOffsets[2].y } },
		});
}

void PauseBuildHud::MoveSelection(int32_t delta)
{
	constexpr int32_t kMaxSelection = 2;
	selection_ += delta;
	if (selection_ < 0) {
		selection_ = kMaxSelection;
	} else if (selection_ > kMaxSelection) {
		selection_ = 0;
	}
}

int32_t PauseBuildHud::GetHoveredMenuIndex() const
{
	Engine::InputSystem::Input* input =
		Engine::InputSystem::Input::GetInstance();
	if (!input || GameInputBindings::IsGameInputSuppressedByImGui()) {
		return -1;
	}
	if (!ScreenUtil::IsInsideDebugSceneViewport(input->GetMousePos())) {
		return -1;
	}
	const Vector2 mousePosition =
		ScreenUtil::ToGamePosition(input->GetMousePos());
	for (int32_t index = 0;
		index < static_cast<int32_t>(menuLayout_.hitboxPositions.size());
		++index) {
		if (IsPointInRect(
				mousePosition,
				menuLayout_.hitboxPositions[static_cast<size_t>(index)],
				menuLayout_.hitboxSizes[static_cast<size_t>(index)])) {
			return index;
		}
	}
	return -1;
}

void PauseBuildHud::ApplyLayout()
{
	const Vector2 menuPosition{ 400.0f, 170.0f };
	const Vector2 menuSize{ 480.0f, 430.0f };
	SetPanelLayout(menuPanel_, menuPosition, menuSize, { 0.0f, 0.0f, 0.0f, 0.0f });
	SetBorderLayout(menuPanelBorders_, menuPosition, menuSize, kBorderThickness, { 1.0f, 0.96f, 0.0f, 0.0f });
	pauseTitleText_.SetScaleToFit(0.86f, 760.0f);
	pauseTitleText_.SetPosition({
		640.0f - pauseTitleText_.MeasureWidth() * 0.5f,
		44.0f,
		});
	const std::array<Vector2, 3> optionPositions{
		Vector2{ 470.0f, 266.0f },
		Vector2{ 450.0f, 394.0f },
		Vector2{ 540.0f, 526.0f },
	};
	const std::array<Vector2, 3> optionSizes{
		Vector2{ 350.0f, 82.0f },
		Vector2{ 390.0f, 82.0f },
		Vector2{ 200.0f, 82.0f },
	};
	for (size_t index = 0; index < menuOptionBackgrounds_.size(); ++index) {
		SetPanelLayout(menuOptionBackgrounds_[index], optionPositions[index], optionSizes[index], { 0.0f, 0.0f, 0.0f, 0.0f });
		const size_t borderIndex = index * 4;
		SetPanelLayout(
			menuOptionBorders_[borderIndex + 0],
			optionPositions[index],
			{ optionSizes[index].x, 2.0f },
			{ 1.0f, 0.96f, 0.0f, 0.0f });
		SetPanelLayout(
			menuOptionBorders_[borderIndex + 1],
			{ optionPositions[index].x, optionPositions[index].y + optionSizes[index].y - 2.0f },
			{ optionSizes[index].x, 2.0f },
			{ 1.0f, 0.96f, 0.0f, 0.0f });
		SetPanelLayout(
			menuOptionBorders_[borderIndex + 2],
			optionPositions[index],
			{ 2.0f, optionSizes[index].y },
			{ 1.0f, 0.96f, 0.0f, 0.0f });
		SetPanelLayout(
			menuOptionBorders_[borderIndex + 3],
			{ optionPositions[index].x + optionSizes[index].x - 2.0f, optionPositions[index].y },
			{ 2.0f, optionSizes[index].y },
			{ 1.0f, 0.96f, 0.0f, 0.0f });
		menuOptionTexts_[index].SetScaleToFit(0.44f, optionSizes[index].x - 36.0f);
		const float width = menuOptionTexts_[index].MeasureWidth();
		menuOptionTexts_[index].SetPosition({
			optionPositions[index].x + (optionSizes[index].x - width) * 0.5f,
			optionPositions[index].y + 17.0f,
			});
	}
	const Vector2 buildPosition{ 39.0f, 200.0f };
	const Vector2 buildSize{ 328.0f, 400.0f };
	SetPanelLayout(buildPanel_, buildPosition, buildSize, kPanelColor);
	SetBorderLayout(buildPanelBorders_, buildPosition, buildSize, kBorderThickness, kFrameColor);
	const Vector2 statusPosition{ 913.0f, 200.0f };
	const Vector2 statusSize{ 328.0f, 400.0f };
	SetPanelLayout(statusPanel_, statusPosition, statusSize, kPanelColor);
	SetBorderLayout(statusPanelBorders_, statusPosition, statusSize, kBorderThickness, kFrameColor);
	for (size_t index = 0; index < statusTexts_.size(); ++index) {
		statusTexts_[index].SetPosition({
			layout_.statusPosition.x,
			layout_.statusPosition.y +
				layout_.statusLineStep * static_cast<float>(index),
			});
		statusTexts_[index].SetScale(layout_.statusScale);
		statusValueTexts_[index].SetPosition({
			layout_.statusPosition.x + 118.0f,
			layout_.statusPosition.y +
				layout_.statusLineStep * static_cast<float>(index),
			});
		statusValueTexts_[index].SetScale(layout_.statusScale);
	}
	for (size_t index = 0; index < icons_.size(); ++index) {
		if (!icons_[index]) {
			continue;
		}
		const float column = static_cast<float>(index % 3);
		const float row = static_cast<float>(index / 3);
		icons_[index]->SetPosition({
			layout_.position.x + layout_.stepX * column,
			layout_.position.y + layout_.stepY * row,
			});
		icons_[index]->SetSize(layout_.iconSize);
	}
	for (size_t layerIndex = 0; layerIndex < vignettePanels_.size(); ++layerIndex) {
		const float inset = kVignetteBandSize * static_cast<float>(layerIndex);
		const float innerWidth = kScreenWidth - inset * 2.0f;
		const float innerHeight = kScreenHeight - inset * 2.0f;
		const float alpha = 0.42f - 0.045f * static_cast<float>(layerIndex);
		auto& panels = vignettePanels_[layerIndex];
		panels[0].SetPosition({ inset, inset });
		panels[0].SetSize({ innerWidth, kVignetteBandSize });
		panels[1].SetPosition({ inset, kScreenHeight - inset - kVignetteBandSize });
		panels[1].SetSize({ innerWidth, kVignetteBandSize });
		panels[2].SetPosition({ inset, inset + kVignetteBandSize });
		panels[2].SetSize({ kVignetteBandSize, innerHeight - kVignetteBandSize * 2.0f });
		panels[3].SetPosition({ kScreenWidth - inset - kVignetteBandSize, inset + kVignetteBandSize });
		panels[3].SetSize({ kVignetteBandSize, innerHeight - kVignetteBandSize * 2.0f });
		for (UIPanel& panel : panels) {
			panel.SetColor({ 0.0f, 0.0f, 0.0f, alpha });
		}
	}
}

#ifdef _DEBUG
void PauseBuildHud::DrawDebugUI()
{
	ImGui::Checkbox("Show Pause Build Icons", &layout_.visible);
	ImGui::Checkbox("Enable HUD Debug##PauseBuildIcons", &layout_.debugEnabled);
	if (!layout_.debugEnabled) {
		return;
	}

	bool changed = false;
	float position[2]{ layout_.position.x, layout_.position.y };
	if (ImGui::DragFloat2(
			"Pause Build Position", position, 1.0f, -400.0f, 1280.0f)) {
		layout_.position = { position[0], position[1] };
		changed = true;
	}
	changed |= ImGui::DragFloat(
		"Pause Build Spacing", &layout_.stepX, 1.0f, 32.0f, 240.0f);
	changed |= ImGui::DragFloat(
		"Pause Build Row Spacing", &layout_.stepY, 1.0f, 32.0f, 240.0f);
	float iconSize[2]{ layout_.iconSize.x, layout_.iconSize.y };
	if (ImGui::DragFloat2(
			"Pause Build Icon Size", iconSize, 1.0f, 16.0f, 256.0f)) {
		layout_.iconSize = {
			(std::max)(16.0f, iconSize[0]),
			(std::max)(16.0f, iconSize[1]),
		};
		changed = true;
	}
	float statusPosition[2]{
		layout_.statusPosition.x, layout_.statusPosition.y };
	if (ImGui::DragFloat2(
			"Pause Status Position", statusPosition, 1.0f, 0.0f, 1280.0f)) {
		layout_.statusPosition = { statusPosition[0], statusPosition[1] };
		changed = true;
	}
	changed |= ImGui::DragFloat(
		"Pause Status Line Step", &layout_.statusLineStep, 0.5f, 10.0f, 40.0f);
	changed |= ImGui::DragFloat(
		"Pause Status Scale", &layout_.statusScale, 0.01f, 0.1f, 0.5f);
	if (changed) {
		ApplyLayout();
	}
	for (int32_t index = 0; index < 3; ++index) {
		ImGui::PushID(index);
		float hitboxPosition[2]{
			menuLayout_.hitboxPositions[static_cast<size_t>(index)].x,
			menuLayout_.hitboxPositions[static_cast<size_t>(index)].y,
		};
		if (ImGui::DragFloat2("Menu Hitbox Position", hitboxPosition, 1.0f, -200.0f, 1280.0f)) {
			menuLayout_.hitboxPositions[static_cast<size_t>(index)] = { hitboxPosition[0], hitboxPosition[1] };
		}
		float hitboxSize[2]{
			menuLayout_.hitboxSizes[static_cast<size_t>(index)].x,
			menuLayout_.hitboxSizes[static_cast<size_t>(index)].y,
		};
		if (ImGui::DragFloat2("Menu Hitbox Size", hitboxSize, 1.0f, 8.0f, 640.0f)) {
			menuLayout_.hitboxSizes[static_cast<size_t>(index)] = { hitboxSize[0], hitboxSize[1] };
		}
		float leftOffset[2]{
			menuLayout_.leftCursorOffsets[static_cast<size_t>(index)].x,
			menuLayout_.leftCursorOffsets[static_cast<size_t>(index)].y,
		};
		if (ImGui::DragFloat2("Left Cursor Offset", leftOffset, 1.0f, -720.0f, 720.0f)) {
			menuLayout_.leftCursorOffsets[static_cast<size_t>(index)] = { leftOffset[0], leftOffset[1] };
		}
		float rightOffset[2]{
			menuLayout_.rightCursorOffsets[static_cast<size_t>(index)].x,
			menuLayout_.rightCursorOffsets[static_cast<size_t>(index)].y,
		};
		if (ImGui::DragFloat2("Right Cursor Offset", rightOffset, 1.0f, -720.0f, 720.0f)) {
			menuLayout_.rightCursorOffsets[static_cast<size_t>(index)] = { rightOffset[0], rightOffset[1] };
		}
		ImGui::PopID();
	}
	if (ImGui::Button("Save Pause Layout")) {
		SaveLayout();
	}
}
#endif

}
