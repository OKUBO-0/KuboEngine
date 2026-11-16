#include "SceneFactory.h"
#include "GamePlayScene.h"
#include "TitleScene.h"
#include "GameClearScene.h"
#include "GameOverScene.h"
#include <cassert>

namespace Engine::Scene {

std::unique_ptr<BaseScene> SceneFactory::CreateScene(const std::string& sceneName)
{
	std::unique_ptr<BaseScene> newscene;

	if (sceneName == "GAMEPLAY") {
		newscene = std::make_unique<GamePlayScene>();
		static_cast<GamePlayScene*>(newscene.get())->SetScenePath(gameplayScenePath_);
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
	else {
		assert(0);
	}

	return newscene;

}

}
