#pragma once

#include "game/directxgame/ui/common/UILabel.h"
#include <memory>

namespace DirectXGame {

class CurtainTransition {
public:
	enum class State {
		None,
		Close,
		Open,
		Finished,
	};

	void Initialize();
	void StartClose(float speed = 20.0f);
	void StartOpen(float speed = 20.0f);
	void Update(float deltaTime);
	void Draw();

	bool IsFinished() const { return state_ == State::Finished; }
	State GetState() const { return state_; }

private:
	std::unique_ptr<UILabel> topCurtain_;
	std::unique_ptr<UILabel> bottomCurtain_;
	float speed_ = 20.0f;
	State state_ = State::None;
};

}
