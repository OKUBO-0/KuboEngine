#include "DirectXGameSceneFactory.h"

#include "game/directxgame/core/DirectXGameSceneId.h"
#include "game/directxgame/core/DirectXGameSessionContext.h"
#include "game/directxgame/scene/DirectXGameResultScene.h"
#include "game/directxgame/scene/DirectXGameScene.h"
#include "game/directxgame/scene/DirectXGameTitleScene.h"
#include <cassert>

namespace DirectXGame {

DirectXGameSceneFactory::DirectXGameSceneFactory()
	: sessionContext_(std::make_shared<DirectXGameSessionContext>())
{
}

std::unique_ptr<Engine::Scene::BaseScene> DirectXGameSceneFactory::CreateScene(const std::string& sceneName)
{
	if (sceneName == SceneId::kTitle) {
		return std::make_unique<DirectXGameTitleScene>(sessionContext_);
	}
	if (sceneName == SceneId::kGame) {
		return std::make_unique<DirectXGameScene>(sessionContext_);
	}
	if (sceneName == SceneId::kResult) {
		return std::make_unique<DirectXGameResultScene>(sessionContext_);
	}

	assert(false && "Unknown DirectXGame scene name");
	return nullptr;
}

} // namespace DirectXGame
