#include "game/directxgame/core/GameplaySceneDebugPanel.h"
#include "game/directxgame/enemy/EnemyManager.h"
#ifdef _DEBUG
#include <imgui.h>
#endif

namespace DirectXGame {

void GameplaySceneDebugPanel::DrawSettings(
	bool* open,
	bool& freezeGameplay,
	const char* stateName)
{
#ifdef _DEBUG
	if (!open || !*open) {
		return;
	}

	ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(260.0f, 92.0f), ImGuiCond_FirstUseEver);
	ImGui::Begin("シーン設定", open);
	if (ImGui::Button(
			freezeGameplay ? "Resume Gameplay" : "Freeze Gameplay",
			ImVec2(-1.0f, 32.0f))) {
		freezeGameplay = !freezeGameplay;
	}
	ImGui::Text("Freeze: %s", freezeGameplay ? "true" : "false");
	ImGui::Text("State: %s", stateName);
	ImGui::End();
#else
	(void)open;
	(void)freezeGameplay;
	(void)stateName;
#endif
}

GameplayDebugAction GameplaySceneDebugPanel::Draw(
	bool* open,
	EnemyManager* enemyManager)
{
#ifdef _DEBUG
	if (!open || !*open) {
		return GameplayDebugAction::None;
	}

	ImGui::SetNextWindowPos(ImVec2(12.0f, 120.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(360.0f, 220.0f), ImGuiCond_FirstUseEver);
	ImGui::Begin("シーン固有デバッグ", open);
	if (enemyManager) {
		ImGui::Separator();
		ImGui::Text(
			"Enemies: %zu / %zu",
			enemyManager->GetActiveEnemyCount(),
			enemyManager->GetEnemies().size());
		ImGui::Text(
			"EXP Orbs: %zu / %zu",
			enemyManager->GetExpOrbCount(),
			EnemyManager::kMaxExpOrbs);
		ImGui::Text("Kills: %d", enemyManager->GetTotalKillCount());
		if (ImGui::Button("Debug Damage All Enemies")) {
			enemyManager->DamageAllEnemies(999);
		}
	}

	GameplayDebugAction action = GameplayDebugAction::None;
	if (ImGui::Button("Go To Result")) {
		action = GameplayDebugAction::GoToResult;
	}
	ImGui::SameLine();
	if (ImGui::Button("Force Time Up")) {
		action = GameplayDebugAction::ForceTimeUp;
	}
	ImGui::SameLine();
	if (ImGui::Button("Back To Title")) {
		action = GameplayDebugAction::BackToTitle;
	}
	ImGui::End();
	return action;
#else
	(void)open;
	(void)enemyManager;
	return GameplayDebugAction::None;
#endif
}

}
