#include "SceneManager.h"
#include "AbstractSceneFactory.h"
#include "BaseScene.h"
#include <cassert>

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
	this->nextScene = std::move(nextScene);
}

void SceneManager::Update()
{

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
	//現在のシーンの描画
	if (currentScene) {
		currentScene->Draw();
	}
}

void SceneManager::Finalize()
{
	if (currentScene) {
		currentScene->Finalize();
		currentScene.reset();
	}
	nextScene.reset();
	sceneFactory = nullptr;
}

void SceneManager::ChangeScene(const std::string& sceneName)
{
	assert(sceneFactory);
	assert(nextScene==nullptr);

	nextScene = sceneFactory->CreateScene(sceneName);

}

}


