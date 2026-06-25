#pragma once

#include "AbstractSceneFactory.h"
#include <memory>

namespace DirectXGame {

class GameSession;

class GameSceneFactory : public Engine::Scene::AbstractSceneFactory {
public:
	GameSceneFactory();

	std::unique_ptr<Engine::Scene::BaseScene> CreateScene(const std::string& sceneName) override;

private:
	std::shared_ptr<GameSession> sessionContext_;
};

} // namespace DirectXGame
