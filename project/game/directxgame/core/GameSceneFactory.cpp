#include "GameSceneFactory.h"

#include "SceneId.h"
#include "GameSession.h"
#include "ResultScene.h"
#include "PlayScene.h"
#include "GameTitleScene.h"
#include <cassert>

namespace DirectXGame {

GameSceneFactory::GameSceneFactory()
	: sessionContext_(std::make_shared<GameSession>())
{
}

std::unique_ptr<Engine::Scene::BaseScene> GameSceneFactory::CreateScene(const std::string& sceneName)
{
	if (sceneName == SceneId::kTitle) {
		return std::make_unique<TitleScene>(sessionContext_);
	}
	if (sceneName == SceneId::kGame) {
		return std::make_unique<PlayScene>(sessionContext_);
	}
	if (sceneName == SceneId::kResult) {
		return std::make_unique<ResultScene>(sessionContext_);
	}

	assert(false && "Unknown DirectXGame scene name");
	return nullptr;
}

} // namespace DirectXGame
