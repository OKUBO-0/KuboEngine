#include "GameplayFlowController.h"
#include <algorithm>

namespace DirectXGame {

void GameplayFlowController::Reset()
{
	state_ = GameplayState::Start;
	resumeState_ = GameplayState::Playing;
	introElapsed_ = 0.0f;
	bossIntroElapsed_ = 0.0f;
}

void GameplayFlowController::UpdateIntro(float deltaTime)
{
	if (state_ != GameplayState::Start) {
		return;
	}
	introElapsed_ = (std::min)(
		introElapsed_ + deltaTime,
		kIntroDuration);
}

void GameplayFlowController::UpdateBossIntro(float deltaTime)
{
	if (state_ != GameplayState::BossIntro) {
		return;
	}
	bossIntroElapsed_ = (std::min)(
		bossIntroElapsed_ + deltaTime,
		kBossIntroDuration);
}

bool GameplayFlowController::EnterPlaying()
{
	const bool enteringFromStart = state_ == GameplayState::Start;
	if (enteringFromStart) {
		introElapsed_ = kIntroDuration;
		state_ = GameplayState::Playing;
		return true;
	}
	if (state_ == GameplayState::Paused) {
		state_ = resumeState_;
		resumeState_ = GameplayState::Playing;
		return false;
	}
	state_ = GameplayState::Playing;
	return false;
}

bool GameplayFlowController::BeginPause()
{
	if (!IsCombatActive()) {
		return false;
	}
	resumeState_ = state_;
	state_ = GameplayState::Paused;
	return true;
}

bool GameplayFlowController::BeginBossIntro()
{
	if (state_ != GameplayState::Playing) {
		return false;
	}
	bossIntroElapsed_ = 0.0f;
	state_ = GameplayState::BossIntro;
	return true;
}

bool GameplayFlowController::EnterBoss()
{
	if (state_ != GameplayState::BossIntro) {
		return false;
	}
	bossIntroElapsed_ = kBossIntroDuration;
	state_ = GameplayState::Boss;
	return true;
}

bool GameplayFlowController::BeginBossDefeated()
{
	if (state_ != GameplayState::Boss) {
		return false;
	}
	state_ = GameplayState::BossDefeated;
	return true;
}

void GameplayFlowController::BeginLevelUp()
{
	state_ = GameplayState::LevelUp;
}

void GameplayFlowController::BeginDead()
{
	state_ = GameplayState::Dead;
}

bool GameplayFlowController::IsCombatActive() const
{
	return state_ == GameplayState::Playing ||
		state_ == GameplayState::Boss;
}

bool GameplayFlowController::IsWorldHudVisible() const
{
	return state_ == GameplayState::Playing ||
		state_ == GameplayState::Boss ||
		state_ == GameplayState::BossDefeated;
}

bool GameplayFlowController::IsCursorHidden() const
{
	return state_ == GameplayState::Playing ||
		state_ == GameplayState::BossIntro ||
		state_ == GameplayState::Boss ||
		state_ == GameplayState::BossDefeated;
}

const char* GameplayFlowController::GetStateName() const
{
	switch (state_) {
	case GameplayState::Start:
		return "Start";
	case GameplayState::Playing:
		return "Playing";
	case GameplayState::BossIntro:
		return "BossIntro";
	case GameplayState::Boss:
		return "Boss";
	case GameplayState::BossDefeated:
		return "BossDefeated";
	case GameplayState::Paused:
		return "Paused";
	case GameplayState::LevelUp:
		return "LevelUp";
	case GameplayState::Dead:
		return "Dead";
	default:
		return "Unknown";
	}
}

}
