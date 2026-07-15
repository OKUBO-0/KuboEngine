#include "DebugRuntime.h"
#include "DataPaths.h"
#include "Enemy.h"
#include "EnemyManager.h"
#include "EnemyView.h"
#include "PlayerManager.h"
#include "UILayoutIO.h"
#include <algorithm>
#include <vector>
#ifdef _DEBUG
#include <imgui.h>
#include <imgui_node_editor.h>
#endif

namespace DirectXGame {

void DebugUI::Runtime::DrawObjectManager(
	bool* open,
	const RuntimeObjectStatus& status,
	EnemyManager* enemyManager)
{
#ifdef _DEBUG
	if (!open || !*open) {
		return;
	}

	ImGui::SetNextWindowPos(ImVec2(920.0f, 460.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(360.0f, 260.0f), ImGuiCond_FirstUseEver);
	ImGui::Begin("オブジェクトマネージャ", open);
	if (ImGui::TreeNode("Scene Objects")) {
		ImGui::BulletText("Player: %s", status.playerLoaded ? "loaded" : "none");
		ImGui::BulletText(
			"PlayerManager: %s",
			status.playerManagerLoaded ? "loaded" : "none");
		ImGui::BulletText(
			"EnemyManager: %s",
			status.enemyManagerLoaded ? "loaded" : "none");
		ImGui::BulletText("GridPlane: %s", status.gridPlaneLoaded ? "loaded" : "none");
		ImGui::BulletText("SkyDome: %s", status.skyDomeLoaded ? "loaded" : "none");
		ImGui::BulletText(
			"CurtainTransition: %s",
			status.curtainLoaded ? "loaded" : "none");
		ImGui::TreePop();
	}
	if (enemyManager && ImGui::TreeNode("Enemies")) {
		const auto& enemies = enemyManager->GetEnemies();
		ImGui::Text(
			"Active: %zu / %zu",
			enemyManager->GetActiveEnemyCount(),
			enemies.size());
		const size_t previewCount = (std::min)(enemies.size(), size_t{ 12 });
		for (size_t index = 0; index < previewCount; ++index) {
			const Enemy* enemy = enemies[index].get();
			if (!enemy) {
				continue;
			}
			const Vector3& position = enemy->GetPosition();
			ImGui::Text(
				"#%zu %s HP:%d Pos: %.1f, %.1f, %.1f",
				index,
				enemy->IsActive() ? "Active" : "Inactive",
				enemy->GetHP(),
				position.x,
				position.y,
				position.z);
		}
		if (enemies.size() > previewCount) {
			ImGui::Text("... %zu more", enemies.size() - previewCount);
		}
		ImGui::TreePop();
	}
	if (enemyManager && ImGui::TreeNode("EXP Orbs")) {
		ImGui::Text(
			"Active: %zu / %zu",
			enemyManager->GetExpOrbCount(),
			EnemyManager::kMaxExpOrbs);
		ImGui::Text(
			"Peak: %zu  Pruned: %zu",
			enemyManager->GetPeakExpOrbCount(),
			enemyManager->GetExpOrbPruneCount());
		if (ImGui::Button("Reset Orb Telemetry")) {
			enemyManager->ResetExpOrbTelemetry();
		}
		ImGui::TreePop();
	}
	ImGui::End();
#else
	(void)open;
	(void)status;
	(void)enemyManager;
#endif
}

void DebugUI::Runtime::DrawMotionEditor(bool* open)
{
#ifdef _DEBUG
	if (!open || !*open) {
		return;
	}

	ImGui::SetNextWindowPos(ImVec2(1255.0f, 120.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(320.0f, 140.0f), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("モーションエディター", open)) {
		ImGui::TextUnformatted("モーション編集ビューです。");
		ax::NodeEditor::Begin("MotionGraph");
		ax::NodeEditor::BeginNode(1);
		ImGui::TextUnformatted("Start");
		ax::NodeEditor::BeginPin(11, ax::NodeEditor::PinKind::Output);
		ImGui::TextUnformatted("Out");
		ax::NodeEditor::EndPin();
		ax::NodeEditor::EndNode();
		ax::NodeEditor::BeginNode(2);
		ax::NodeEditor::BeginPin(21, ax::NodeEditor::PinKind::Input);
		ImGui::TextUnformatted("In");
		ax::NodeEditor::EndPin();
		ImGui::TextUnformatted("Death Camera");
		ax::NodeEditor::EndNode();
		ax::NodeEditor::Link(100, 11, 21);
		ax::NodeEditor::End();
	}
	ImGui::End();
#else
	(void)open;
#endif
}

void DebugUI::Runtime::DrawColliderManager(
	bool* open,
	bool* debugDrawEnabled,
	const EnemyManager* enemyManager,
	const PlayerManager* playerManager)
{
#ifdef _DEBUG
	if (!open || !*open || !debugDrawEnabled) {
		return;
	}

	ImGui::SetNextWindowPos(ImVec2(1255.0f, 280.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(380.0f, 360.0f), ImGuiCond_FirstUseEver);
	ImGui::Begin("コライダー・当たり判定", open);
	ImGui::Checkbox("DebugDraw collision / spawn range", debugDrawEnabled);
	ImGui::SeparatorText("Runtime Tags");
	ImGui::BulletText("Player");
	ImGui::BulletText("Enemy");
	ImGui::BulletText("PlayerBullet");
	ImGui::BulletText("ExpOrb");
	ImGui::SeparatorText("Collision State");
	if (enemyManager) {
		ImGui::Text("Enemies: %zu active", enemyManager->GetActiveEnemyCount());
		ImGui::Text("EXP Orbs: %zu active", enemyManager->GetExpOrbCount());
		ImGui::Text(
			"Recent Hit Effects: %zu",
			enemyManager->GetRecentHitEffectPositions().size());
		ImGui::Text(
			"Recent Death Effects: %zu",
			enemyManager->GetRecentDeathEffectPositions().size());
	}
	if (playerManager) {
		ImGui::Text(
			"Bow Arrow Projectiles: %zu",
			playerManager->GetNormalBullets().size());
		ImGui::Text(
			"Rock Projectiles: %zu",
			playerManager->GetOrbitBullets().size());
	}
	ImGui::SeparatorText("Boss Visual Tuning");
	float octopusOffsetY = EnemyView::GetOctopusModelGroundOffsetY();
	if (ImGui::DragFloat(
			"Octopus Ground Offset Y",
			&octopusOffsetY,
			0.01f,
			0.0f,
			8.0f,
			"%.3f")) {
		EnemyView::SetOctopusModelGroundOffsetY(octopusOffsetY);
	}
	ImGui::Text("Raise if buried, lower if floating.");
	if (ImGui::Button("Save Boss Visual Tuning")) {
		std::vector<UILayoutIO::Entry> entries;
		EnemyView::AppendVisualTuningEntries(entries);
		UILayoutIO::Save(DataPaths::kDebugTuning, entries);
	}
	ImGui::SameLine();
	if (ImGui::Button("Reset Offset")) {
		EnemyView::SetOctopusModelGroundOffsetY(4.7f);
	}
	ImGui::End();
#else
	(void)open;
	(void)debugDrawEnabled;
	(void)enemyManager;
	(void)playerManager;
#endif
}

}
