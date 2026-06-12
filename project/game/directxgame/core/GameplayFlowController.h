#pragma once

namespace DirectXGame {

enum class GameplayState {
	Start,
	Playing,
	BossIntro,
	Boss,
	BossDefeated,
	Paused,
	LevelUp,
	Dead,
};

class GameplayFlowController final {
public:
	static constexpr float kIntroDuration = 1.2f;

	void Reset();
	void UpdateIntro(float deltaTime);
	bool EnterPlaying();
	bool BeginPause();
	bool BeginBossIntro();
	bool EnterBoss();
	bool BeginBossDefeated();
	void BeginLevelUp();
	void BeginDead();

	GameplayState GetState() const { return state_; }
	bool Is(GameplayState state) const { return state_ == state; }
	bool IsCombatActive() const;
	bool IsWorldHudVisible() const;
	bool IsCursorHidden() const;
	bool IsIntroFinished() const { return introElapsed_ >= kIntroDuration; }
	float GetIntroElapsed() const { return introElapsed_; }
	const char* GetStateName() const;

private:
	GameplayState state_ = GameplayState::Start;
	GameplayState resumeState_ = GameplayState::Playing;
	float introElapsed_ = 0.0f;
};

}
