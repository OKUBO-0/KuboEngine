#include "TitleSceneDebugUIController.h"

#include "DebugEditorManager.h"
#include "DataPaths.h"
#include "Input.h"
#include "OffscreenRenderManager.h"
#include "ResourceProbe.h"
#include "AudioDebugPanel.h"
#include "GameInputBindings.h"
#include "SceneLighting.h"
#include "ScreenUtil.h"
#include "GameTitleScene.h"
#include "UILayoutIO.h"
#include <array>
#include <iterator>
#include <string>
#include <vector>
#ifdef _DEBUG
#include <imgui.h>
#endif

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
	if (scene.titleDebugDrawEnabled_ && scene.showingUpgradeScreen_) {
		ImDrawList* drawList = ImGui::GetForegroundDrawList();
		for (int32_t index = 0; index < 5; ++index) {
			const Vector2 gameMin = layout.shopItemPositions[static_cast<size_t>(index)];
			const Vector2 gameMax{
				gameMin.x + layout.shopItemHitboxSize.x,
				gameMin.y + layout.shopItemHitboxSize.y,
			};
			const Vector2 screenMin = ScreenUtil::ToWindowPosition(gameMin);
			const Vector2 screenMax = ScreenUtil::ToWindowPosition(gameMax);
			const ImU32 color = index == scene.shopItemIndex_
				? IM_COL32(64, 255, 96, 230)
				: IM_COL32(255, 80, 80, 220);
			drawList->AddRect(
				{ screenMin.x, screenMin.y },
				{ screenMax.x, screenMax.y },
				color, 0.0f, 0, 2.0f);
		}
	}

	if (scene.titleDebugDrawEnabled_ && !scene.showingUpgradeScreen_) {
		constexpr const char* kMenuNames[]{ "PLAY", "SHOP", "QUIT" };
		ImDrawList* drawList = ImGui::GetForegroundDrawList();
		for (int32_t index = 0; index < 3; ++index) {
			const Vector2 offset = index == 1
				? layout.cursorShopOffset
				: (index == 2 ? layout.cursorQuitOffset : Vector2{});
			const Vector2 gameMin{
				layout.menuHitboxPosition.x + offset.x,
				layout.menuHitboxPosition.y + offset.y,
			};
			const Vector2 gameMax{
				gameMin.x + layout.menuHitboxSize.x,
				gameMin.y + layout.menuHitboxSize.y,
			};
			const Vector2 screenMin = ScreenUtil::ToWindowPosition(gameMin);
			const Vector2 screenMax = ScreenUtil::ToWindowPosition(gameMax);
			const ImU32 color = index == scene.menuIndex_
				? IM_COL32(64, 255, 96, 230)
				: IM_COL32(255, 200, 48, 210);
			drawList->AddRect(
				{ screenMin.x, screenMin.y },
				{ screenMax.x, screenMax.y },
				color,
				0.0f,
				0,
				2.0f);
			drawList->AddText(
				{ screenMin.x + 4.0f, screenMin.y + 4.0f },
				color,
				kMenuNames[index]);
		}
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
	if (windows.audio) {
		DebugUI::Audio::Draw(
			&windows.audio,
			GetGameAudioTuningEntries(),
			[]() {
				std::vector<UILayoutIO::Entry> entries;
				AppendGameAudioTuningEntries(entries);
				UILayoutIO::Save(DataPaths::kDebugTuning, entries);
			},
			[]() { LoadGameAudioTuning(); });
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

			float cursorShopOffset[2]{ layout.cursorShopOffset.x, layout.cursorShopOffset.y };
			if (ImGui::DragFloat2("Cursor Shop Offset", cursorShopOffset, 1.0f, -1280.0f, 1280.0f)) {
				layout.cursorShopOffset = { cursorShopOffset[0], cursorShopOffset[1] };
				scene.ApplyLayout();
			}
			float cursorQuitOffset[2]{ layout.cursorQuitOffset.x, layout.cursorQuitOffset.y };
			if (ImGui::DragFloat2("Cursor Quit Offset", cursorQuitOffset, 1.0f, -1280.0f, 1280.0f)) {
				layout.cursorQuitOffset = { cursorQuitOffset[0], cursorQuitOffset[1] };
				scene.ApplyLayout();
			}
			ImGui::DragFloat("Cursor Easing Speed", &layout.cursorEasingSpeed, 0.1f, 1.0f, 40.0f);

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

			if (ImGui::CollapsingHeader("Shop Layout")) {
				for (int32_t index = 0; index < 5; ++index) {
					float position[2]{
						layout.shopItemPositions[static_cast<size_t>(index)].x,
						layout.shopItemPositions[static_cast<size_t>(index)].y,
					};
					const std::string label = "Shop Item " + std::to_string(index) + " Position";
					if (ImGui::DragFloat2(label.c_str(), position, 1.0f, -200.0f, 1280.0f)) {
						layout.shopItemPositions[static_cast<size_t>(index)] = { position[0], position[1] };
					}
				}
				float shopHitboxSize[2]{ layout.shopItemHitboxSize.x, layout.shopItemHitboxSize.y };
				if (ImGui::DragFloat2("Shop Hitbox Size", shopHitboxSize, 1.0f, 8.0f, 640.0f)) {
					layout.shopItemHitboxSize = { shopHitboxSize[0], shopHitboxSize[1] };
				}
				float squareSize[2]{ layout.shopLevelSquareSize.x, layout.shopLevelSquareSize.y };
				if (ImGui::DragFloat2("Level Square Size", squareSize, 0.5f, 2.0f, 80.0f)) {
					layout.shopLevelSquareSize = { squareSize[0], squareSize[1] };
				}
				ImGui::DragFloat("Level Square Spacing", &layout.shopLevelSquareStepX, 0.5f, 2.0f, 100.0f);
				ImGui::DragFloat("Level Square Offset Y", &layout.shopLevelSquareOffsetY, 0.5f, -100.0f, 300.0f);
				float priceOffset[2]{ layout.shopPriceOffset.x, layout.shopPriceOffset.y };
				if (ImGui::DragFloat2("Price Offset", priceOffset, 0.5f, -200.0f, 400.0f)) {
					layout.shopPriceOffset = { priceOffset[0], priceOffset[1] };
				}
				float priceSize[2]{ layout.shopPriceDigitSize.x, layout.shopPriceDigitSize.y };
				if (ImGui::DragFloat2("Price Digit Size", priceSize, 0.5f, 4.0f, 80.0f)) {
					layout.shopPriceDigitSize = { priceSize[0], priceSize[1] };
				}
				ImGui::DragFloat("Price Digit Spacing", &layout.shopPriceDigitStepX, 0.5f, 2.0f, 80.0f);
				float coinPosition[2]{ layout.shopCoinPosition.x, layout.shopCoinPosition.y };
				if (ImGui::DragFloat2("Owned Coin Position", coinPosition, 1.0f, -200.0f, 1280.0f)) {
					layout.shopCoinPosition = { coinPosition[0], coinPosition[1] };
				}
				float coinSize[2]{ layout.shopCoinDigitSize.x, layout.shopCoinDigitSize.y };
				if (ImGui::DragFloat2("Owned Coin Digit Size", coinSize, 0.5f, 4.0f, 80.0f)) {
					layout.shopCoinDigitSize = { coinSize[0], coinSize[1] };
				}
				ImGui::DragFloat("Owned Coin Digit Spacing", &layout.shopCoinDigitStepX, 0.5f, 2.0f, 80.0f);
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
