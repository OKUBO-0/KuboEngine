#include "SceneTransitionPresentation.h"

namespace DirectXGame {

void SceneTransitionPresentation::Initialize(float openSpeed)
{
	curtain_.Initialize();
	curtain_.StartOpen(openSpeed);
	pendingSceneId_.clear();
	initialized_ = true;
	transitionReady_ = false;
}

bool SceneTransitionPresentation::Request(
	std::string_view sceneId,
	float closeSpeed)
{
	if (!initialized_ || sceneId.empty() || HasPendingScene()) {
		return false;
	}
	pendingSceneId_ = sceneId;
	transitionReady_ = false;
	curtain_.StartClose(closeSpeed);
	return true;
}

bool SceneTransitionPresentation::Update(float deltaTime)
{
	if (!initialized_) {
		return false;
	}
	curtain_.Update(deltaTime);
	if (!transitionReady_ &&
		HasPendingScene() &&
		curtain_.IsFinished()) {
		transitionReady_ = true;
		return true;
	}
	return false;
}

void SceneTransitionPresentation::Draw()
{
	if (initialized_) {
		curtain_.Draw();
	}
}

}
