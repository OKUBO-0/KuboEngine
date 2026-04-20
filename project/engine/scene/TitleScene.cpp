#include "TitleScene.h"
#include "Object3DCommon.h"
#include "SpriteCommon.h"
#include "Input.h"
#include "SceneManager.h"
#include <imgui.h>

namespace Engine::Scene {

void TitleScene::Initialize()
{
	
	
	
}

void TitleScene::Finalize()
{
}

void TitleScene::Update()
{
	if (Engine::InputSystem::Input::GetInstance()->TriggerKey(DIK_SPACE)) {
		SceneManager::GetInstance()->ChangeScene("GAMEPLAY");
	}

	if (ImGui::CollapsingHeader("Model", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Text("titleScene %d");
		if (ImGui::Button("gamePlayScene"))
		{
			SceneManager::GetInstance()->ChangeScene("GAMEPLAY");
		}
		


	}

}

void TitleScene::Draw()
{
	//3dオブジェクトの描画準備。3Dオブジェクトの描画に共通のグラフィックスコマンドを積む
	Engine::Graphics3D::Object3DCommon::GetInstance()->CommonDraw();

	//Spriteの描画準備。spriteの描画に共通のグラフィックスコマンドを積む
	Engine::Graphics2D::SpriteCommon::GetInstance()->CommonDraw();

}

}
