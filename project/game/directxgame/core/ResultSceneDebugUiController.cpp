#include "game/directxgame/core/ResultSceneDebugUiController.h"

#include "DebugEditorManager.h"
#include "Input.h"
#include "game/directxgame/core/DirectXGameSceneId.h"
#include "game/directxgame/core/DirectXGameSessionContext.h"
#include "game/directxgame/core/GameAudioDebugPanel.h"
#include "game/directxgame/core/GameInputBindings.h"
#include "game/directxgame/core/ScreenUtil.h"
#include "game/directxgame/scene/DirectXGameResultScene.h"
#include <array>
#include <iterator>
#ifdef _DEBUG
#include <imgui.h>
#endif

namespace {

constexpr char kAudioResultFinish[] = "result.finish";

#ifdef _DEBUG
bool DrawVector2Setting(
	const char* label,
	Vector2& value,
	float minimum,
	float maximum)
{
	float components[2]{ value.x, value.y };
	if (!ImGui::DragFloat2(
			label,
			components,
			1.0f,
			minimum,
			maximum)) {
		return false;
	}
	value = { components[0], components[1] };
	return true;
}
#endif

}

namespace DirectXGame {

void ResultSceneDebugUiController::Draw(DirectXGameResultScene& scene)
{
#ifdef _DEBUG
	auto& windows = scene.debugWindows_;
	Engine::InputSystem::Input* input =
		Engine::InputSystem::Input::GetInstance();
	if (input && input->TriggerKey(DIK_F5)) {
		scene.ReloadDebugData();
	}

	const DirectXGameResultScene::DebugWindowVisibility previousWindows =
		windows;
	const Engine::Editor::DebugEditorMenuItem windowItems[] = {
		{ "Scene", &windows.sceneView },
		{ "統計", &windows.statisticsView },
		{ "シーン設定", &windows.sceneSettings },
		{ "オーディオ", &windows.audio },
		{ "キー操作デバッグ", &windows.keyInputDebug },
	};
	const Engine::Editor::DebugEditorMenuItem editItems[] = {
		{ "シーン設定", &windows.sceneSettings },
	};
	const Engine::Editor::DebugEditorMenuItem objectItems[] = {
		{ "Scene", &windows.sceneView },
		{ "シーン設定", &windows.sceneSettings },
	};
	Engine::Editor::DebugEditorManager::DrawMainMenu({
		windowItems,
		std::size(windowItems),
		editItems,
		std::size(editItems),
		objectItems,
		std::size(objectItems),
		"リザルトレイアウトを保存",
		[&scene]() { scene.SaveLayout(); },
		"リザルト設定を再読み込み",
		[&scene]() { scene.ReloadDebugData(); },
		"Result Debug UI",
		[&scene]() { scene.RequestSceneChange(SceneId::kTitle); },
		{},
		{},
		&windows.windowSwitcher,
	});

	if (windows.windowSwitcher) {
		Engine::Editor::DebugEditorManager::DrawWindowSwitcher(
			"ウィンドウ表示切り替え",
			&windows.windowSwitcher,
			windowItems,
			std::size(windowItems),
			{ 260.0f, 180.0f },
			[]() {
				Engine::Editor::DebugEditorManager::DrawHotReloadButton();
			});
	}

	if (windows.sceneView) {
		const Engine::Editor::DebugSceneViewportState sceneViewport =
			Engine::Editor::DebugEditorManager::DrawSceneViewport(
				&windows.sceneView);
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
		const std::array<AudioTuningEntry, 1> resultAudioEntries{ {
			{ "Result Finish Volume", kAudioResultFinish, 1.0f },
		} };
		GameAudioDebugPanel::Draw(
			&windows.audio,
			resultAudioEntries);
	}

	if (windows.sceneSettings) {
		ImGui::Begin("シーン設定", &windows.sceneSettings);
		ImGui::Checkbox(
			"Enable Result Layout Debug",
			&scene.layoutDebugEnabled_);
		if (scene.layoutDebugEnabled_) {
			bool layoutChanged = false;
			layoutChanged |= DrawVector2Setting(
				"Background Position",
				scene.backgroundPosition_,
				-400.0f,
				1280.0f);
			layoutChanged |= DrawVector2Setting(
				"Background Size",
				scene.backgroundSize_,
				64.0f,
				1600.0f);
			layoutChanged |= DrawVector2Setting(
				"Result UI Position",
				scene.resultPosition_,
				-400.0f,
				1280.0f);
			layoutChanged |= DrawVector2Setting(
				"Result UI Size",
				scene.resultSize_,
				64.0f,
				1600.0f);
			layoutChanged |= DrawVector2Setting(
				"Finish UI Position",
				scene.finishPosition_,
				-400.0f,
				1280.0f);
			layoutChanged |= DrawVector2Setting(
				"Finish UI Size",
				scene.finishSize_,
				64.0f,
				1600.0f);
			layoutChanged |= DrawVector2Setting(
				"EXP Position",
				scene.expPosition_,
				-400.0f,
				1280.0f);
			layoutChanged |= DrawVector2Setting(
				"Level Position",
				scene.levelPosition_,
				-400.0f,
				1280.0f);
			layoutChanged |= DrawVector2Setting(
				"Kill Position",
				scene.killPosition_,
				-400.0f,
				1280.0f);
			layoutChanged |= DrawVector2Setting(
				"Total Score Position",
				scene.totalScorePosition_,
				-400.0f,
				1280.0f);
			layoutChanged |= DrawVector2Setting(
				"Digit Size",
				scene.digitSize_,
				4.0f,
				128.0f);
			layoutChanged |= ImGui::DragFloat(
				"Score Scale",
				&scene.scoreScale_,
				0.05f,
				0.25f,
				6.0f);
			if (layoutChanged) {
				scene.ApplyLayout();
			}
			if (ImGui::Button("Save Result Layout")) {
				scene.SaveLayout();
			}
		}
		if (ImGui::Button("Back To Title")) {
			scene.RequestSceneChange(SceneId::kTitle);
		}
		ImGui::End();
	}

	if (windows.statisticsView) {
		ImGui::Begin("統計", &windows.statisticsView);
		ImGui::TextUnformatted("Stage 12 result scene");
		ImGui::Text(
			"Count Up Finished: %s",
			scene.countUpFinished_ ? "true" : "false");
		ImGui::Text("Displayed EXP: %.0f", scene.displayedExp_);
		ImGui::Text("Displayed Level: %.0f", scene.displayedLevel_);
		ImGui::Text("Displayed Kills: %.0f", scene.displayedKills_);
		ImGui::Text(
			"Displayed Total Score: %.0f",
			scene.displayedTotalScore_);
		if (scene.sessionContext_) {
			const DirectXGameResultData& resultData =
				scene.sessionContext_->GetResultData();
			ImGui::Separator();
			ImGui::Text(
				"Run Count: %u",
				scene.sessionContext_->GetRunCount());
			ImGui::Text(
				"Elapsed Frames: %u",
				resultData.elapsedFrames);
			ImGui::Text("Level: %u", resultData.level);
			ImGui::Text("Kill Count: %u", resultData.killCount);
		}
		ImGui::End();
	}

	if (windows.keyInputDebug) {
		ImGui::Begin(
			"キー操作デバッグ",
			&windows.keyInputDebug);
		ImGui::TextUnformatted("リザルトシーン入力");
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
		ImGui::Text(
			"Pending Scene: %s",
			scene.pendingSceneId_.empty()
				? "none"
				: scene.pendingSceneId_.c_str());
		ImGui::End();
	}

	Engine::Editor::DebugEditorManager::SaveWindowItems(
		windowItems,
		std::size(windowItems));
	if (previousWindows.windowSwitcher != windows.windowSwitcher ||
		previousWindows.sceneView != windows.sceneView ||
		previousWindows.statisticsView != windows.statisticsView ||
		previousWindows.sceneSettings != windows.sceneSettings ||
		previousWindows.audio != windows.audio ||
		previousWindows.keyInputDebug != windows.keyInputDebug) {
		scene.SaveLayout();
	}
#else
	(void)scene;
#endif
}

}
