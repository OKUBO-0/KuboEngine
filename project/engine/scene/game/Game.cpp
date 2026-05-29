#include "Game.h"
#include "SceneFactory.h"
#include "SceneManager.h"
#include "ImGuiManager.h"
#include "OffscreenRenderManager.h"
#include "SrvManager.h"
#include "DirectXCommon.h"
#include <utility>

namespace Engine::Scene {

Game::Game()
	: initialSceneName_("GAMEPLAY")
{
}

Game::Game(std::unique_ptr<AbstractSceneFactory> sceneFactory, std::string initialSceneName)
	: initialSceneName_(std::move(initialSceneName))
{
	SetSceneFactory(std::move(sceneFactory));
}

void Game::SetSceneFactory(std::unique_ptr<AbstractSceneFactory> sceneFactory)
{
	this->sceneFactory = std::move(sceneFactory);
}

void Game::SetInitialSceneName(std::string sceneName)
{
	initialSceneName_ = std::move(sceneName);
}

void Game::Initialize()
{
	// 初期化
	Engine::Base::Framework::Initialize();
	if (!sceneFactory) {
		sceneFactory = std::make_unique<SceneFactory>();
	}
	SceneManager::GetInstance()->SetSceneFactory(sceneFactory.get());

	// シーンの変更
	// "TITLE"
	// "GAMEPLAY"
	// "GAMEOVER"
	// "GAMECLEAR"
	SceneManager::GetInstance()->ChangeScene(initialSceneName_);
}

void Game::Finalize()
{
	// 終了
	Engine::Base::Framework::Finalize();
}

void Game::Update()
{
#ifdef _DEBUG
	imGuiManager->Begin();
#endif // _DEBUG
	// 更新
	Engine::Base::Framework::Update();

#ifdef _DEBUG
	imGuiManager->End();
#endif // _DEBUG
}

void Game::Draw()
{
	// DirectXの描画準備。すべての描画に共通のグラフィックスコマンドを積む
	offscreenRenderManager->Begin();
	srvManager->PreDraw();
	SceneManager::GetInstance()->Draw();
	offscreenRenderManager->End();

	dxCommon->Begin();
	// 描画
	offscreenRenderManager->Draw();
#ifdef _DEBUG
	imGuiManager->Draw();
#endif // _DEBUG
	dxCommon->End();
}

}
