#include "VisualDebugPanel.h"
#include "GameParticleEffects.h"
#include "GameplayHudPresentation.h"
#include "Player.h"
#include "ExpGauge.h"
#include "HpGauge.h"
#include "KeyUI.h"
#include "MiniMap.h"
#include "PauseBuildHud.h"
#include "Timer.h"
#ifdef _DEBUG
#include <imgui.h>
#endif

namespace DirectXGame {

void DebugUI::Visual::Draw(
	bool* particleViewOpen,
	bool* spriteManagerOpen,
	bool uiInitialized,
	bool* debugDrawEnabled,
	float hitFlashTimer,
	float deathTimer,
	Player* player,
	GameParticleEffects& particleEffects,
	GameplayHudPresentation& gameplayHud,
	Timer& timer,
	HpGauge& hpGauge,
	ExpGauge& expGauge,
	KeyUI& keyUi,
	MiniMap& miniMap,
	PauseBuildHud& pauseBuildHud,
	const std::function<void()>& emitLevelUpConfetti)
{
#ifdef _DEBUG
	if (!particleViewOpen || !spriteManagerOpen ||
		(!*particleViewOpen && !*spriteManagerOpen)) {
		return;
	}

	ImGui::SetNextWindowPos(ImVec2(548.0f, 330.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(430.0f, 420.0f), ImGuiCond_FirstUseEver);
	bool windowOpen = *particleViewOpen || *spriteManagerOpen;
	ImGui::Begin("パーティクルビュー / スプライトマネージャ", &windowOpen);
	if (!windowOpen) {
		*particleViewOpen = false;
		*spriteManagerOpen = false;
	}
	if (uiInitialized) {
		ImGui::Separator();
		if (debugDrawEnabled) {
			ImGui::Checkbox(
				"DebugDraw collision / spawn range",
				debugDrawEnabled);
		}
		ImGui::Text(
			"Hit Flash: %.2f  Death Timer: %.2f",
			hitFlashTimer,
			deathTimer);
		if (ImGui::CollapsingHeader("Particle Tuning")) {
			if (player) {
				particleEffects.DrawDebugUI(player->GetWorldPosition());
			}
			if (emitLevelUpConfetti) {
				ImGui::SameLine();
				if (ImGui::Button("Emit LevelUp Confetti")) {
					emitLevelUpConfetti();
				}
			}
		}
		gameplayHud.DebugDrawImGui();
		timer.DebugDrawImGui();
		hpGauge.DebugDrawImGui();
		expGauge.DebugDrawImGui();
		keyUi.DebugDrawImGui();
		miniMap.DebugDrawImGui();
		if (ImGui::CollapsingHeader("Pause Build Icons")) {
			pauseBuildHud.DrawDebugUI();
		}
	}
	ImGui::End();
#else
	(void)particleViewOpen;
	(void)spriteManagerOpen;
	(void)uiInitialized;
	(void)debugDrawEnabled;
	(void)hitFlashTimer;
	(void)deathTimer;
	(void)player;
	(void)particleEffects;
	(void)gameplayHud;
	(void)timer;
	(void)hpGauge;
	(void)expGauge;
	(void)keyUi;
	(void)miniMap;
	(void)pauseBuildHud;
	(void)emitLevelUpConfetti;
#endif
}

}
