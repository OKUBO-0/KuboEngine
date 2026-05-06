#include "game/directxgame/core/DirectXGameSessionContext.h"

namespace DirectXGame {

void DirectXGameSessionContext::BeginNewRun()
{
	++runCount_;
	gameFrameCount_ = 0;
	resultData_ = {};
}

void DirectXGameSessionContext::OnEnterTitleScene()
{
	++titleVisitCount_;
}

void DirectXGameSessionContext::OnEnterGameScene()
{
	++gameVisitCount_;
}

void DirectXGameSessionContext::OnEnterResultScene()
{
	++resultVisitCount_;
}

void DirectXGameSessionContext::AdvanceGameFrame()
{
	++gameFrameCount_;
}

void DirectXGameSessionContext::SetResultSummary(
	uint32_t elapsedFrames, uint32_t level, uint32_t killCount, int32_t totalExp)
{
	resultData_.elapsedFrames = elapsedFrames;
	resultData_.level = level;
	resultData_.killCount = killCount;
	resultData_.totalExp = totalExp;
}

}
