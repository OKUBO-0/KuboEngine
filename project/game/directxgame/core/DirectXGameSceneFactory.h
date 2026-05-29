#pragma once

#include "AbstractSceneFactory.h"
#include <memory>

namespace DirectXGame {

class DirectXGameSessionContext;

class DirectXGameSceneFactory : public Engine::Scene::AbstractSceneFactory {
public:
	DirectXGameSceneFactory();

	std::unique_ptr<Engine::Scene::BaseScene> CreateScene(const std::string& sceneName) override;

private:
	std::shared_ptr<DirectXGameSessionContext> sessionContext_;
};

} // namespace DirectXGame
