#pragma once

#include <cstdint>

namespace DirectXGame {

struct DirectXGameResultData {
	DirectXGameResultData()
		: finalLevel(1), totalKillCount(0)
	{
	}

	int32_t totalExp = 0;
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

class DirectXGameSessionContext {
public:
	void BeginNewRun();

	void OnEnterTitleScene();
	void OnEnterGameScene();
	void OnEnterResultScene();

	void AdvanceGameFrame();
	void SetResultSummary(uint32_t elapsedFrames, uint32_t level, uint32_t killCount, int32_t totalExp = 0);

	uint32_t GetRunCount() const { return runCount_; }
	uint32_t GetTitleVisitCount() const { return titleVisitCount_; }
	uint32_t GetGameVisitCount() const { return gameVisitCount_; }
	uint32_t GetResultVisitCount() const { return resultVisitCount_; }
	uint32_t GetGameFrameCount() const { return gameFrameCount_; }

	DirectXGameResultData& GetResultData() { return resultData_; }
	const DirectXGameResultData& GetResultData() const { return resultData_; }

private:
	uint32_t runCount_ = 0;
	uint32_t titleVisitCount_ = 0;
	uint32_t gameVisitCount_ = 0;
	uint32_t resultVisitCount_ = 0;
	uint32_t gameFrameCount_ = 0;
	DirectXGameResultData resultData_{};
};

using ResultData = DirectXGameResultData;
using GameSessionContext = DirectXGameSessionContext;

}
