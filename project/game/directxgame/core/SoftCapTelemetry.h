#pragma once

#include <cstddef>
#include <cstdint>

namespace DirectXGame {

class EnemyManager;
class PlayerManager;

class SoftCapTelemetry final {
public:
	struct Snapshot {
		uint32_t frame = 0;
		uint32_t elapsedFrames = 0;
		float elapsedMinutes = 0.0f;
		size_t enemyCount = 0;
		int32_t killCount = 0;
		size_t expOrbCount = 0;
		size_t expOrbPeak = 0;
		size_t expOrbPrunes = 0;
		float expOrbPrunesPerMinute = 0.0f;
		size_t normalBulletCount = 0;
		size_t normalBulletPeak = 0;
		size_t normalBulletPrunes = 0;
		float normalBulletPrunesPerMinute = 0.0f;
		size_t orbitBulletCount = 0;
		size_t particleCount = 0;
	};

	Snapshot Capture(
		uint32_t frame,
		const EnemyManager* enemyManager,
		const PlayerManager* playerManager) const;
	void Reset(
		uint32_t frame,
		EnemyManager* enemyManager,
		PlayerManager* playerManager);
	void SaveCsv(const Snapshot& snapshot, const char* stateName, int32_t level) const;

#ifdef _DEBUG
	void DrawControls(
		const Snapshot& snapshot,
		const char* stateName,
		int32_t level,
		bool autoSaveAllowed,
		EnemyManager* enemyManager,
		PlayerManager* playerManager);
#endif

private:
	uint32_t startFrame_ = 0;
	bool autoSaveEnabled_ = false;
	int32_t autoSaveIntervalSeconds_ = 60;
	uint32_t nextAutoSaveFrame_ = 0;
	uint32_t lastAutoSaveFrame_ = UINT32_MAX;
};

}
