#include "game/directxgame/core/GameplayPlayerDebugPanel.h"
#include "game/directxgame/player/Player.h"
#include "game/directxgame/player/PlayerManager.h"
#include <iterator>
#ifdef _DEBUG
#include <imgui.h>
#endif

namespace DirectXGame {

void GameplayPlayerDebugPanel::Draw(
	bool* objectViewOpen,
	bool* objectSettingsOpen,
	Player* player,
	PlayerManager* playerManager)
{
#ifdef _DEBUG
	if (!objectViewOpen || !objectSettingsOpen ||
		(!*objectViewOpen && !*objectSettingsOpen)) {
		return;
	}

	ImGui::SetNextWindowPos(ImVec2(1125.0f, 390.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(360.0f, 360.0f), ImGuiCond_FirstUseEver);
	bool windowOpen = *objectViewOpen || *objectSettingsOpen;
	ImGui::Begin("オブジェクトビュー / オブジェクト設定", &windowOpen);
	if (!windowOpen) {
		*objectViewOpen = false;
		*objectSettingsOpen = false;
	}

	if (player) {
		const Vector3& position = player->GetWorldPosition();
		ImGui::Separator();
		ImGui::Text(
			"Player Position: %.2f, %.2f, %.2f",
			position.x,
			position.y,
			position.z);
		ImGui::Text("Player Rotation Y: %.2f", player->GetWorldRotationY());
		ImGui::Text(
			"Aim Device: %s",
			player->GetAimInputDevice() == Player::AimInputDevice::Gamepad ?
			"Gamepad" :
			"KeyboardMouse");

		float moveSpeed = player->GetMoveSpeed();
		if (ImGui::DragFloat("Move Speed", &moveSpeed, 0.1f, 1.0f, 120.0f)) {
			player->SetMoveSpeed(moveSpeed);
		}
		bool mouseAimEnabled = player->IsMouseAimEnabled();
		if (ImGui::Checkbox("Mouse Aim", &mouseAimEnabled)) {
			player->SetMouseAimEnabled(mouseAimEnabled);
		}
		float cameraHeight = player->GetCameraHeight();
		if (ImGui::DragFloat(
				"Camera Height",
				&cameraHeight,
				0.5f,
				20.0f,
				160.0f)) {
			player->SetCameraHeight(cameraHeight);
		}
		float cameraDistance = player->GetCameraDistance();
		if (ImGui::DragFloat(
				"Camera Distance",
				&cameraDistance,
				0.5f,
				10.0f,
				120.0f)) {
			player->SetCameraDistance(cameraDistance);
		}
		float cameraPitch = player->GetCameraPitch();
		if (ImGui::DragFloat(
				"Camera Pitch",
				&cameraPitch,
				0.01f,
				0.2f,
				1.5f)) {
			player->SetCameraPitch(cameraPitch);
		}
		float cameraFollowSmoothness = player->GetCameraFollowSmoothness();
		if (ImGui::DragFloat(
				"Camera Follow Smoothness",
				&cameraFollowSmoothness,
				0.1f,
				0.0f,
				30.0f)) {
			player->SetCameraFollowSmoothness(cameraFollowSmoothness);
		}

		constexpr const char* kCameraModeLabels[] = {
			"World Back",
			"Player Back",
			"World Front",
			"Top Down",
		};
		int32_t cameraMode = static_cast<int32_t>(player->GetCameraMode());
		if (ImGui::Combo(
				"Camera Mode",
				&cameraMode,
				kCameraModeLabels,
				static_cast<int32_t>(std::size(kCameraModeLabels)))) {
			player->SetCameraMode(static_cast<Player::CameraMode>(cameraMode));
		}
	}

	if (playerManager) {
		ImGui::Separator();
		ImGui::Text(
			"HP: %d / %d",
			playerManager->GetHP(),
			playerManager->GetMaxHP());
		ImGui::Text(
			"EXP: %d / %d",
			playerManager->GetEXP(),
			playerManager->GetNextLevelEXP());
		ImGui::Text("Level: %d", playerManager->GetLevel());
		ImGui::Text(
			"Normal Lv: %d  Active: %zu",
			playerManager->GetNormalBulletLevel(),
			playerManager->GetNormalBullets().size());
		ImGui::Text(
			"Orbit Lv: %d  Active: %zu",
			playerManager->GetOrbitBulletLevel(),
			playerManager->GetOrbitBullets().size());
		ImGui::Text(
			"Drone Lv: %d  Enabled: %s",
			playerManager->GetDroneLevel(),
			playerManager->HasDrone() ? "true" : "false");
		ImGui::Text(
			"Lightning Lv: %d  Enabled: %s",
			playerManager->GetLightningLevel(),
			playerManager->HasLightning() ? "true" : "false");
		if (ImGui::Button("Add EXP +10")) {
			playerManager->AddEXP(10);
		}
		ImGui::SameLine();
		if (ImGui::Button("Normal Up")) {
			playerManager->UpgradeNormalBullets();
		}
		ImGui::SameLine();
		if (ImGui::Button("Orbit Up")) {
			playerManager->UpgradeOrbitBullets();
		}
		if (ImGui::Button("Drone Up")) {
			playerManager->UpgradeDrone();
		}
		ImGui::SameLine();
		if (ImGui::Button("Lightning Up")) {
			playerManager->UpgradeLightning();
		}
		ImGui::SameLine();
		if (ImGui::Button("Max Weapons")) {
			playerManager->MaxAllWeapons();
		}
		ImGui::SameLine();
		if (ImGui::Button("Strongest")) {
			playerManager->MakeDebugStrongest();
		}
	}
	ImGui::End();
#else
	(void)objectViewOpen;
	(void)objectSettingsOpen;
	(void)player;
	(void)playerManager;
#endif
}

}
