#pragma once

#include "game/directxgame/effects/CurtainTransition.h"
#include <string>
#include <string_view>

namespace DirectXGame {

class SceneTransitionPresentation final {
public:
	void Initialize(float openSpeed = 20.0f);
	bool Request(std::string_view sceneId, float closeSpeed = 24.0f);
	bool Update(float deltaTime);
	void Draw();

	bool HasPendingScene() const { return !pendingSceneId_.empty(); }
	bool IsInitialized() const { return initialized_; }
	const std::string& GetPendingSceneId() const { return pendingSceneId_; }

private:
	CurtainTransition curtain_;
	std::string pendingSceneId_;
	bool initialized_ = false;
	bool transitionReady_ = false;
};

}
