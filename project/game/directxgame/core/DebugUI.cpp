#include "DebugUI.h"
#include "Input.h"
#include "GameSession.h"
#include "AudioDebugPanel.h"
#include "GameAudioTuning.h"
#include "DebugContext.h"
#include "DebugEditorShell.h"
#include "GameplayFlowController.h"
#include "DebugInput.h"
#include "PlayerDebugPanel.h"
#include "RenderingDebugPanel.h"
#include "DebugRuntime.h"
#include "DebugScene.h"
#include "VisualDebugPanel.h"
#include "GameParticleEffects.h"
#include "SceneTransitionPresentation.h"
#include "EnemyManager.h"
#include "Player.h"
#include "PlayerManager.h"
#include "GameplayHudPresentation.h"
#include "PauseBuildHud.h"
#include "GridPlane.h"
#include "SkyDome.h"
#include <string_view>
#ifdef _DEBUG
#include <imgui.h>
#endif

namespace DirectXGame {

DebugUIAction DebugUI::Update(
	DebugContext& debugContext,
	const GameplayFlowController& gameplayFlow,
	const SceneTransitionPresentation& sceneTransition,
	GameplayHudPresentation& gameplayHud,
	GameParticleEffects& particleEffects,
	PauseBuildHud& pauseBuildHud,
	Player* player,
	PlayerManager* playerManager,
	EnemyManager* enemyManager,
	GridPlane* gridPlane,
	SkyDome* skyDome,
	const GameSession* sessionContext,
	GameInputBindings::NavigationInputDevice navigationInputDevice,
	bool uiInitialized,
	float deathPresentationElapsed,
	const std::function<void()>& emitLevelUpConfetti)
{
#ifdef _DEBUG
	DebugWindowVisibility& windows = debugContext.Windows();
	Engine::InputSystem::Input* input =
		Engine::InputSystem::Input::GetInstance();
	if (input->TriggerKey(DIK_F5)) {
		debugContext.Load(player, particleEffects);
	}
	if (input->TriggerKey(DIK_F6) && playerManager) {
		playerManager->ForceDebugDeath();
	}
	if (input->TriggerKey(DIK_F8) && playerManager) {
		playerManager->AddEXP(10);
	}
	if (input->TriggerKey(DIK_F9) && playerManager) {
		playerManager->MakeDebugStrongest();
	}
	if (input->TriggerKey(DIK_F10)) {
		return DebugUIAction::RequestResult;
	}
	if (input->TriggerKey(DIK_F11)) {
		return DebugUIAction::ForceBoss;
	}

	DebugUIAction action = DebugUIAction::None;
	const Engine::Editor::DebugPlaybackState playbackState =
		debugContext.IsGameplayFrozen()
			? Engine::Editor::DebugPlaybackState::Paused
			: Engine::Editor::DebugPlaybackState::Playing;
	const bool windowVisibilityChanged = DebugUI::EditorShell::Draw(
		windows,
		[&]() { debugContext.Save(player, particleEffects); },
		[&]() { debugContext.Load(player, particleEffects); },
		[&]() { action = DebugUIAction::BackToTitle; },
		[&]() { action = DebugUIAction::RequestResult; },
		playbackState,
		[&]() { debugContext.GameplayFrozen() = !debugContext.GameplayFrozen(); });
	if (windows.keyInputDebug) {
		const bool gameplayUpdateRuns =
			gameplayFlow.IsCombatActive() &&
			!sceneTransition.HasPendingScene() &&
			!debugContext.IsGameplayFrozen();
		const DebugInputState state{
			navigationInputDevice,
			gameplayFlow.GetStateName(),
			sceneTransition.HasPendingScene()
				? std::string_view(sceneTransition.GetPendingSceneId())
				: std::string_view("none"),
			gameplayUpdateRuns,
			!gameplayFlow.Is(GameplayState::Paused) || !gameplayUpdateRuns,
			!gameplayFlow.Is(GameplayState::LevelUp) || !gameplayUpdateRuns,
			!gameplayFlow.Is(GameplayState::Dead) || !gameplayUpdateRuns,
		};
		DebugUI::Input::Draw(
			&windows.keyInputDebug,
			input,
			state);
	}
	if (windows.lightSettings) {
		DebugUI::Rendering::DrawLighting(
			&windows.lightSettings,
			debugContext.CameraEnabled(),
			debugContext.CameraPosition(),
			debugContext.CameraRotation(),
			debugContext.LightDrawEnabled(),
			debugContext.CollisionDrawEnabled(),
			[&]() { debugContext.UpdateCamera(); });
	}
	if (windows.audio) {
		ImGui::SetNextWindowPos(
			ImVec2(1125.0f, 12.0f),
			ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(
			ImVec2(360.0f, 360.0f),
			ImGuiCond_FirstUseEver);
		DebugUI::Audio::Draw(
			&windows.audio,
			GetGameAudioTuningEntries(),
			[&]() { debugContext.Save(player, particleEffects); },
			[&]() { debugContext.Load(player, particleEffects); });
	}
	if (windows.statisticsView) {
		const uint32_t gameFrameCount =
			sessionContext ? sessionContext->GetGameFrameCount() : 0u;
		debugContext.StatisticsPanel().Draw(
			&windows.statisticsView,
			gameplayFlow.GetStateName(),
			gameFrameCount,
			gameplayFlow.Is(GameplayState::Playing) &&
				!debugContext.IsGameplayFrozen(),
			enemyManager,
			playerManager);
	}
	if (windows.objectView || windows.objectSettings) {
		DebugUI::PlayerPanel::Draw(
			&windows.objectView,
			&windows.objectSettings,
			player,
			playerManager);
	}
	if (windows.particleView || windows.spriteManager) {
		DebugUI::Visual::Draw(
			&windows.particleView,
			&windows.spriteManager,
			uiInitialized,
			&debugContext.CollisionDrawEnabled(),
			gameplayHud.GetHitFlashTimer(),
			deathPresentationElapsed,
			player,
			particleEffects,
			gameplayHud,
			gameplayHud.GetTimer(),
			gameplayHud.GetHpGauge(),
			gameplayHud.GetExpGauge(),
			gameplayHud.GetKeyUi(),
			gameplayHud.GetPauseMiniMap(),
			pauseBuildHud,
			emitLevelUpConfetti);
	}
	if (windows.sceneSpecificDebug) {
		const SceneAction panelAction =
			DebugUI::Scene::Draw(
				&windows.sceneSpecificDebug,
				enemyManager);
		if (panelAction == SceneAction::GoToResult) {
			action = DebugUIAction::RequestResult;
		} else if (panelAction == SceneAction::ForceTimeUp) {
			action = DebugUIAction::ForceBoss;
		} else if (panelAction == SceneAction::BackToTitle) {
			action = DebugUIAction::BackToTitle;
		}
	}
	if (windows.offscreenSettings) {
		DebugUI::Rendering::DrawOffscreen(
			&windows.offscreenSettings);
	}
	if (windows.gizmo) {
		debugContext.GizmoPanel().Draw(&windows.gizmo, player);
	}
	if (windows.objectManager) {
		const RuntimeObjectStatus objectStatus{
			player != nullptr,
			playerManager != nullptr,
			enemyManager != nullptr,
			gridPlane != nullptr,
			skyDome != nullptr,
			sceneTransition.IsInitialized(),
		};
		DebugUI::Runtime::DrawObjectManager(
			&windows.objectManager,
			objectStatus,
			enemyManager);
	}
	if (windows.motionEditor) {
		DebugUI::Runtime::DrawMotionEditor(
			&windows.motionEditor);
	}
	if (windows.colliderTagManager) {
		DebugUI::Runtime::DrawColliderManager(
			&windows.colliderTagManager,
			&debugContext.CollisionDrawEnabled(),
			enemyManager,
			playerManager);
	}
	if (windowVisibilityChanged) {
		debugContext.Save(player, particleEffects);
	}
	return action;
#else
	(void)debugContext;
	(void)gameplayFlow;
	(void)sceneTransition;
	(void)gameplayHud;
	(void)particleEffects;
	(void)pauseBuildHud;
	(void)player;
	(void)playerManager;
	(void)enemyManager;
	(void)gridPlane;
	(void)skyDome;
	(void)sessionContext;
	(void)navigationInputDevice;
	(void)uiInitialized;
	(void)deathPresentationElapsed;
	(void)emitLevelUpConfetti;
	return DebugUIAction::None;
#endif
}

}
