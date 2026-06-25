#include "game/directxgame/core/TitleSceneDebugUIController.h"

#include "DebugEditorManager.h"
#include "Input.h"
#include "OffscreenRenderManager.h"
#include "game/directxgame/core/ResourceProbe.h"
#include "game/directxgame/core/AudioDebugPanel.h"
#include "game/directxgame/core/GameInputBindings.h"
#include "game/directxgame/core/SceneLighting.h"
#include "game/directxgame/core/ScreenUtil.h"
#include "game/directxgame/scene/GameTitleScene.h"
#include <array>
#include <iterator>
#include <string>
#ifdef _DEBUG
#include <imgui.h>
#endif

namespace {

constexpr char kAudioTitleBgm[] = "title.bgm";
constexpr char kAudioTitleSelect[] = "title.select";
constexpr char kAudioTitleDecide[] = "title.decide";

}

namespace DirectXGame {

void TitleSceneDebugUIController::Draw(TitleScene& scene)
{
#ifdef _DEBUG
	auto& windows = scene.debugWindows_;
	auto& layout = scene.layoutSettings_;

	Engine::InputSystem::Input* input = Engine::InputSystem::Input::GetInstance();
	if (input && input->TriggerKey(DIK_F5)) {
		scene.ReloadDebugData();
	}

	const TitleScene::DebugWindowVisibility previousWindows = windows;
	const Engine::Editor::DebugEditorMenuItem windowItems[] = {
		{ "Scene", &windows.titleView },
		{ "統計", &windows.statisticsView },
		{ "シーン設定", &windows.titleSettings },
		{ "オフスクリーン設定", &windows.offscreenSettings },
		{ "ライト設定", &windows.lightSettings },
		{ "オーディオ", &windows.audio },
		{ "キー操作デバッグ", &windows.keyInputDebug },
	};
	const Engine::Editor::DebugEditorMenuItem editItems[] = {
		{ "シーン設定", &windows.titleSettings },
		{ "ライト設定", &windows.lightSettings },
	};
	const Engine::Editor::DebugEditorMenuItem objectItems[] = {
		{ "Scene", &windows.titleView },
		{ "シーン設定", &windows.titleSettings },
	};
	Engine::Editor::DebugEditorManager::DrawMainMenu({
		windowItems,
		std::size(windowItems),
		editItems,
		std::size(editItems),
		objectItems,
		std::size(objectItems),
		"タイトルレイアウトを保存",
		[&scene]() { scene.SaveLayout(); },
		"タイトル設定を再読み込み",
		[&scene]() { scene.ReloadDebugData(); },
		"Title Debug UI",
		{},
		[&scene]() { scene.StartGameTransition(); },
		{},
		&windows.windowSwitcher,
	});

	if (windows.windowSwitcher) {
		Engine::Editor::DebugEditorManager::DrawWindowSwitcher(
			"ウィンドウ表示切り替え",
			&windows.windowSwitcher,
			windowItems,
			std::size(windowItems),
			{ 260.0f, 220.0f },
			[]() {
				Engine::Editor::DebugEditorManager::DrawHotReloadButton();
			});
	}
	if (windows.offscreenSettings) {
		if (Engine::Base::OffscreenRenderManager* offscreen =
				Engine::Base::OffscreenRenderManager::GetInstance()) {
			offscreen->DrawImGui();
		}
	}
	if (windows.titleView) {
		const Engine::Editor::DebugSceneViewportState sceneViewport =
			Engine::Editor::DebugEditorManager::DrawSceneViewport(
				&windows.titleView);
		ScreenUtil::SetDebugSceneInputActive(sceneViewport.inputActive);
		if (sceneViewport.drawn) {
			ScreenUtil::SetDebugSceneViewport(
				sceneViewport.min,
				sceneViewport.size);
		} else {
			ScreenUtil::ClearDebugSceneViewport();
		}
	} else {
		ScreenUtil::ClearDebugSceneViewport();
	}

	if (windows.audio) {
		const std::array<AudioTuningEntry, 3> titleAudioEntries{ {
			{ "Title BGM", kAudioTitleBgm, 0.1f, scene.titleBgmHandle_ },
			{ "Title Select", kAudioTitleSelect, 1.0f },
			{ "Title Decide", kAudioTitleDecide, 1.0f },
		} };
		DebugUI::Audio::Draw(&windows.audio, titleAudioEntries);
	}

	if (windows.statisticsView) {
		ImGui::Begin("統計", &windows.statisticsView);
	const ResourceProbeStatus& probeStatus =
		ResourceProbe::Verify();
		ImGui::Text(
			"Texture Probe: %s",
			probeStatus.textureLoaded ? "OK" : "NG");
		ImGui::Text(
			"Model Probe: %s",
			probeStatus.modelLoaded ? "OK" : "NG");
		ImGui::Text(
			"CSV Probe: %s",
			probeStatus.csvOpened ? "OK" : "NG");
		ImGui::Text(
			"Required Assets: %s (%zu checked, %zu missing)",
			probeStatus.requiredAssetsReady ? "OK" : "NG",
			probeStatus.requiredAssetCount,
			probeStatus.missingRequiredAssets.size());
		if (!probeStatus.missingRequiredAssets.empty() &&
			ImGui::TreeNode("Missing DirectXGame Assets")) {
			for (const std::string& path :
				probeStatus.missingRequiredAssets) {
				ImGui::TextUnformatted(path.c_str());
			}
			ImGui::TreePop();
		}
		ImGui::End();
	}

	if (windows.titleSettings) {
		ImGui::Begin("シーン設定", &windows.titleSettings);
		ImGui::Checkbox("Enable Title Debug", &layout.debugEnabled);
		if (layout.debugEnabled) {
			float titlePosition[2]{
				layout.titlePosition.x,
				layout.titlePosition.y,
			};
			if (ImGui::DragFloat2(
					"Title Position",
					titlePosition,
					1.0f,
					-400.0f,
					1280.0f)) {
				layout.titlePosition = {
					titlePosition[0],
					titlePosition[1],
				};
				scene.ApplyLayout();
			}

			float titleSize[2]{ layout.titleSize.x, layout.titleSize.y };
			if (ImGui::DragFloat2(
					"Title Size",
					titleSize,
					1.0f,
					64.0f,
					1280.0f)) {
				layout.titleSize = { titleSize[0], titleSize[1] };
				scene.ApplyLayout();
			}

			float cursorPosition[2]{
				layout.cursorBasePosition.x,
				layout.cursorBasePosition.y,
			};
			if (ImGui::DragFloat2(
					"Cursor Base",
					cursorPosition,
					1.0f,
					-400.0f,
					1280.0f)) {
				layout.cursorBasePosition = {
					cursorPosition[0],
					cursorPosition[1],
				};
				scene.ApplyLayout();
			}

			float cursorSize[2]{
				layout.cursorSize.x,
				layout.cursorSize.y,
			};
			if (ImGui::DragFloat2(
					"Cursor Size",
					cursorSize,
					1.0f,
					64.0f,
					1280.0f)) {
				layout.cursorSize = {
					cursorSize[0],
					cursorSize[1],
				};
				scene.ApplyLayout();
			}

			if (ImGui::DragFloat(
					"Cursor Step",
					&layout.cursorStepY,
					1.0f,
					16.0f,
					320.0f)) {
				scene.ApplyLayout();
			}

			float hitboxPosition[2]{
				layout.menuHitboxPosition.x,
				layout.menuHitboxPosition.y,
			};
			if (ImGui::DragFloat2(
					"Menu Hitbox Pos",
					hitboxPosition,
					1.0f,
					-400.0f,
					1280.0f)) {
				layout.menuHitboxPosition = {
					hitboxPosition[0],
					hitboxPosition[1],
				};
			}

			float hitboxSize[2]{
				layout.menuHitboxSize.x,
				layout.menuHitboxSize.y,
			};
			if (ImGui::DragFloat2(
					"Menu Hitbox Size",
					hitboxSize,
					1.0f,
					16.0f,
					640.0f)) {
				layout.menuHitboxSize = {
					hitboxSize[0],
					hitboxSize[1],
				};
			}
			ImGui::DragFloat(
				"Menu Hitbox Step",
				&layout.menuHitboxStepY,
				1.0f,
				16.0f,
				320.0f);

			float guidePosition[2]{
				layout.guidePosition.x,
				layout.guidePosition.y,
			};
			if (ImGui::DragFloat2(
					"Guide Position",
					guidePosition,
					1.0f,
					-400.0f,
					1280.0f)) {
				layout.guidePosition = {
					guidePosition[0],
					guidePosition[1],
				};
				scene.ApplyLayout();
			}

			float guideSize[2]{
				layout.guideSize.x,
				layout.guideSize.y,
			};
			if (ImGui::DragFloat2(
					"Guide Size",
					guideSize,
					1.0f,
					64.0f,
					1280.0f)) {
				layout.guideSize = {
					guideSize[0],
					guideSize[1],
				};
				scene.ApplyLayout();
			}

			float modelPosition[3]{
				layout.modelBasePosition.x,
				layout.modelBasePosition.y,
				layout.modelBasePosition.z,
			};
			if (ImGui::DragFloat3(
					"Model Position",
					modelPosition,
					0.1f,
					-40.0f,
					40.0f)) {
				layout.modelBasePosition = {
					modelPosition[0],
					modelPosition[1],
					modelPosition[2],
				};
				scene.ApplyLayout();
			}

			float modelScale[3]{
				layout.modelScale.x,
				layout.modelScale.y,
				layout.modelScale.z,
			};
			if (ImGui::DragFloat3(
					"Model Scale",
					modelScale,
					0.1f,
					0.5f,
					10.0f)) {
				layout.modelScale = {
					modelScale[0],
					modelScale[1],
					modelScale[2],
				};
				scene.ApplyLayout();
			}

			if (ImGui::CollapsingHeader(
					"Title Light",
					ImGuiTreeNodeFlags_DefaultOpen)) {
				SceneLighting::DrawDebugUI();
			}

			float cameraTarget[3]{
				layout.cameraTarget.x,
				layout.cameraTarget.y,
				layout.cameraTarget.z,
			};
			if (ImGui::DragFloat3(
					"Camera Target Offset",
					cameraTarget,
					0.1f,
					-60.0f,
					60.0f)) {
				layout.cameraTarget = {
					cameraTarget[0],
					cameraTarget[1],
					cameraTarget[2],
				};
				scene.ApplyLayout();
			}

			if (ImGui::DragFloat(
					"Camera Distance",
					&layout.cameraDistance,
					0.5f,
					8.0f,
					120.0f)) {
				scene.ApplyLayout();
			}
			if (ImGui::DragFloat(
					"Camera Height",
					&layout.cameraHeight,
					0.5f,
					-20.0f,
					80.0f)) {
				scene.ApplyLayout();
			}
			if (ImGui::DragFloat(
					"Camera Pitch",
					&layout.cameraPitch,
					0.01f,
					-1.2f,
					1.2f)) {
				scene.ApplyLayout();
			}
			if (ImGui::DragFloat(
					"Camera Yaw",
					&layout.cameraYaw,
					0.01f,
					-6.28f,
					6.28f)) {
				scene.ApplyLayout();
			}
			ImGui::DragFloat(
				"Camera Orbit Speed",
				&layout.cameraOrbitSpeed,
				0.01f,
				-1.0f,
				1.0f);

			if (ImGui::Button("Save Title Layout")) {
				scene.SaveLayout();
			}
		}
		ImGui::End();
	}

	if (windows.keyInputDebug) {
		ImGui::Begin("キー操作デバッグ", &windows.keyInputDebug);
		ImGui::TextUnformatted("タイトルシーン入力");
		ImGui::Text(
			"Input Device: %s",
			GameInputBindings::ToDisplayName(
				scene.navigationInputDevice_));
		ImGui::Text(
			"Confirm: %s",
			GameInputBindings::GetConfirmLabel(
				scene.navigationInputDevice_));
		ImGui::Text(
			"Cancel: %s",
			GameInputBindings::GetCancelLabel(
				scene.navigationInputDevice_));
		ImGui::Text("Menu Index: %d", scene.menuIndex_);
		ImGui::Text(
			"Guide Active: %s",
			scene.guideActive_ ? "true" : "false");
		ImGui::End();
	}

	Engine::Editor::DebugEditorManager::SaveWindowItems(
		windowItems,
		std::size(windowItems));
	if (previousWindows.windowSwitcher != windows.windowSwitcher ||
		previousWindows.titleView != windows.titleView ||
		previousWindows.statisticsView != windows.statisticsView ||
		previousWindows.titleSettings != windows.titleSettings ||
		previousWindows.offscreenSettings != windows.offscreenSettings ||
		previousWindows.lightSettings != windows.lightSettings ||
		previousWindows.audio != windows.audio ||
		previousWindows.keyInputDebug != windows.keyInputDebug) {
		scene.SaveLayout();
	}
#else
	(void)scene;
#endif
}

}
