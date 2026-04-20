#include "GameClearScene.h"
#include "Object3DCommon.h"
#include "SpriteCommon.h"
#include <imgui.h>
#include "SceneManager.h"
#include "CameraManager.h"

namespace Engine::Scene {

void GameClearScene::Initialize()
{
	Engine::CameraSystem::CameraManager::GetInstance()->Initialize();
}

void GameClearScene::Finalize()
{
}

void GameClearScene::Update()
{
	Engine::CameraSystem::CameraManager::GetInstance()->GetActiveCamera()->Update();

#ifdef _DEBUG
	if (ImGui::CollapsingHeader("Model", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Text("gameClearScene");
		if (ImGui::Button("TitleScene"))
		{
			SceneManager::GetInstance()->ChangeScene("TITLE");
		}
	}
#endif // _DEBUG
}

void GameClearScene::Draw()
{
#pragma region 3Dオブジェクト描画
	//3dオブジェクトの描画準備。3Dオブジェクトの描画に共通のグラフィックスコマンドを積む
	Engine::Graphics3D::Object3DCommon::GetInstance()->CommonDraw();
#pragma endregion

#pragma region スプライト描画
	//Spriteの描画準備。spriteの描画に共通のグラフィックスコマンドを積む
	Engine::Graphics2D::SpriteCommon::GetInstance()->CommonDraw();
#pragma endregion
}

}
