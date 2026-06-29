#include "PauseBuildHud.h"
#include "DataPaths.h"
#include "ScreenUtil.h"
#include "UILayoutIO.h"
#include "PlayerManager.h"
#include "Input.h"
#include <algorithm>
#include <cmath>
#ifdef _DEBUG
#include <imgui.h>
#endif

namespace {

constexpr float kCursorStepY = 168.0f;

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
	cursor_.Initialize("ui/game/pause_arrow.png", { 0.0f, 0.0f });

	const UILayoutIO::LayoutMap values =
		UILayoutIO::LoadOrDefault(DataPaths::kPauseLayout, {});
	layout_.position = UILayoutIO::GetVector2(
		values, "pauseBuildPosition", layout_.position);
	layout_.stepX = UILayoutIO::GetFloat(
		values, "pauseBuildStepX", layout_.stepX);
	layout_.iconSize = UILayoutIO::GetVector2(
		values, "pauseBuildIconSize", layout_.iconSize);
	layout_.visible = UILayoutIO::GetFloat(
		values, "pauseBuildVisible", layout_.visible ? 1.0f : 0.0f) > 0.5f;
	menuLayout_.hitboxPositions[0] = UILayoutIO::GetVector2(
		values, "menuHitbox0", menuLayout_.hitboxPositions[0]);
	menuLayout_.hitboxPositions[1] = UILayoutIO::GetVector2(
		values, "menuHitbox1", menuLayout_.hitboxPositions[1]);
	menuLayout_.hitboxSize = UILayoutIO::GetVector2(
		values, "menuHitboxSize", menuLayout_.hitboxSize);

	const std::array<const char*, 5> iconPaths{
		"ui/game/lvup/normal_icon.png",
		"ui/game/lvup/orbit_icon.png",
		"ui/game/lvup/drone_icon.png",
		"ui/game/lvup/lightning_icon.png",
		"ui/game/lvup/attack_icon.png",
	};
	for (size_t index = 0; index < icons_.size(); ++index) {
		icons_[index].Initialize(iconPaths[index], {});
		icons_[index].SetVisible(false);
	}
	ApplyLayout();
}

void PauseBuildHud::Start()
{
	selection_ = 0;
	cursor_.SetPosition({ 0.0f, 0.0f });
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
	cursor_.SetPosition({
		0.0f,
		selection_ == 0 ? 0.0f : kCursorStepY,
	});
	const float cursorPulse =
		0.5f + 0.5f * std::sin(animationTime * 8.0f);
	cursor_.SetScale(1.0f);
	cursor_.SetAlpha(0.72f + cursorPulse * 0.28f);

	const bool mouseConfirm =
		hoveredMenuIndex >= 0 &&
		GameInputBindings::IsMouseConfirmTriggered(
			Engine::InputSystem::Input::GetInstance());
	const bool confirmed =
		inputDevice == GameInputBindings::NavigationInputDevice::Mouse
		? mouseConfirm
		: confirmTriggered;
	if (confirmed) {
		return selection_ == 0
			? PauseMenuAction::Resume
			: PauseMenuAction::BackToTitle;
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
	const std::array<float, 5> alphas{
		1.0f,
		playerManager.HasOrbitBullets() ? 1.0f : 0.25f,
		playerManager.HasDrone() ? 1.0f : 0.25f,
		playerManager.HasLightning() ? 1.0f : 0.25f,
		playerManager.GetAttackPower() > 1 ? 1.0f : 0.4f,
	};

	for (size_t index = 0; index < icons_.size(); ++index) {
		UILabel& icon = icons_[index];
		const float pulse =
			0.5f + 0.5f * std::sin(animationTime * 3.8f + static_cast<float>(index) * 0.65f);
		const bool enabled = alphas[index] >= 0.95f;
		icon.SetVisible(layout_.visible);
		icon.SetScale(enabled ? 1.0f + pulse * 0.035f : 1.0f);
		icon.SetColor({ 1.0f, 1.0f, 1.0f, alphas[index] });
	}
}

void PauseBuildHud::Draw()
{
	overlay_.Draw();
	if (!layout_.visible) {
		cursor_.Draw();
		return;
	}
	for (UILabel& icon : icons_) {
		icon.Draw();
	}
	cursor_.Draw();
}

void PauseBuildHud::SaveLayout() const
{
	UILayoutIO::Save(DataPaths::kPauseLayout,
		{
			{ "pauseBuildPosition", { layout_.position.x, layout_.position.y } },
			{ "pauseBuildStepX", { layout_.stepX } },
			{ "pauseBuildIconSize", { layout_.iconSize.x, layout_.iconSize.y } },
			{ "pauseBuildVisible", { layout_.visible ? 1.0f : 0.0f } },
			{ "menuHitbox0", {
				menuLayout_.hitboxPositions[0].x, menuLayout_.hitboxPositions[0].y } },
			{ "menuHitbox1", {
				menuLayout_.hitboxPositions[1].x, menuLayout_.hitboxPositions[1].y } },
			{ "menuHitboxSize", {
				menuLayout_.hitboxSize.x, menuLayout_.hitboxSize.y } },
		});
}

void PauseBuildHud::MoveSelection(int32_t delta)
{
	constexpr int32_t kMaxSelection = 1;
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
				menuLayout_.hitboxSize)) {
			return index;
		}
	}
	return -1;
}

void PauseBuildHud::ApplyLayout()
{
	for (size_t index = 0; index < icons_.size(); ++index) {
		icons_[index].SetPosition({
			layout_.position.x + layout_.stepX * static_cast<float>(index),
			layout_.position.y,
		});
		icons_[index].SetSize(layout_.iconSize);
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
	if (ImGui::Button("Save Pause Layout")) {
		SaveLayout();
	}
}
#endif

}
