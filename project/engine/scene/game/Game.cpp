#include "Game.h"
#include "SceneFactory.h"
#include "game/directxgame/core/DirectXGameSceneId.h"
#include "SceneManager.h"
#include "ImGuiManager.h"
#include "OffscreenRenderManager.h"
#include "SrvManager.h"
#include "DirectXCommon.h"

namespace Engine::Scene {

namespace {

constexpr bool kBootDirectXGameMigration = true;

}

void Game::Initialize()
{
	// 初期化
	Engine::Base::Framework::Initialize();
	sceneFactory = std::make_unique<SceneFactory>();
	SceneManager::GetInstance()->SetSceneFactory(sceneFactory.get());

	// シーンの変更
	// "TITLE"
	// "GAMEPLAY"
	// "GAMEOVER"
	// "GAMECLEAR"
	const char* initialSceneName = kBootDirectXGameMigration ? DirectXGame::SceneId::kTitle : "GAMEPLAY";
	SceneManager::GetInstance()->ChangeScene(initialSceneName);
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
