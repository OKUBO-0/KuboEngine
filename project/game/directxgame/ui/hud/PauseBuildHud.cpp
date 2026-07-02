#include "PauseBuildHud.h"
#include "DataPaths.h"
#include "ScreenUtil.h"
#include "UILayoutIO.h"
#include "PlayerManager.h"
#include "Input.h"
#include "GameSpriteFactory.h"
#include "DigitSpriteUtil.h"
#include <algorithm>
#include <cmath>
#include <string>
#ifdef _DEBUG
#include <imgui.h>
#endif

namespace {

constexpr float kScreenWidth = 1280.0f;
constexpr float kScreenHeight = 720.0f;
constexpr float kVignetteBandSize = 45.0f;

bool IsPointInRect(const Vector2& point, const Vector2& position, const Vector2& size)
{
	return point.x >= position.x && point.x <= position.x + size.x &&
		point.y >= position.y && point.y <= position.y + size.y;
}

}

namespace DirectXGame {

void PauseBuildHud::Initialize()
{
	overlay_.Initialize("ui/game/pause.png", { 0.0f, 0.0f });
	overlay_.SetSize({ 1280.0f, 720.0f });
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

	const std::array<const char*, kIconCount> iconPaths{
		"ui/game/lvup/normal_icon.png",
		"ui/game/lvup/orbit_icon.png",
		"ui/game/lvup/drone_icon.png",
		"ui/game/lvup/lightning_icon.png",
		"ui/game/lvup/normal_icon.png",
		"ui/game/lvup/maxhp_icon.png",
		"ui/game/lvup/attack_icon.png",
		"ui/game/lvup/speed_icon.png",
		"ui/game/lvup/heal_icon.png",
	};
	for (size_t index = 0; index < icons_.size(); ++index) {
		icons_[index] = GameSpriteFactory::Create(iconPaths[index], {});
		icons_[index]->SetTextureLeftTop({ 662.0f, 308.0f });
		icons_[index]->SetTextureSize({ 84.0f, 84.0f });
		icons_[index]->SetSize(layout_.iconSize);
		icons_[index]->SetColor({ 1.0f, 1.0f, 1.0f, 0.0f });
		levelDigits_[index] = GameSpriteFactory::Create("ui/number/numbers.png", {});
		levelDigits_[index]->SetSize({ 18.0f, 24.0f });
		levelDigits_[index]->SetTextureSize({ 24.0f, 32.0f });
		levelDigits_[index]->SetColor({ 1.0f, 1.0f, 1.0f, 0.0f });
	}
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
	if (!acquisitionBaselineInitialized_) {
		baselineMaxHP_ = playerManager.GetMaxHP();
		baselineAttackPower_ = playerManager.GetAttackPower();
		baselineMoveSpeedLevel_ = playerManager.GetMoveSpeedLevel();
		baselineExpPickupRangeMultiplier_ = playerManager.GetExpPickupRangeMultiplier();
		acquisitionBaselineInitialized_ = true;
	}
	const std::array<bool, kIconCount> acquired{
		true,
		playerManager.HasOrbitBullets(),
		playerManager.HasDrone(),
		playerManager.HasLightning(),
		playerManager.HasExplosiveBullets(),
		playerManager.GetMaxHP() > baselineMaxHP_,
		playerManager.GetAttackPower() > baselineAttackPower_,
		playerManager.GetMoveSpeedLevel() > baselineMoveSpeedLevel_,
		playerManager.GetExpPickupRangeMultiplier() > baselineExpPickupRangeMultiplier_,
	};
	for (int32_t index = 1; index < static_cast<int32_t>(acquired.size()); ++index) {
		if (!acquired[static_cast<size_t>(index)] || acquisitionRecorded_[static_cast<size_t>(index)]) {
			continue;
		}
		acquisitionRecorded_[static_cast<size_t>(index)] = true;
		(index <= 4 ? weaponAcquisitionOrder_ : itemAcquisitionOrder_).push_back(index);
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
	}
	const int32_t hoveredMenuIndex = GetHoveredMenuIndex();
	if (inputDevice == GameInputBindings::NavigationInputDevice::Mouse &&
		hoveredMenuIndex >= 0) {
		selection_ = hoveredMenuIndex;
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
	leftCursor_.SetAlpha(0.72f + cursorPulse * 0.28f);
	rightCursor_.SetAlpha(0.72f + cursorPulse * 0.28f);

	const bool mouseConfirm =
		hoveredMenuIndex >= 0 &&
		GameInputBindings::IsMouseConfirmTriggered(
			Engine::InputSystem::Input::GetInstance());
	const bool confirmed =
		inputDevice == GameInputBindings::NavigationInputDevice::Mouse
		? mouseConfirm
		: confirmTriggered;
	if (confirmed) {
		switch (selection_) {
		case 0: return PauseMenuAction::Resume;
		case 1: return PauseMenuAction::Restart;
		case 2: return PauseMenuAction::Exit;
		default: break;
		}
	}
	if (cancelTriggered) {
		return PauseMenuAction::Resume;
	}
	return PauseMenuAction::None;
}

void PauseBuildHud::UpdateBuildIcons(
	const PlayerManager& playerManager,
	float animationTime)
{
	TrackAcquisitions(playerManager);
	for (auto& icon : icons_) {
		if (icon) {
			icon->SetColor({ 1.0f, 1.0f, 1.0f, 0.0f });
		}
	}
	for (auto& digit : levelDigits_) {
		if (digit) {
			digit->SetColor({ 1.0f, 1.0f, 1.0f, 0.0f });
		}
	}
	const std::array<int32_t, kIconCount> levels{
		playerManager.GetNormalBulletLevel(),
		playerManager.GetOrbitBulletLevel(),
		playerManager.GetDroneLevel(),
		playerManager.GetLightningLevel(),
		playerManager.GetExplosiveBulletLevel(),
		playerManager.GetMaxHPUpgradeLevel(),
		playerManager.GetAttackPowerUpgradeLevel(),
		playerManager.GetMoveSpeedLevel(),
		static_cast<int32_t>((playerManager.GetExpPickupRangeMultiplier() - baselineExpPickupRangeMultiplier_) / 0.25f + 0.5f),
	};
	auto placeRow = [this, &levels](const std::vector<int32_t>& order, float y) {
		for (size_t orderIndex = 0; orderIndex < order.size(); ++orderIndex) {
			const int32_t iconIndex = order[orderIndex];
			auto& icon = icons_[static_cast<size_t>(iconIndex)];
			if (!icon) {
				continue;
			}
			icon->SetPosition({
				layout_.position.x + layout_.stepX * static_cast<float>(orderIndex), y });
			icon->SetSize(layout_.iconSize);
			icon->SetColor({ 1.0f, 1.0f, 1.0f, layout_.visible ? 1.0f : 0.0f });
			auto& digit = levelDigits_[static_cast<size_t>(iconIndex)];
			if (digit) {
				DigitSpriteUtil::SetDigitSprite(
					*digit, 24.0f, { 24.0f, 32.0f },
					std::clamp(levels[static_cast<size_t>(iconIndex)], 0, 9));
				digit->SetPosition({
					layout_.position.x + layout_.stepX * static_cast<float>(orderIndex) +
						(layout_.iconSize.x - 18.0f) * 0.5f,
					y + layout_.iconSize.y + 4.0f,
				});
				digit->SetSize({ 18.0f, 24.0f });
				digit->SetColor({ 1.0f, 0.88f, 0.2f, layout_.visible ? 1.0f : 0.0f });
			}
		}
	};
	placeRow(weaponAcquisitionOrder_, layout_.position.y);
	placeRow(itemAcquisitionOrder_, layout_.position.y + layout_.stepY);
	(void)animationTime;
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
	if (!layout_.visible) {
		leftCursor_.Draw();
		rightCursor_.Draw();
		return;
	}
	for (auto& icon : icons_) {
		if (icon) {
			icon->Update();
			icon->Draw();
		}
	}
	for (auto& digit : levelDigits_) {
		if (digit) {
			digit->Update();
			digit->Draw();
		}
	}
	leftCursor_.Draw();
	rightCursor_.Draw();
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
