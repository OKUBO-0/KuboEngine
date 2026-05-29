#include "Game.h"
#include "SceneFactory.h"
#include "SceneManager.h"
#include "ImGuiManager.h"
#include "OffscreenRenderManager.h"
#include "SrvManager.h"
#include "DirectXCommon.h"
#ifdef _DEBUG
#include "Audio.h"
#include "DebugEditorManager.h"
#include "Input.h"
#include <imgui.h>
#endif // _DEBUG
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
	DrawDebugEditorShell();
	imGuiManager->End();
#endif // _DEBUG
}

void Game::DrawDebugEditorShell()
{
#ifdef _DEBUG
	Engine::Editor::DebugEditorMenuItem windowItems[] = {
		{ "Scene", &debugSceneViewOpen_ },
		{ "統計", &debugStatsOpen_ },
		{ "シーン設定", &debugSceneSettingsOpen_ },
		{ "キー操作デバッグ", &debugKeyInputOpen_ },
		{ "オフスクリーン設定", &debugOffscreenOpen_ },
		{ "オーディオ", &debugAudioOpen_ },
		{ "ライト設定", &debugLightOpen_ },
	};
	constexpr size_t windowItemCount = sizeof(windowItems) / sizeof(windowItems[0]);

	Engine::Editor::DebugEditorMenuConfig menuConfig{};
	menuConfig.windowItems = windowItems;
	menuConfig.windowItemCount = windowItemCount;
	menuConfig.windowSwitcher = &debugWindowSwitcherOpen_;
	menuConfig.helpText = "Engine debug editor";
	Engine::Editor::DebugEditorManager::DrawMainMenu(menuConfig);

	Engine::Editor::DebugEditorManager::DrawWindowSwitcher(
		"ウィンドウ表示切り替え",
		&debugWindowSwitcherOpen_,
		windowItems,
		windowItemCount,
		{ 360.0f, 260.0f },
		[]() { Engine::Editor::DebugEditorManager::DrawHotReloadButton(); });

	if (debugSceneViewOpen_) {
		Engine::Editor::DebugEditorManager::DrawSceneViewport(&debugSceneViewOpen_);
	}

	if (debugStatsOpen_) {
		ImGui::Begin("統計", &debugStatsOpen_);
		ImGui::Text("FPS: %.1f", frameDeltaTime_ > 0.0f ? 1.0f / frameDeltaTime_ : 0.0f);
		ImGui::Text("Frame Delta: %.4f sec", frameDeltaTime_);
		ImGui::End();
	}

	if (debugSceneSettingsOpen_) {
		ImGui::Begin("シーン設定", &debugSceneSettingsOpen_);
		ImGui::TextUnformatted("Engine sample scene settings");
		Engine::Editor::DebugEditorManager::DrawHotReloadButton();
		ImGui::End();
	}

	if (debugKeyInputOpen_) {
		ImGui::Begin("キー操作デバッグ", &debugKeyInputOpen_);
		Engine::InputSystem::Input* input = Engine::InputSystem::Input::GetInstance();
		ImGui::Text("W: %s", input->PushKey(DIK_W) ? "down" : "up");
		ImGui::Text("A: %s", input->PushKey(DIK_A) ? "down" : "up");
		ImGui::Text("S: %s", input->PushKey(DIK_S) ? "down" : "up");
		ImGui::Text("D: %s", input->PushKey(DIK_D) ? "down" : "up");
		ImGui::Separator();
		ImGui::Text("Space: %s", input->PushKey(DIK_SPACE) ? "down" : "up");
		ImGui::Text("Left Mouse: %s", input->PushMouse(0) ? "down" : "up");
		ImGui::Text("Right Mouse: %s", input->PushMouse(1) ? "down" : "up");
		ImGui::End();
	}

	if (debugOffscreenOpen_) {
		if (Engine::Base::OffscreenRenderManager* offscreen = Engine::Base::OffscreenRenderManager::GetInstance()) {
			offscreen->DrawImGui(&debugOffscreenOpen_);
		}
	}

	if (debugAudioOpen_) {
		ImGui::Begin("オーディオ", &debugAudioOpen_);
		if (ImGui::SliderFloat("Master Volume", &debugMasterVolume_, 0.0f, 1.0f)) {
			Engine::AudioSystem::Audio::GetInstance()->SetVolume(debugMasterVolume_);
		}
		if (ImGui::Button("Stop All")) {
			Engine::AudioSystem::Audio::GetInstance()->StopAudio();
		}
		ImGui::SameLine();
		if (ImGui::Button("Pause All")) {
			Engine::AudioSystem::Audio::GetInstance()->PauseAudio();
		}
		ImGui::SameLine();
		if (ImGui::Button("Resume All")) {
			Engine::AudioSystem::Audio::GetInstance()->ResumeAudio();
		}
		ImGui::End();
	}

	if (debugLightOpen_) {
		ImGui::Begin("ライト設定", &debugLightOpen_);
		ImGui::TextUnformatted("Scene-specific light controls can be registered here.");
		ImGui::End();
	}
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
