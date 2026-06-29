#include "StatisticsDebugPanel.h"
#include "CameraManager.h"
#include "DirectXCommon.h"
#include "Object3DCommon.h"
#include "ParticleManager.h"
#include "SpriteCommon.h"
#include "SrvManager.h"
#include "EnemyManager.h"
#include "PlayerManager.h"
#include "Drone.h"
#include <optional>
#include <string>
#include <vector>
#ifdef _DEBUG
#include <imgui.h>
#include <implot.h>
#endif

namespace DirectXGame {

void StatisticsDebugPanel::Draw(
	bool* open,
	const char* stateName,
	uint32_t gameFrameCount,
	bool autoSaveAllowed,
	EnemyManager* enemyManager,
	PlayerManager* playerManager)
{
#ifdef _DEBUG
	if (!open || !*open) {
		return;
	}

	ImGui::SetNextWindowPos(ImVec2(12.0f, 330.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(520.0f, 420.0f), ImGuiCond_FirstUseEver);
	ImGui::Begin("統計", open);

	const SoftCapTelemetry::Snapshot softCap =
		softCapTelemetry_.Capture(gameFrameCount, enemyManager, playerManager);
	Engine::Particle::ParticleManager* particleManager =
		Engine::Particle::ParticleManager::GetInstance();
	Engine::Base::DirectXCommon* dxCommon =
		Engine::Graphics2D::SpriteCommon::GetInstance()->GetDxCommon();

	ImGui::Text("State: %s", stateName);
	ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
	if (dxCommon && ImGui::CollapsingHeader("FPS Limit", ImGuiTreeNodeFlags_DefaultOpen)) {
		bool frameLimitEnabled = dxCommon->IsFrameRateLimitEnabled();
		if (ImGui::Checkbox("Frame Rate Limit", &frameLimitEnabled)) {
			dxCommon->SetFrameRateLimitEnabled(frameLimitEnabled);
		}

		float targetFrameRate = dxCommon->GetTargetFrameRate();
		if (ImGui::SliderFloat(
				"Target FPS",
				&targetFrameRate,
				15.0f,
				240.0f,
				"%.0f")) {
			dxCommon->SetTargetFrameRate(targetFrameRate);
		}

		bool vSyncEnabled = dxCommon->IsVSyncEnabled();
		if (ImGui::Checkbox("VSync", &vSyncEnabled)) {
			dxCommon->SetVSyncEnabled(vSyncEnabled);
		}
		ImGui::Text(
			"Effective cap: %s%s",
			frameLimitEnabled ? "Target FPS" : "Unlimited",
			vSyncEnabled ? " + VSync" : "");
	}

	fpsHistory_[fpsHistoryOffset_] = ImGui::GetIO().Framerate;
	fpsHistoryOffset_ =
		(fpsHistoryOffset_ + 1) % static_cast<int32_t>(fpsHistory_.size());
	if (ImPlot::BeginPlot(
			"FPS History",
			ImVec2(-1.0f, 140.0f),
			ImPlotFlags_NoLegend)) {
		ImPlot::SetupAxes("Frame", "FPS");
		ImPlot::PlotLine(
			"FPS",
			fpsHistory_.data(),
			static_cast<int>(fpsHistory_.size()));
		ImPlot::EndPlot();
	}

	ImGui::Text(
		"Enemies: %zu  EXP Orbs: %zu / %zu  Kills: %d",
		softCap.enemyCount,
		softCap.expOrbCount,
		EnemyManager::kMaxExpOrbs,
		softCap.killCount);
	ImGui::Text(
		"EXP Orb Peak: %zu  Pruned: %zu",
		softCap.expOrbPeak,
		softCap.expOrbPrunes);
	ImGui::Text(
		"Bullets: Normal %zu / %zu | Orbit %zu | Drone %zu / %zu",
		softCap.normalBulletCount,
		PlayerManager::kMaxActiveNormalBullets,
		softCap.orbitBulletCount,
		softCap.droneBulletCount,
		Drone::kMaxActiveBullets);
	ImGui::Text(
		"Bullet Peaks: Normal %zu / Drone %zu  Pruned: Normal %zu / Drone %zu",
		softCap.normalBulletPeak,
		softCap.droneBulletPeak,
		softCap.normalBulletPrunes,
		softCap.droneBulletPrunes);
	ImGui::Text(
		"Prune Rate/min: EXP %.1f | Normal %.1f | Drone %.1f",
		softCap.expOrbPrunesPerMinute,
		softCap.normalBulletPrunesPerMinute,
		softCap.droneBulletPrunesPerMinute);
	softCapTelemetry_.DrawControls(
		softCap,
		stateName,
		playerManager ? playerManager->GetLevel() : 0,
		autoSaveAllowed,
		enemyManager,
		playerManager);
	if (softCap.expOrbCount >= EnemyManager::kMaxExpOrbs ||
		softCap.normalBulletCount >= PlayerManager::kMaxActiveNormalBullets ||
		softCap.droneBulletCount >= Drone::kMaxActiveBullets) {
		ImGui::TextColored(
			ImVec4(1.0f, 0.75f, 0.25f, 1.0f),
			"Soft cap currently active");
	}

	if (particleManager) {
		ImGui::Text(
			"Particles: %zu active across %zu groups",
			particleManager->GetTotalActiveParticleCount(),
			particleManager->GetParticleGroupCount());
		bool useFixedParticleDelta = particleManager->IsUsingFixedDeltaTime();
		if (ImGui::Checkbox("Use Fixed Particle Delta", &useFixedParticleDelta)) {
			particleManager->SetUseFixedDeltaTime(useFixedParticleDelta);
		}
		ImGui::Text(
			"Particle Delta: %.4f sec  fixed %.4f sec",
			particleManager->GetLastAppliedDeltaTime(),
			particleManager->GetFixedDeltaTime());
		if (ImGui::CollapsingHeader("Render Debug")) {
			const Engine::Graphics3D::Object3DCommon::ShadowPassStats& shadowStats =
				Engine::Graphics3D::Object3DCommon::GetInstance()->GetShadowPassStats();
			const std::string& activeCameraName =
				Engine::CameraSystem::CameraManager::GetInstance()->GetActiveCameraName();
			ImGui::Text(
				"Active Camera: %s",
				activeCameraName.empty() ? "none" : activeCameraName.c_str());
			ImGui::Text(
				"Particle Draw Calls: %u",
				particleManager->GetLastDrawCallCount());
			ImGui::Text(
				"Particle Drawn Instances: %u",
				particleManager->GetLastDrawnInstanceCount());
			ImGui::Text(
				"Shadow Casters: %u submitted / %u candidates (%u culled)",
				shadowStats.submittedCount,
				shadowStats.candidateCount,
				shadowStats.culledCount);
		}

		if (ImGui::TreeNode("Particle Groups")) {
			constexpr std::array<const char*, 12> kParticleGroups{
				"DirectXGame.Ripple",
				"DirectXGame.Spark",
				"DirectXGame.EnemyHitSpark",
				"DirectXGame.ExpSpark",
				"DirectXGame.LightningImpact",
				"DirectXGame.PlayerDeathSpark",
				"DirectXGame.DeathSmoke",
				"DirectXGame.Confetti",
				"DirectXGame.SuicideEnemyTrail",
			};
			for (const char* groupName : kParticleGroups) {
				const std::optional<size_t> activeCount =
					particleManager->GetActiveParticleCount(groupName);
				const std::optional<uint32_t> maxInstanceCount =
					particleManager->GetParticleGroupMaxInstanceCount(groupName);
				const std::optional<std::string> debugName =
					particleManager->GetParticleGroupDebugName(groupName);
				ImGui::Text(
					"%s: %zu / %u",
					debugName.value_or(groupName).c_str(),
					activeCount.value_or(0),
					maxInstanceCount.value_or(0));
			}
			ImGui::TreePop();
		}
	}

	if (Engine::Base::SrvManager* srvManager =
		Engine::Graphics3D::Object3DCommon::GetInstance()->GetSrvManager()) {
		ImGui::Text(
			"SRV: %u / %u used  remaining %u",
			srvManager->GetUsedCount(),
			srvManager->GetMaxCount(),
			srvManager->GetRemainingCount());
		if (ImGui::TreeNode("SRV Usage")) {
			const std::vector<Engine::Base::SrvManager::UsageRecord>& records =
				srvManager->GetUsageRecords();
			const size_t firstIndex = records.size() > 24 ? records.size() - 24 : 0;
			for (size_t index = firstIndex; index < records.size(); ++index) {
				ImGui::Text(
					"#%u %s",
					records[index].index,
					records[index].usage.c_str());
			}
			ImGui::TreePop();
		}
	}

	if (playerManager) {
		ImGui::Text(
			"Weapons: Normal Lv%d Dmg%d Interval %.2f",
			playerManager->GetNormalBulletLevel(),
			playerManager->GetNormalBulletDamage(),
			playerManager->GetNormalBulletInterval());
		ImGui::Text(
			"Orbit Lv%d Dmg%d | Drone Lv%d Dmg%d | Lightning Lv%d Dmg%d Count%d Radius %.1f",
			playerManager->GetOrbitBulletLevel(),
			playerManager->GetOrbitBulletDamage(),
			playerManager->GetDroneLevel(),
			playerManager->GetDroneDamage(),
			playerManager->GetLightningLevel(),
			playerManager->GetLightningDamage(),
			playerManager->GetLightningStrikeCount(),
			playerManager->GetLightningRadius());
	}

	ImGui::End();
#else
	(void)open;
	(void)stateName;
	(void)gameFrameCount;
	(void)autoSaveAllowed;
	(void)enemyManager;
	(void)playerManager;
#endif
}

}
