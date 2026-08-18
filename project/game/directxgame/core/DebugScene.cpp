#include "DebugScene.h"
#include "EnemyManager.h"
#ifdef _DEBUG
#include <imgui.h>
#endif

namespace DirectXGame {

SceneAction DebugUI::Scene::Draw(
	bool* open,
	EnemyManager* enemyManager)
{
#ifdef _DEBUG
	if (!open || !*open) {
		return SceneAction::None;
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
		enemyManager->DrawBossAttackTuningDebugUI();
	}

	SceneAction action = SceneAction::None;
	if (ImGui::Button("Go To Result")) {
		action = SceneAction::GoToResult;
	}
	ImGui::SameLine();
	if (ImGui::Button("Force Time Up")) {
		action = SceneAction::ForceTimeUp;
	}
	ImGui::SameLine();
	if (ImGui::Button("Back To Title")) {
		action = SceneAction::BackToTitle;
	}
	ImGui::End();
	return action;
#else
	(void)open;
	(void)enemyManager;
	return SceneAction::None;
#endif
}

}
