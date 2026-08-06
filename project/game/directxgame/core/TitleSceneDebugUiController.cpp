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
		{ "タイトルUI調整", &windows.titleSettings },
		{ "タイトル3D・カメラ", &windows.titleModelSettings },
		{ "オフスクリーン設定", &windows.offscreenSettings },
		{ "ライト設定", &windows.lightSettings },
		{ "オーディオ", &windows.audio },
		{ "キー操作デバッグ", &windows.keyInputDebug },
	};
	const Engine::Editor::DebugEditorMenuItem editItems[] = {
		{ "タイトルUI調整", &windows.titleSettings },
		{ "タイトル3D・カメラ", &windows.titleModelSettings },
		{ "ライト設定", &windows.lightSettings },
	};
	const Engine::Editor::DebugEditorMenuItem objectItems[] = {
		{ "Scene", &windows.titleView },
		{ "タイトル3D・カメラ", &windows.titleModelSettings },
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
		ImGui::Begin("タイトルUI調整", &windows.titleSettings);
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

			if (ImGui::CollapsingHeader("Character Select Layout", ImGuiTreeNodeFlags_DefaultOpen)) {
				float backgroundPosition[2]{
					layout.selectBackgroundPosition.x,
					layout.selectBackgroundPosition.y,
				};
				if (ImGui::DragFloat2("Select BG Position", backgroundPosition, 1.0f, -400.0f, 1280.0f)) {
					layout.selectBackgroundPosition = { backgroundPosition[0], backgroundPosition[1] };
					scene.ApplyStartCharacterSelectionLayout();
				}
				float backgroundSize[2]{
					layout.selectBackgroundSize.x,
					layout.selectBackgroundSize.y,
				};
				if (ImGui::DragFloat2("Select BG Size", backgroundSize, 1.0f, 64.0f, 1600.0f)) {
					layout.selectBackgroundSize = { backgroundSize[0], backgroundSize[1] };
					scene.ApplyStartCharacterSelectionLayout();
				}
				float titlePosition[2]{
					layout.selectTitleTextPosition.x,
					layout.selectTitleTextPosition.y,
				};
				if (ImGui::DragFloat2("Select Title Position", titlePosition, 1.0f, -200.0f, 1280.0f)) {
					layout.selectTitleTextPosition = { titlePosition[0], titlePosition[1] };
					scene.ApplyStartCharacterSelectionLayout();
				}
				if (ImGui::DragFloat("Select Title Scale", &layout.selectTitleTextScale, 0.01f, 0.05f, 1.5f)) {
					scene.ApplyStartCharacterSelectionLayout();
				}
				float iconBase[2]{
					layout.selectIconBasePosition.x,
					layout.selectIconBasePosition.y,
				};
				if (ImGui::DragFloat2("Select Icon Base", iconBase, 1.0f, -200.0f, 1280.0f)) {
					layout.selectIconBasePosition = { iconBase[0], iconBase[1] };
					scene.ApplyStartCharacterSelectionLayout();
				}
				float iconSize[2]{
					layout.selectIconSize.x,
					layout.selectIconSize.y,
				};
				if (ImGui::DragFloat2("Select Icon Size", iconSize, 1.0f, 8.0f, 220.0f)) {
					layout.selectIconSize = { iconSize[0], iconSize[1] };
					scene.ApplyStartCharacterSelectionLayout();
				}
				float iconHitbox[2]{
					layout.selectIconHitboxSize.x,
					layout.selectIconHitboxSize.y,
				};
				if (ImGui::DragFloat2("Select Icon Hitbox", iconHitbox, 1.0f, 8.0f, 260.0f)) {
					layout.selectIconHitboxSize = { iconHitbox[0], iconHitbox[1] };
					scene.ApplyStartCharacterSelectionLayout();
				}
				if (ImGui::DragFloat("Select Icon Step X", &layout.selectIconStepX, 1.0f, 16.0f, 240.0f)) {
					scene.ApplyStartCharacterSelectionLayout();
				}
				if (ImGui::DragFloat("Select Icon Step Y", &layout.selectIconStepY, 1.0f, 16.0f, 240.0f)) {
					scene.ApplyStartCharacterSelectionLayout();
				}
				float iconModelBase[3]{
					layout.selectIconModelBasePosition.x,
					layout.selectIconModelBasePosition.y,
					layout.selectIconModelBasePosition.z,
				};
				if (ImGui::DragFloat3("Icon Model Base Position", iconModelBase, 0.1f, -60.0f, 60.0f)) {
					layout.selectIconModelBasePosition = { iconModelBase[0], iconModelBase[1], iconModelBase[2] };
				}
				float iconModelStep[3]{
					layout.selectIconModelStep.x,
					layout.selectIconModelStep.y,
					layout.selectIconModelStep.z,
				};
				if (ImGui::DragFloat3("Icon Model Step", iconModelStep, 0.1f, -20.0f, 20.0f)) {
					layout.selectIconModelStep = { iconModelStep[0], iconModelStep[1], iconModelStep[2] };
				}
				float iconModelScale[3]{
					layout.selectIconModelScale.x,
					layout.selectIconModelScale.y,
					layout.selectIconModelScale.z,
				};
				if (ImGui::DragFloat3("Icon Model Scale", iconModelScale, 0.01f, 0.0001f, 8.0f)) {
					layout.selectIconModelScale = { iconModelScale[0], iconModelScale[1], iconModelScale[2] };
				}
				float iconModelRotation[3]{
					layout.selectIconModelRotation.x,
					layout.selectIconModelRotation.y,
					layout.selectIconModelRotation.z,
				};
				if (ImGui::DragFloat3("Icon Model Rotation", iconModelRotation, 0.01f, -6.28f, 6.28f)) {
					layout.selectIconModelRotation = { iconModelRotation[0], iconModelRotation[1], iconModelRotation[2] };
				}
				float iconWeaponOffset[3]{
					layout.selectIconWeaponOffset.x,
					layout.selectIconWeaponOffset.y,
					layout.selectIconWeaponOffset.z,
				};
				if (ImGui::DragFloat3("Icon Weapon Offset", iconWeaponOffset, 0.05f, -10.0f, 10.0f)) {
					layout.selectIconWeaponOffset = { iconWeaponOffset[0], iconWeaponOffset[1], iconWeaponOffset[2] };
				}
				float iconWeaponScale[3]{
					layout.selectIconWeaponScale.x,
					layout.selectIconWeaponScale.y,
					layout.selectIconWeaponScale.z,
				};
				if (ImGui::DragFloat3("Icon Weapon Scale", iconWeaponScale, 0.01f, 0.0001f, 8.0f)) {
					layout.selectIconWeaponScale = { iconWeaponScale[0], iconWeaponScale[1], iconWeaponScale[2] };
				}
				float detailFacePosition[2]{
					layout.selectDetailFacePosition.x,
					layout.selectDetailFacePosition.y,
				};
				if (ImGui::DragFloat2("Detail Face Position", detailFacePosition, 1.0f, -200.0f, 1280.0f)) {
					layout.selectDetailFacePosition = { detailFacePosition[0], detailFacePosition[1] };
					scene.ApplyStartCharacterSelectionLayout();
				}
				float detailFaceSize[2]{
					layout.selectDetailFaceSize.x,
					layout.selectDetailFaceSize.y,
				};
				if (ImGui::DragFloat2("Detail Face Size", detailFaceSize, 1.0f, 8.0f, 220.0f)) {
					layout.selectDetailFaceSize = { detailFaceSize[0], detailFaceSize[1] };
					scene.ApplyStartCharacterSelectionLayout();
				}
				float weaponIconPosition[2]{
					layout.selectWeaponIconPosition.x,
					layout.selectWeaponIconPosition.y,
				};
				if (ImGui::DragFloat2("Detail Weapon Position", weaponIconPosition, 1.0f, -200.0f, 1280.0f)) {
					layout.selectWeaponIconPosition = { weaponIconPosition[0], weaponIconPosition[1] };
					scene.ApplyStartCharacterSelectionLayout();
				}
				float weaponIconSize[2]{
					layout.selectWeaponIconSize.x,
					layout.selectWeaponIconSize.y,
				};
				if (ImGui::DragFloat2("Detail Weapon Size", weaponIconSize, 1.0f, 8.0f, 220.0f)) {
					layout.selectWeaponIconSize = { weaponIconSize[0], weaponIconSize[1] };
					scene.ApplyStartCharacterSelectionLayout();
				}
				float weaponDescriptionPosition[2]{
					layout.selectWeaponDescriptionPosition.x,
					layout.selectWeaponDescriptionPosition.y,
				};
				if (ImGui::DragFloat2("Weapon Description Position", weaponDescriptionPosition, 1.0f, -200.0f, 1280.0f)) {
					layout.selectWeaponDescriptionPosition = { weaponDescriptionPosition[0], weaponDescriptionPosition[1] };
					scene.ApplyStartCharacterSelectionLayout();
				}
				if (ImGui::DragFloat("Weapon Description Scale", &layout.selectWeaponDescriptionScale, 0.01f, 0.05f, 1.0f)) {
					scene.ApplyStartCharacterSelectionLayout();
				}
				if (ImGui::DragFloat("Weapon Description Max Width", &layout.selectWeaponDescriptionMaxWidth, 1.0f, 32.0f, 360.0f)) {
					scene.ApplyStartCharacterSelectionLayout();
				}
				float buttonPosition[2]{
					layout.selectActionButtonPosition.x,
					layout.selectActionButtonPosition.y,
				};
				if (ImGui::DragFloat2("Select Button Position", buttonPosition, 1.0f, -200.0f, 1280.0f)) {
					layout.selectActionButtonPosition = { buttonPosition[0], buttonPosition[1] };
					scene.ApplyStartCharacterSelectionLayout();
				}
				float buttonSize[2]{
					layout.selectActionButtonSize.x,
					layout.selectActionButtonSize.y,
				};
				if (ImGui::DragFloat2("Select Button Size", buttonSize, 1.0f, 8.0f, 320.0f)) {
					layout.selectActionButtonSize = { buttonSize[0], buttonSize[1] };
					scene.ApplyStartCharacterSelectionLayout();
				}
				float buttonTextOffset[2]{
					layout.selectActionTextOffset.x,
					layout.selectActionTextOffset.y,
				};
				if (ImGui::DragFloat2("Select Button Text Offset", buttonTextOffset, 0.5f, -80.0f, 160.0f)) {
					layout.selectActionTextOffset = { buttonTextOffset[0], buttonTextOffset[1] };
					scene.ApplyStartCharacterSelectionLayout();
				}
				if (ImGui::DragFloat("Select Button Text Scale", &layout.selectActionTextScale, 0.01f, 0.05f, 1.0f)) {
					scene.ApplyStartCharacterSelectionLayout();
				}
				float selectModelPosition[3]{
					layout.selectModelPosition.x,
					layout.selectModelPosition.y,
					layout.selectModelPosition.z,
				};
				if (ImGui::DragFloat3("Select Model Position", selectModelPosition, 0.1f, -60.0f, 60.0f)) {
					layout.selectModelPosition = { selectModelPosition[0], selectModelPosition[1], selectModelPosition[2] };
				}
				float selectModelScale[3]{
					layout.selectModelScale.x,
					layout.selectModelScale.y,
					layout.selectModelScale.z,
				};
				if (ImGui::DragFloat3("Select Model Scale", selectModelScale, 0.01f, 0.0001f, 20.0f)) {
					layout.selectModelScale = { selectModelScale[0], selectModelScale[1], selectModelScale[2] };
				}
				float selectModelRotation[3]{
					layout.selectModelRotation.x,
					layout.selectModelRotation.y,
					layout.selectModelRotation.z,
				};
				if (ImGui::DragFloat3("Select Model Rotation", selectModelRotation, 0.01f, -6.28f, 6.28f)) {
					layout.selectModelRotation = { selectModelRotation[0], selectModelRotation[1], selectModelRotation[2] };
				}
				float selectModelWeaponOffset[3]{
					layout.selectModelWeaponOffset.x,
					layout.selectModelWeaponOffset.y,
					layout.selectModelWeaponOffset.z,
				};
				if (ImGui::DragFloat3("Select Model Weapon Offset", selectModelWeaponOffset, 0.05f, -12.0f, 12.0f)) {
					layout.selectModelWeaponOffset = { selectModelWeaponOffset[0], selectModelWeaponOffset[1], selectModelWeaponOffset[2] };
				}
				float selectModelWeaponScale[3]{
					layout.selectModelWeaponScale.x,
					layout.selectModelWeaponScale.y,
					layout.selectModelWeaponScale.z,
				};
				if (ImGui::DragFloat3("Select Model Weapon Scale", selectModelWeaponScale, 0.01f, 0.0001f, 10.0f)) {
					layout.selectModelWeaponScale = { selectModelWeaponScale[0], selectModelWeaponScale[1], selectModelWeaponScale[2] };
				}
				float detailModelPosition[3]{
					layout.selectDetailModelPosition.x,
					layout.selectDetailModelPosition.y,
					layout.selectDetailModelPosition.z,
				};
				if (ImGui::DragFloat3("Detail Model Position", detailModelPosition, 0.1f, -60.0f, 60.0f)) {
					layout.selectDetailModelPosition = { detailModelPosition[0], detailModelPosition[1], detailModelPosition[2] };
				}
				float detailModelScale[3]{
					layout.selectDetailModelScale.x,
					layout.selectDetailModelScale.y,
					layout.selectDetailModelScale.z,
				};
				if (ImGui::DragFloat3("Detail Model Scale", detailModelScale, 0.01f, 0.0001f, 8.0f)) {
					layout.selectDetailModelScale = { detailModelScale[0], detailModelScale[1], detailModelScale[2] };
				}
				float detailWeaponOffset[3]{
					layout.selectDetailWeaponOffset.x,
					layout.selectDetailWeaponOffset.y,
					layout.selectDetailWeaponOffset.z,
				};
				if (ImGui::DragFloat3("Detail Weapon Offset", detailWeaponOffset, 0.05f, -10.0f, 10.0f)) {
					layout.selectDetailWeaponOffset = { detailWeaponOffset[0], detailWeaponOffset[1], detailWeaponOffset[2] };
				}
				float detailWeaponScale[3]{
					layout.selectDetailWeaponScale.x,
					layout.selectDetailWeaponScale.y,
					layout.selectDetailWeaponScale.z,
				};
				if (ImGui::DragFloat3("Detail Weapon Scale", detailWeaponScale, 0.01f, 0.0001f, 8.0f)) {
					layout.selectDetailWeaponScale = { detailWeaponScale[0], detailWeaponScale[1], detailWeaponScale[2] };
				}
			}

			if (ImGui::Button("Save Title Layout")) {
				scene.SaveLayout();
			}
		}
		ImGui::End();
	}

	if (windows.titleModelSettings) {
		ImGui::Begin("タイトル3D・カメラ", &windows.titleModelSettings);
		float modelPosition[3]{
			layout.modelBasePosition.x,
			layout.modelBasePosition.y,
			layout.modelBasePosition.z,
		};
		if (ImGui::DragFloat3(
				"Title Model Position",
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
				"Title Model Scale",
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

		if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
			float cameraTarget[3]{
				layout.cameraTarget.x,
				layout.cameraTarget.y,
				layout.cameraTarget.z,
			};
			if (ImGui::DragFloat3(
					"Target Offset",
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
					"Distance",
					&layout.cameraDistance,
					0.5f,
					8.0f,
					120.0f)) {
				scene.ApplyLayout();
			}
			if (ImGui::DragFloat(
					"Height",
					&layout.cameraHeight,
					0.5f,
					-20.0f,
					80.0f)) {
				scene.ApplyLayout();
			}
			if (ImGui::DragFloat(
					"Pitch",
					&layout.cameraPitch,
					0.01f,
					-1.2f,
					1.2f)) {
				scene.ApplyLayout();
			}
			if (ImGui::DragFloat(
					"Yaw",
					&layout.cameraYaw,
					0.01f,
					-6.28f,
					6.28f)) {
				scene.ApplyLayout();
			}
			ImGui::DragFloat(
				"Orbit Speed",
				&layout.cameraOrbitSpeed,
				0.01f,
				-1.0f,
				1.0f);
		}

		if (ImGui::Button("Save Title Layout")) {
			scene.SaveLayout();
		}
		ImGui::End();
	}

	if (windows.lightSettings) {
		ImGui::SetNextWindowPos(ImVec2(730.0f, 12.0f), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(380.0f, 360.0f), ImGuiCond_FirstUseEver);
		ImGui::Begin("ライト設定", &windows.lightSettings);
		SceneLighting::DrawDebugUI();
		if (ImGui::Button("Save Title Lighting")) {
			std::vector<UILayoutIO::Entry> entries;
			SceneLighting::AppendTuningEntries(entries, "title.");
			UILayoutIO::Save(DataPaths::kDebugTuning, entries);
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
		previousWindows.titleModelSettings != windows.titleModelSettings ||
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
