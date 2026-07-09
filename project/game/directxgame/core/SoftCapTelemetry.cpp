#include "SoftCapTelemetry.h"
#include "ParticleManager.h"
#include "DataPaths.h"
#include "EnemyManager.h"
#include "PlayerManager.h"
#include <algorithm>
#include <fstream>
#include <string>
#ifdef _DEBUG
#include <imgui.h>
#endif

namespace DirectXGame {

SoftCapTelemetry::Snapshot SoftCapTelemetry::Capture(
	uint32_t frame,
	const EnemyManager* enemyManager,
	const PlayerManager* playerManager) const
{
	Snapshot snapshot{};
	snapshot.frame = frame;
	snapshot.elapsedFrames = frame >= startFrame_ ? frame - startFrame_ : frame;
	snapshot.elapsedMinutes =
		static_cast<float>(snapshot.elapsedFrames) / (60.0f * 60.0f);
	const float rateDivisor = (std::max)(snapshot.elapsedMinutes, 0.01f);

	if (enemyManager) {
		snapshot.enemyCount = enemyManager->GetActiveEnemyCount();
		snapshot.killCount = enemyManager->GetTotalKillCount();
		snapshot.expOrbCount = enemyManager->GetExpOrbCount();
		snapshot.expOrbPeak = enemyManager->GetPeakExpOrbCount();
		snapshot.expOrbPrunes = enemyManager->GetExpOrbPruneCount();
		snapshot.expOrbPrunesPerMinute =
			static_cast<float>(snapshot.expOrbPrunes) / rateDivisor;
		const EnemyCollisionContext::Telemetry& collision =
			enemyManager->GetCollisionTelemetry();
		snapshot.collisionQueryCount = collision.queryCount;
		snapshot.nearbyCandidateCount = collision.nearbyCandidateCount;
		snapshot.bruteForceCandidateCount = collision.bruteForceCandidateCount;
		snapshot.collisionCandidateReductionPercent =
			collision.CandidateReductionPercent();
		snapshot.spatialBuildMilliseconds = collision.spatialBuildMilliseconds;
		snapshot.collisionMilliseconds = collision.collisionMilliseconds;
		snapshot.separationMilliseconds = collision.separationMilliseconds;
		snapshot.collisionBruteForceMode =
			enemyManager->GetBroadPhaseMode() == EnemyBroadPhaseMode::BruteForce;
	}
	if (playerManager) {
		snapshot.normalBulletCount = playerManager->GetNormalBullets().size();
		snapshot.normalBulletPeak = playerManager->GetPeakNormalBulletCount();
		snapshot.normalBulletPrunes = playerManager->GetNormalBulletPruneCount();
		snapshot.normalBulletPrunesPerMinute =
			static_cast<float>(snapshot.normalBulletPrunes) / rateDivisor;
		snapshot.orbitBulletCount = playerManager->GetOrbitBullets().size();
	}
	if (Engine::Particle::ParticleManager* particleManager =
		Engine::Particle::ParticleManager::GetInstance()) {
		snapshot.particleCount = particleManager->GetTotalActiveParticleCount();
	}
	return snapshot;
}

void SoftCapTelemetry::Reset(
	uint32_t frame,
	EnemyManager* enemyManager,
	PlayerManager* playerManager)
{
	if (enemyManager) {
		enemyManager->ResetExpOrbTelemetry();
	}
	if (playerManager) {
		playerManager->ResetBulletTelemetry();
	}
	startFrame_ = frame;
	nextAutoSaveFrame_ =
		frame + static_cast<uint32_t>(autoSaveIntervalSeconds_ * 60);
	lastAutoSaveFrame_ = UINT32_MAX;
}

void SoftCapTelemetry::SaveCsv(
	const Snapshot& snapshot,
	const char* stateName,
	int32_t level) const
{
#ifdef _DEBUG
	const std::string path = DataPaths::Resolve(DataPaths::kSoftCapTelemetry);
	bool writeHeader = true;
	{
		std::ifstream existing(path);
		writeHeader =
			!existing.good() || existing.peek() == std::ifstream::traits_type::eof();
	}

	std::ofstream file(path, std::ios::app);
	if (!file.is_open()) {
		return;
	}
	if (writeHeader) {
		file << "frame,telemetryFrames,telemetryMinutes,state,level,enemyCount,killCount,"
			"expOrbCount,expOrbCap,expOrbPeak,expOrbPrunes,expOrbPrunesPerMinute,"
			"normalBulletCount,normalBulletCap,normalBulletPeak,normalBulletPrunes,normalBulletPrunesPerMinute,"
			"particleCount\n";
	}
	file << snapshot.frame << ','
		<< snapshot.elapsedFrames << ','
		<< snapshot.elapsedMinutes << ','
		<< stateName << ','
		<< level << ','
		<< snapshot.enemyCount << ','
		<< snapshot.killCount << ','
		<< snapshot.expOrbCount << ','
		<< EnemyManager::kMaxExpOrbs << ','
		<< snapshot.expOrbPeak << ','
		<< snapshot.expOrbPrunes << ','
		<< snapshot.expOrbPrunesPerMinute << ','
		<< snapshot.normalBulletCount << ','
		<< PlayerManager::kMaxActiveNormalBullets << ','
		<< snapshot.normalBulletPeak << ','
		<< snapshot.normalBulletPrunes << ','
		<< snapshot.normalBulletPrunesPerMinute << ','
		<< snapshot.particleCount << '\n';

	const std::string collisionPath =
		DataPaths::Resolve(DataPaths::kCollisionTelemetry);
	bool writeCollisionHeader = true;
	{
		std::ifstream existing(collisionPath);
		writeCollisionHeader = !existing.good() ||
			existing.peek() == std::ifstream::traits_type::eof();
	}
	std::ofstream collisionFile(collisionPath, std::ios::app);
	if (collisionFile.is_open()) {
		if (writeCollisionHeader) {
			collisionFile
				<< "frame,mode,state,level,enemyCount,normalBulletCount,orbitBulletCount,"
				"queryCount,nearbyCandidateCount,bruteForceCandidateCount,"
				"candidateReductionPercent,spatialBuildMilliseconds,collisionMilliseconds,"
				"separationMilliseconds\n";
		}
		collisionFile
			<< snapshot.frame << ','
			<< (snapshot.collisionBruteForceMode
				? "brute_force" : "spatial_grid") << ','
			<< stateName << ','
			<< level << ','
			<< snapshot.enemyCount << ','
			<< snapshot.normalBulletCount << ','
			<< snapshot.orbitBulletCount << ','
			<< snapshot.collisionQueryCount << ','
			<< snapshot.nearbyCandidateCount << ','
			<< snapshot.bruteForceCandidateCount << ','
			<< snapshot.collisionCandidateReductionPercent << ','
			<< snapshot.spatialBuildMilliseconds << ','
			<< snapshot.collisionMilliseconds << ','
			<< snapshot.separationMilliseconds << '\n';
	}
#else
	(void)snapshot;
	(void)stateName;
	(void)level;
#endif
}

#ifdef _DEBUG
void SoftCapTelemetry::DrawControls(
	const Snapshot& snapshot,
	const char* stateName,
	int32_t level,
	bool autoSaveAllowed,
	EnemyManager* enemyManager,
	PlayerManager* playerManager)
{
	if (ImGui::Button("Reset Soft Cap Telemetry")) {
		Reset(snapshot.frame, enemyManager, playerManager);
	}
	ImGui::SameLine();
	if (ImGui::Button("Save Soft Cap CSV")) {
		SaveCsv(snapshot, stateName, level);
	}
	if (ImGui::Checkbox("Auto Save Soft Cap CSV", &autoSaveEnabled_)) {
		nextAutoSaveFrame_ = snapshot.frame;
		lastAutoSaveFrame_ = UINT32_MAX;
	}
	if (ImGui::DragInt(
			"Auto Save Interval Sec",
			&autoSaveIntervalSeconds_,
			1.0f,
			5,
			300)) {
		autoSaveIntervalSeconds_ = (std::max)(5, autoSaveIntervalSeconds_);
		nextAutoSaveFrame_ =
			snapshot.frame + static_cast<uint32_t>(autoSaveIntervalSeconds_ * 60);
	}
	if (autoSaveEnabled_) {
		const uint32_t framesUntilNext =
			snapshot.frame < nextAutoSaveFrame_ ?
			nextAutoSaveFrame_ - snapshot.frame :
			0u;
		ImGui::Text(
			"Next Auto Save: %.1f sec",
			static_cast<float>(framesUntilNext) / 60.0f);
	}
	if (autoSaveEnabled_ &&
		autoSaveAllowed &&
		snapshot.frame >= nextAutoSaveFrame_ &&
		snapshot.frame != lastAutoSaveFrame_) {
		SaveCsv(snapshot, stateName, level);
		lastAutoSaveFrame_ = snapshot.frame;
		nextAutoSaveFrame_ =
			snapshot.frame + static_cast<uint32_t>(autoSaveIntervalSeconds_ * 60);
	}
}
#endif

}
