#include "game/directxgame/core/GameplayDebugEditorShell.h"
#include "DebugEditorManager.h"
#include "game/directxgame/core/ScreenUtil.h"
#include <iterator>
#ifdef _DEBUG
#include <imgui.h>
#endif

namespace DirectXGame {

bool GameplayDebugEditorShell::Draw(
	GameplayDebugWindowVisibility& windows,
	const std::function<void()>& save,
	const std::function<void()>& reload,
	const std::function<void()>& goToTitle,
	const std::function<void()>& goToResult)
{
#ifdef _DEBUG
	const GameplayDebugWindowVisibility previous = windows;
	const Engine::Editor::DebugEditorMenuItem windowItems[] = {
		{ "Scene", &windows.sceneView },
		{ "オブジェクトビュー", &windows.objectView },
		{ "パーティクルビュー", &windows.particleView },
		{ "統計", &windows.statisticsView },
		{ "オフスクリーン設定", &windows.offscreenSettings },
		{ "ライト設定", &windows.lightSettings },
		{ "ギズモ", &windows.gizmo },
		{ "オブジェクトマネージャ", &windows.objectManager },
		{ "モーションエディター", &windows.motionEditor },
		{ "スプライトマネージャ", &windows.spriteManager },
		{ "コライダー/タグ管理", &windows.colliderTagManager },
		{ "オーディオ", &windows.audio },
		{ "キー操作デバッグ", &windows.keyInputDebug },
		{ "シーン設定", &windows.sceneSettings },
		{ "シーン固有デバッグ", &windows.sceneSpecificDebug },
		{ "オブジェクト設定", &windows.objectSettings },
	};
	const Engine::Editor::DebugEditorMenuItem editItems[] = {
		{ "ギズモ", &windows.gizmo },
		{ "オブジェクト設定", &windows.objectSettings },
		{ "モーションエディター", &windows.motionEditor },
	};
	const Engine::Editor::DebugEditorMenuItem objectItems[] = {
		{ "オブジェクトマネージャ", &windows.objectManager },
		{ "オブジェクト設定", &windows.objectSettings },
		{ "コライダー/タグ管理", &windows.colliderTagManager },
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
	return false;
#endif
}

}
