#include "SceneManager.h"
#include "AbstractSceneFactory.h"
#include "BaseScene.h"
#include <cassert>
#include <cmath>
#include <stdexcept>

namespace Engine::Scene {

SceneManager::SceneManager() = default;
SceneManager::~SceneManager() = default;

SceneManager* SceneManager::GetInstance()
{
	static SceneManager manager;
	return &manager;
}

void SceneManager::SetNextScene(std::unique_ptr<BaseScene> nextScene)
{
	if (finalizing_) throw std::logic_error("Cannot queue a Scene during shutdown");
	this->nextScene = std::move(nextScene);
}

void SceneManager::SetSceneFactory(AbstractSceneFactory* sceneFactory)
{
	if (finalizing_) throw std::logic_error("Cannot replace the Scene factory during shutdown");
	this->sceneFactory = sceneFactory;
}

void SceneManager::Update()
{
	Update(1.0f / 60.0f);
}

float SceneManager::SimulationDeltaTime(float realDeltaTime) const
{
    return currentScene ? currentScene->SimulationDeltaTime(realDeltaTime) : realDeltaTime;
}

void SceneManager::Update(float deltaTime)
{
	if (finalizing_) return;
	frameDeltaTime_ = std::isfinite(deltaTime) && deltaTime >= 0.0f ? deltaTime : 0.0f;

	//シーンの切り替え
	if (nextScene) {
		//旧シーンの終了処理
		if (currentScene) {
			currentScene->Finalize();
		}
		//新シーンの初期化
		currentScene = std::move(nextScene);

		currentScene->SetSceneManager(this);

		//新シーンの初期化
		currentScene->Initialize();
	}

	//現在のシーンの更新
	if (currentScene) {
		currentScene->Update();
	}

}

void SceneManager::Draw()
{
	if (finalizing_) return;
	//現在のシーンの描画
	if (currentScene) {
		currentScene->Draw();
	}
}

void SceneManager::Finalize()
{
	if (finalizing_) return;
	finalizing_ = true;
	struct FinalizingScope {
		bool& active;
		~FinalizingScope() { active = false; }
	} scope{finalizing_};
	// Unpublish before callbacks. Both owners are destroyed before the guard exits,
	// even if Finalize throws; a later host/static shutdown cannot retain them.
	// Derived Scene destructors must remain non-throwing.
	auto retiring = std::move(currentScene);
	auto pending = std::move(nextScene);
	sceneFactory = nullptr;
	frameDeltaTime_ = 1.0f / 60.0f;
	if (retiring) retiring->Finalize();
}

void SceneManager::ChangeScene(const std::string& sceneName)
{
	if (finalizing_) throw std::logic_error("Cannot change Scene during shutdown");
	assert(sceneFactory);
	assert(nextScene==nullptr);

	nextScene = sceneFactory->CreateScene(sceneName);

}

}


