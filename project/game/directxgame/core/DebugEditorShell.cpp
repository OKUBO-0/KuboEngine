#include "game/directxgame/core/DebugEditorShell.h"
#include "DebugEditorManager.h"
#include "IconsFontAwesome5.h"
#include "game/directxgame/core/ScreenUtil.h"
#include <iterator>
#ifdef _DEBUG
#include <imgui.h>
#endif

namespace DirectXGame {

bool DebugUI::EditorShell::Draw(
	DebugWindowVisibility& windows,
	const std::function<void()>& save,
	const std::function<void()>& reload,
	const std::function<void()>& goToTitle,
	const std::function<void()>& goToResult,
	Engine::Editor::DebugPlaybackState playbackState,
	const std::function<void()>& togglePlayback)
{
#ifdef _DEBUG
	const DebugWindowVisibility previous = windows;
	const Engine::Editor::DebugEditorMenuItem windowItems[] = {
		{ "Scene", &windows.sceneView, ICON_FA_GAMEPAD },
		{ "オブジェクトビュー", &windows.objectView, ICON_FA_CUBE },
		{ "パーティクルビュー", &windows.particleView, ICON_FA_MAGIC },
		{ "統計", &windows.statisticsView, ICON_FA_CHART_BAR },
		{ "オフスクリーン設定", &windows.offscreenSettings, ICON_FA_WINDOW_RESTORE },
		{ "ライト設定", &windows.lightSettings, ICON_FA_LIGHTBULB },
		{ "ギズモ", &windows.gizmo, ICON_FA_ARROWS_ALT },
		{ "オブジェクトマネージャ", &windows.objectManager, ICON_FA_LIST },
		{ "モーションエディター", &windows.motionEditor, ICON_FA_FILM },
		{ "スプライトマネージャ", &windows.spriteManager, ICON_FA_IMAGE },
		{ "コライダー/タグ管理", &windows.colliderTagManager, ICON_FA_TAGS },
		{ "オーディオ", &windows.audio, ICON_FA_VOLUME_UP },
		{ "キー操作デバッグ", &windows.keyInputDebug, ICON_FA_KEYBOARD },
		{ "シーン固有デバッグ", &windows.sceneSpecificDebug, ICON_FA_BUG },
		{ "オブジェクト設定", &windows.objectSettings, ICON_FA_SLIDERS_H },
	};
	const Engine::Editor::DebugEditorMenuItem editItems[] = {
		{ "ギズモ", &windows.gizmo, ICON_FA_ARROWS_ALT },
		{ "オブジェクト設定", &windows.objectSettings, ICON_FA_SLIDERS_H },
		{ "モーションエディター", &windows.motionEditor, ICON_FA_FILM },
	};
	const Engine::Editor::DebugEditorMenuItem objectItems[] = {
		{ "オブジェクトマネージャ", &windows.objectManager, ICON_FA_LIST },
		{ "オブジェクト設定", &windows.objectSettings, ICON_FA_SLIDERS_H },
		{ "コライダー/タグ管理", &windows.colliderTagManager, ICON_FA_TAGS },
	};
	Engine::Editor::DebugEditorManager::DrawMainMenu({
		windowItems,
		std::size(windowItems),
		editItems,
		std::size(editItems),
		objectItems,
		std::size(objectItems),
		"デバッグ設定を保存",
		save,
		"デバッグ設定を復元",
		reload,
		"Debug: F5 Reload / F6 Death / F8 EXP / F9 Max Weapons / F10 Result / F11 Boss",
		goToTitle,
		{},
		goToResult,
		&windows.windowSwitcher,
		playbackState,
		togglePlayback,
	});
	if (windows.windowSwitcher) {
		ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_FirstUseEver);
		Engine::Editor::DebugEditorManager::DrawWindowSwitcher(
			"ウィンドウ表示切り替え",
			&windows.windowSwitcher,
			windowItems,
			std::size(windowItems),
			{ 260.0f, 440.0f },
			[&save, &reload]() {
				if (save && ImGui::Button("表示状態を保存")) {
					save();
				}
				if (reload) {
					ImGui::SameLine();
					if (ImGui::Button("復元")) {
						reload();
					}
				}
				Engine::Editor::DebugEditorManager::DrawHotReloadButton();
			});
	}

	if (windows.sceneView) {
		const Engine::Editor::DebugSceneViewportState viewport =
			Engine::Editor::DebugEditorManager::DrawSceneViewport(
				&windows.sceneView);
		ScreenUtil::SetDebugSceneInputActive(viewport.inputActive);
		if (viewport.drawn) {
			ScreenUtil::SetDebugSceneViewport(viewport.min, viewport.size);
		} else {
			ScreenUtil::ClearDebugSceneViewport();
		}
	} else {
		ScreenUtil::ClearDebugSceneViewport();
	}

	Engine::Editor::DebugEditorManager::SaveWindowItems(
		windowItems,
		std::size(windowItems));
	return previous != windows;
#else
	(void)windows;
	(void)save;
	(void)reload;
	(void)goToTitle;
	(void)goToResult;
	(void)playbackState;
	(void)togglePlayback;
	return false;
#endif
}

}
