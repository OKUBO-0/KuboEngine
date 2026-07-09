#include "PlayerDebugPanel.h"
#include "Player.h"
#include "PlayerManager.h"
#include <iterator>
#ifdef _DEBUG
#include <imgui.h>
#endif

namespace DirectXGame {

void DebugUI::PlayerPanel::Draw(
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

	if (*objectViewOpen) {
		ImGui::SetNextWindowPos(ImVec2(1125.0f, 390.0f), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(360.0f, 310.0f), ImGuiCond_FirstUseEver);
		ImGui::Begin("プレイヤー状態", objectViewOpen);
		if (player) {
		const Vector3& position = player->GetWorldPosition();
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
		}
		if (playerManager) {
			ImGui::SeparatorText("ステータス");
			ImGui::Text("HP: %d / %d", playerManager->GetHP(), playerManager->GetMaxHP());
			ImGui::Text("EXP: %d / %d", playerManager->GetEXP(), playerManager->GetNextLevelEXP());
			ImGui::Text("Level: %d", playerManager->GetLevel());
			const PlayerStats& stats = playerManager->GetStats();
			ImGui::Text("DMG x%.2f  ASPD x%.2f  Duration x%.2f",
				stats.GetDamageMultiplier(), stats.GetAttackSpeedMultiplier(), stats.GetDurationMultiplier());
			ImGui::Text("Move x%.2f  Projectile x%.2f  Size x%.2f  Count %+d",
				stats.GetMovementSpeedMultiplier(), stats.GetProjectileSpeedMultiplier(),
				stats.GetAreaSizeMultiplier(), stats.GetProjectileCountBonus());
			ImGui::Text("Pickup x%.2f  EXP x%.2f  Coin x%.2f  Crit %.1f%% / x%.2f",
				stats.GetPickupRangeMultiplier(), stats.GetExpGainMultiplier(),
				stats.GetCoinGainMultiplier(), stats.GetCritChance() * 100.0f,
				stats.GetCritDamageMultiplier());
			ImGui::SeparatorText("武器状態");
			ImGui::Text("Bow Arrow Lv: %d  Active: %zu", playerManager->GetNormalBulletLevel(), playerManager->GetNormalBullets().size());
			ImGui::Text("Rock Lv: %d  Active: %zu", playerManager->GetOrbitBulletLevel(), playerManager->GetOrbitBullets().size());
			ImGui::Text("Thunder Staff Lv: %d  Enabled: %s", playerManager->GetLightningLevel(), playerManager->HasLightning() ? "true" : "false");
			ImGui::Text("Sword Lv: %d  Enabled: %s", playerManager->GetSwordLevel(), playerManager->HasSword() ? "true" : "false");
			ImGui::Text("Aura Lv: %d  Enabled: %s", playerManager->GetAuraLevel(), playerManager->HasAura() ? "true" : "false");
			ImGui::Text("Flame Shoes Lv: %d  Zones: %zu", playerManager->GetFlameShoesLevel(), playerManager->GetFlameZoneCount());
			ImGui::Text("Bone Lv: %d  Bullets: %zu", playerManager->GetBoneLevel(), playerManager->GetBoneBullets().size());
			ImGui::Text("Handgun Lv: %d  Bullets: %zu", playerManager->GetHandgunLevel(), playerManager->GetHandgunBullets().size());
			ImGui::Text("Boomerang Lv: %d  Bullets: %zu", playerManager->GetBoomerangLevel(), playerManager->GetBoomerangBullets().size());
		}
		ImGui::End();
	}

	if (*objectSettingsOpen) {
		ImGui::SetNextWindowPos(ImVec2(1495.0f, 390.0f), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(360.0f, 360.0f), ImGuiCond_FirstUseEver);
		ImGui::Begin("プレイヤー設定・武器デバッグ", objectSettingsOpen);
		if (player) {
			ImGui::SeparatorText("移動・照準");

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
		float cameraLookSmoothing = player->GetCameraLookSmoothing();
		if (ImGui::DragFloat(
				"Camera Look Smoothing",
				&cameraLookSmoothing,
				0.01f,
				0.0f,
				0.95f)) {
			player->SetCameraLookSmoothing(cameraLookSmoothing);
		}

		constexpr const char* kCameraModeLabels[] = {
			"World Back",
			"Player Back",
			"World Front",
			"Top Down",
			"Megabonk",
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
		ImGui::SeparatorText("ゲームプレイ操作");
		if (ImGui::Button("Add EXP +10")) {
			playerManager->AddEXP(10);
		}
		ImGui::SameLine();
		if (ImGui::Button("Bow Arrow Up")) {
			playerManager->UpgradeNormalBullets();
		}
		ImGui::SameLine();
		if (ImGui::Button("Rock Up")) {
			playerManager->UpgradeOrbitBullets();
		}
		if (ImGui::Button("Thunder Staff Up")) {
			playerManager->UpgradeLightning();
		}
		ImGui::SameLine();
		if (ImGui::Button("Sword Up")) {
			playerManager->UpgradeSword();
		}
		ImGui::SameLine();
		if (ImGui::Button("Aura Up")) {
			playerManager->UpgradeAura();
		}
		if (ImGui::Button("Flame Shoes Up")) {
			playerManager->UpgradeFlameShoes();
		}
		if (ImGui::Button("Bone Up")) playerManager->UpgradeBone();
		ImGui::SameLine();
		if (ImGui::Button("Handgun Up")) playerManager->UpgradeHandgun();
		ImGui::SameLine();
		if (ImGui::Button("Boomerang Up")) playerManager->UpgradeBoomerang();
		if (ImGui::Button("Max Weapons")) {
			playerManager->MaxAllWeapons();
		}
		ImGui::SameLine();
		if (ImGui::Button("Strongest")) {
			playerManager->MakeDebugStrongest();
		}
		}
		ImGui::End();
	}
#else
	(void)objectViewOpen;
	(void)objectSettingsOpen;
	(void)player;
	(void)playerManager;
#endif
}

}
