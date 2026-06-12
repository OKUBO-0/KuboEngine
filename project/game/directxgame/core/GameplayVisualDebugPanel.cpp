#include "game/directxgame/core/GameplayVisualDebugPanel.h"
#include "game/directxgame/effects/GameParticleEffects.h"
#include "game/directxgame/player/Player.h"
#include "game/directxgame/ui/gauge/ExpGauge.h"
#include "game/directxgame/ui/gauge/HpGauge.h"
#include "game/directxgame/ui/hud/KeyUI.h"
#include "game/directxgame/ui/hud/MiniMap.h"
#include "game/directxgame/ui/hud/PauseBuildHud.h"
#include "game/directxgame/ui/hud/Timer.h"
#ifdef _DEBUG
#include <imgui.h>
#endif

namespace DirectXGame {

void GameplayVisualDebugPanel::Draw(
	bool* particleViewOpen,
	bool* spriteManagerOpen,
	bool uiInitialized,
	bool* debugDrawEnabled,
	float hitFlashTimer,
	float deathTimer,
	Player* player,
	GameParticleEffects& particleEffects,
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
				particleEffects.DrawDebugUi(player->GetWorldPosition());
			}
			if (emitLevelUpConfetti) {
				ImGui::SameLine();
				if (ImGui::Button("Emit LevelUp Confetti")) {
					emitLevelUpConfetti();
				}
			}
		}
		timer.DebugDrawImGui();
		hpGauge.DebugDrawImGui();
		expGauge.DebugDrawImGui();
		keyUi.DebugDrawImGui();
		miniMap.DebugDrawImGui();
		if (ImGui::CollapsingHeader("Pause Build Icons")) {
			pauseBuildHud.DrawDebugUi();
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
