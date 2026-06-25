#pragma once

#include <cstdint>

namespace DirectXGame {

struct RunResult {
	RunResult()
		: finalLevel(1), totalKillCount(0)
	{
	}

	int32_t totalExp = 0;
	int32_t coins = 0;
	union {
		int32_t finalLevel;
		uint32_t level;
	};
	union {
		int32_t totalKillCount;
		uint32_t killCount;
	};
	uint32_t elapsedFrames = 0;
};

class GameSession {
public:
	GameSession();
	void BeginNewRun();
	void SetRandomSeed(uint32_t seed);
	bool IsSceneStressEnabled() const { return sceneStressCycleTarget_ > 0; }
	void AdvanceSceneStressFrame() { ++sceneStressFrameCount_; }
	uint32_t GetSceneStressFrameCount() const { return sceneStressFrameCount_; }
	uint32_t GetSceneStressCycleTarget() const { return sceneStressCycleTarget_; }
	bool IsSceneStressComplete() const;
	void RecordSrvUsage(uint32_t usedCount, uint32_t highWatermark);
	void WriteSceneStressReport() const;

	void OnEnterTitleScene();
	void OnEnterGameScene();
	void OnEnterResultScene();

	void AdvanceGameFrame();
	void SetResultSummary(
		uint32_t elapsedFrames,
		uint32_t level,
		uint32_t killCount,
		int32_t totalExp = 0,
		int32_t coins = 0);
	void AddRunCoins(int32_t amount);
	int32_t GetRunCoins() const { return runCoins_; }
	int32_t GetOwnedCoins() const { return ownedCoins_; }

	uint32_t GetRunCount() const { return runCount_; }
	uint32_t GetTitleVisitCount() const { return titleVisitCount_; }
	uint32_t GetGameVisitCount() const { return gameVisitCount_; }
	uint32_t GetResultVisitCount() const { return resultVisitCount_; }
	uint32_t GetGameFrameCount() const { return gameFrameCount_; }
	uint32_t GetRunRandomSeed() const { return runRandomSeed_; }

	RunResult& GetResultData() { return resultData_; }
	const RunResult& GetResultData() const { return resultData_; }

private:
	void LoadProfile();
	void SaveProfile() const;

	uint32_t runCount_ = 0;
	uint32_t titleVisitCount_ = 0;
	uint32_t gameVisitCount_ = 0;
	uint32_t resultVisitCount_ = 0;
	uint32_t gameFrameCount_ = 0;
	uint32_t baseRandomSeed_ = 0;
	uint32_t runRandomSeed_ = 0;
	uint32_t sceneStressCycleTarget_ = 0;
	uint32_t sceneStressFrameCount_ = 0;
	uint32_t stressMaxSrvUsed_ = 0;
	uint32_t stressMaxSrvHighWatermark_ = 0;
	int32_t runCoins_ = 0;
	int32_t ownedCoins_ = 0;
	RunResult resultData_{};
};

}
