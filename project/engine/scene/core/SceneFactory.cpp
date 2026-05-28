#include "SceneFactory.h"
#include "game/directxgame/core/DirectXGameSceneId.h"
#include "game/directxgame/core/DirectXGameSessionContext.h"
#include "game/directxgame/scene/DirectXGameResultScene.h"
#include "game/directxgame/scene/DirectXGameScene.h"
#include "game/directxgame/scene/DirectXGameTitleScene.h"
#include "GamePlayScene.h"
#include "TitleScene.h"
#include "GameClearScene.h"
#include "GameOverScene.h"
#include <cassert>

namespace Engine::Scene {

std::unique_ptr<BaseScene> SceneFactory::CreateScene(const std::string& sceneName)
{
	std::unique_ptr<BaseScene> newscene;

	if (!directXGameSessionContext_) {
		directXGameSessionContext_ = std::make_shared<DirectXGame::DirectXGameSessionContext>();
	}

	if (sceneName == "GAMEPLAY") {
		newscene = std::make_unique<GamePlayScene>();
	}
	else if (sceneName == "TITLE") {
		newscene = std::make_unique<TitleScene>();
	}
	else if (sceneName == "GAMECLEAR") {
		newscene = std::make_unique<GameClearScene>();
	}
	else if (sceneName == "GAMEOVER") {
		newscene = std::make_unique<GameOverScene>();
	}
	else if (sceneName == DirectXGame::SceneId::kTitle) {
		newscene = std::make_unique<DirectXGame::DirectXGameTitleScene>(directXGameSessionContext_);
	}
	else if (sceneName == DirectXGame::SceneId::kGame) {
		newscene = std::make_unique<DirectXGame::DirectXGameScene>(directXGameSessionContext_);
	}
	else if (sceneName == DirectXGame::SceneId::kResult) {
		newscene = std::make_unique<DirectXGame::DirectXGameResultScene>(directXGameSessionContext_);
	}
	else {
		assert(0);
	}

	return newscene;

}

}
