#include "game/directxgame/core/GameplayDebugUiController.h"
#include "Input.h"
#include "game/directxgame/core/DirectXGameSessionContext.h"
#include "game/directxgame/core/GameAudioDebugPanel.h"
#include "game/directxgame/core/GameAudioTuning.h"
#include "game/directxgame/core/GameplayDebugContext.h"
#include "game/directxgame/core/GameplayDebugEditorShell.h"
#include "game/directxgame/core/GameplayFlowController.h"
#include "game/directxgame/core/GameplayInputDebugPanel.h"
#include "game/directxgame/core/GameplayPlayerDebugPanel.h"
#include "game/directxgame/core/GameplayRenderingDebugPanel.h"
#include "game/directxgame/core/GameplayRuntimeDebugPanel.h"
#include "game/directxgame/core/GameplaySceneDebugPanel.h"
#include "game/directxgame/core/GameplayVisualDebugPanel.h"
#include "game/directxgame/effects/GameParticleEffects.h"
#include "game/directxgame/effects/SceneTransitionPresentation.h"
#include "game/directxgame/enemy/EnemyManager.h"
#include "game/directxgame/player/Player.h"
#include "game/directxgame/player/PlayerManager.h"
#include "game/directxgame/ui/hud/GameplayHudPresentation.h"
#include "game/directxgame/ui/hud/PauseBuildHud.h"
#include "game/directxgame/world/GridPlane.h"
#include "game/directxgame/world/SkyDome.h"
#include <string_view>
#ifdef _DEBUG
#include <imgui.h>
#endif

namespace DirectXGame {

GameplayDebugUiAction GameplayDebugUiController::Update(
	GameplayDebugContext& debugContext,
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
	const DirectXGameSessionContext* sessionContext,
	GameInputBindings::NavigationInputDevice navigationInputDevice,
	bool uiInitialized,
	float deathPresentationElapsed,
	const std::function<void()>& emitLevelUpConfetti)
{
#ifdef _DEBUG
	GameplayDebugWindowVisibility& windows = debugContext.Windows();
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
		return GameplayDebugUiAction::RequestResult;
	}
	if (input->TriggerKey(DIK_F11)) {
		return GameplayDebugUiAction::ForceBoss;
	}

	GameplayDebugUiAction action = GameplayDebugUiAction::None;
	const bool windowVisibilityChanged = GameplayDebugEditorShell::Draw(
		windows,
		[&]() { debugContext.Save(player, particleEffects); },
		[&]() { debugContext.Load(player, particleEffects); },
		[&]() { action = GameplayDebugUiAction::BackToTitle; },
		[&]() { action = GameplayDebugUiAction::RequestResult; });

	if (windows.sceneSettings) {
		GameplaySceneDebugPanel::DrawSettings(
			&windows.sceneSettings,
			debugContext.GameplayFrozen(),
			gameplayFlow.GetStateName());
	}
	if (windows.keyInputDebug) {
		const bool gameplayUpdateRuns =
			gameplayFlow.IsCombatActive() &&
			!sceneTransition.HasPendingScene() &&
			!debugContext.IsGameplayFrozen();
		const GameplayInputDebugState state{
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
		GameplayInputDebugPanel::Draw(
			&windows.keyInputDebug,
			input,
			state);
	}
	if (windows.lightSettings) {
		GameplayRenderingDebugPanel::DrawLighting(
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
		GameAudioDebugPanel::Draw(
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
		GameplayPlayerDebugPanel::Draw(
			&windows.objectView,
			&windows.objectSettings,
			player,
			playerManager);
	}
	if (windows.particleView || windows.spriteManager) {
		GameplayVisualDebugPanel::Draw(
			&windows.particleView,
			&windows.spriteManager,
			uiInitialized,
			&debugContext.CollisionDrawEnabled(),
			gameplayHud.GetHitFlashTimer(),
			deathPresentationElapsed,
			player,
			particleEffects,
			gameplayHud.GetTimer(),
			gameplayHud.GetHpGauge(),
			gameplayHud.GetExpGauge(),
			gameplayHud.GetKeyUi(),
			gameplayHud.GetPauseMiniMap(),
			pauseBuildHud,
			emitLevelUpConfetti);
	}
	if (windows.sceneSpecificDebug) {
		const GameplayDebugAction panelAction =
			GameplaySceneDebugPanel::Draw(
				&windows.sceneSpecificDebug,
				enemyManager);
		if (panelAction == GameplayDebugAction::GoToResult) {
			action = GameplayDebugUiAction::RequestResult;
		} else if (panelAction == GameplayDebugAction::ForceTimeUp) {
			action = GameplayDebugUiAction::ForceBoss;
		} else if (panelAction == GameplayDebugAction::BackToTitle) {
			action = GameplayDebugUiAction::BackToTitle;
		}
	}
	if (windows.offscreenSettings) {
		GameplayRenderingDebugPanel::DrawOffscreen(
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
		GameplayRuntimeDebugPanel::DrawObjectManager(
			&windows.objectManager,
			objectStatus,
			enemyManager);
	}
	if (windows.motionEditor) {
		GameplayRuntimeDebugPanel::DrawMotionEditor(
			&windows.motionEditor);
	}
	if (windows.colliderTagManager) {
		GameplayRuntimeDebugPanel::DrawColliderManager(
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
	return GameplayDebugUiAction::None;
#endif
}

}
