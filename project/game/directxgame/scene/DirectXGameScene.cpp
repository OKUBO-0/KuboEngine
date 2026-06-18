#include "game/directxgame/scene/DirectXGameScene.h"
#include "game/directxgame/core/DirectXGameDataPaths.h"
#include "game/directxgame/core/GameplayDebugDraw.h"
#include "game/directxgame/core/GameplayDebugUiController.h"
#include "game/directxgame/core/GameMenuController.h"
#include "game/directxgame/core/GameModelCache.h"
#include "game/directxgame/core/DirectXGameSceneId.h"
#include "game/directxgame/core/DirectXGameSessionContext.h"
#include "game/directxgame/core/ScreenUtil.h"
#include "game/directxgame/core/SceneLighting.h"
#include "CameraManager.h"
#include "Input.h"
#include "LineCommon.h"
#include "MyMath.h"
#include "Object3DCommon.h"
#include "OffscreenRenderManager.h"
#include "ParticleManager.h"
#include "SceneManager.h"
#include "SpriteCommon.h"
#include "TextureManager.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace {

constexpr float kGameTimeLimitSeconds = 300.0f;
constexpr char kAudioStart[] = "game.start";
constexpr char kAudioPauseToggle[] = "game.pauseToggle";
constexpr char kAudioLevelUp[] = "game.levelUp";
constexpr char kAudioDeath[] = "game.death";
constexpr char kEnvironmentTexturePath[] = "Resources/textures/skybox/test.dds";
float Clamp01(float value)
{
	return std::clamp(value, 0.0f, 1.0f);
}

}

namespace DirectXGame {

DirectXGameScene::DirectXGameScene(std::shared_ptr<DirectXGameSessionContext> sessionContext)
	: sessionContext_(std::move(sessionContext))
{
}

void DirectXGameScene::Initialize()
{
	gameplayFlow_.Reset();
	Engine::CameraSystem::CameraManager::GetInstance()->Initialize();

	if (sessionContext_) {
		sessionContext_->OnEnterGameScene();
	}

	InitializeLighting();
	InitializeWorld();
	debugContext_.InitializeCamera();
	particleEffects_.Initialize();
	debugContext_.Load(player_.get(), particleEffects_);
	if (player_) {
		player_->StartIntroPresentation();
	}
	InitializeUi();
	combatEffectsPresentation_.Initialize();
	if (playerManager_) {
		combatEffectsPresentation_.Reset(*playerManager_);
	}
	sceneTransition_.Initialize();
	ApplyPostEffect();
}

void DirectXGameScene::Finalize()
{
	if (startSeHandle_ != 0) { GameAudioCache::Stop(startSeHandle_); }
	if (pauseSeHandle_ != 0) { GameAudioCache::Stop(pauseSeHandle_); }
	if (levelUpSeHandle_ != 0) { GameAudioCache::Stop(levelUpSeHandle_); }
	if (gameOverSeHandle_ != 0) { GameAudioCache::Stop(gameOverSeHandle_); }
	Engine::CameraSystem::CameraManager::GetInstance()->RemoveCamera("directxgame_player");
	debugContext_.FinalizeCamera();
}

void DirectXGameScene::Update()
{
	constexpr float kFixedDeltaTime = 1.0f / 60.0f;

	navigationInputDevice_ = GameInputBindings::DetectNavigationInputDevice(
		Engine::InputSystem::Input::GetInstance(),
		navigationInputDevice_);

	UpdateUi(kFixedDeltaTime);
	if (sceneTransition_.Update(kFixedDeltaTime)) {
		Engine::Scene::SceneManager::GetInstance()->ChangeScene(
			sceneTransition_.GetPendingSceneId());
	}
	if (sceneTransition_.HasPendingScene()) {
		ApplyPostEffect();
		UpdateDebugUi();
		return;
	}

#ifdef _DEBUG
	const bool gameplayFrozen = debugContext_.IsGameplayFrozen();
#else
	const bool gameplayFrozen = false;
#endif

	if (gameplayFlow_.Is(GameplayState::Playing) && !gameplayFrozen) {
		if (gameplayHud_.GetTimer().GetTime() >= kGameTimeLimitSeconds) {
			StartBossPhase();
		}
		UpdateGamePlay(kFixedDeltaTime);
	} else if (gameplayFlow_.Is(GameplayState::BossIntro) && !gameplayFrozen) {
		UpdateBossEntrance(kFixedDeltaTime);
	} else if (gameplayFlow_.Is(GameplayState::Boss) && !gameplayFrozen) {
		UpdateGamePlay(kFixedDeltaTime);
		if (enemyManager_ && enemyManager_->IsBossDefeated()) {
			StartBossDefeatPresentation();
		}
	} else if (gameplayFlow_.Is(GameplayState::BossDefeated)) {
		UpdateBossDefeatPresentation(kFixedDeltaTime);
	} else if (gameplayFlow_.Is(GameplayState::Start)) {
		gameplayFlow_.UpdateIntro(kFixedDeltaTime);
		if (player_) {
			player_->UpdateIntroPresentation(
				gameplayFlow_.GetIntroElapsed(),
				GameplayFlowController::kIntroDuration);
		}
	}
	UpdateEffects();

	if (gridPlane_ && player_) {
		gridPlane_->Update(player_->GetWorldPosition());
	}
	if (skyDome_) {
		skyDome_->Update();
	}
	debugContext_.UpdateCamera();

	if (sessionContext_ && gameplayFlow_.IsCombatActive() && !gameplayFrozen) {
		sessionContext_->AdvanceGameFrame();
	}

	if (gameplayFlow_.Is(GameplayState::Dead)) {
		RecordResultSummary();
		const GameMenuInputState menuInput = GameMenuController::Update(
			Engine::InputSystem::Input::GetInstance(),
			navigationInputDevice_);
		navigationInputDevice_ = menuInput.device;
		if (player_ &&
			playerDeathPresentation_.Update(
				*player_,
				kFixedDeltaTime,
				menuInput.confirm)) {
			RequestResultScene();
		}
	}

	ApplyPostEffect();
	UpdateDebugUi();
	QueueDebugDraw();
}

void DirectXGameScene::Draw()
{
	Engine::Graphics3D::Object3DCommon* objectCommon =
		Engine::Graphics3D::Object3DCommon::GetInstance();
	if (player_) {
		objectCommon->BeginShadowPass(player_->GetWorldPosition());
		player_->DrawShadow();
		if (enemyManager_) {
			enemyManager_->DrawShadow();
		}
		objectCommon->EndShadowPass();
	}

	objectCommon->CommonDraw();
	if (gridPlane_) {
		gridPlane_->Draw();
	}
	if (skyDome_) {
		skyDome_->Draw();
	}
	if (player_) {
		player_->Draw();
	}
	if (enemyManager_) {
		enemyManager_->Draw();
	}
	if (playerManager_) {
		playerManager_->Draw();
	}
	combatEffectsPresentation_.Draw();
	Engine::Particle::ParticleManager::GetInstance()->Draw();
	Engine::LineSystem::LineCommon::GetInstance()->Draw();

	Engine::Graphics2D::SpriteCommon::GetInstance()->CommonDraw();
	DrawUi();
	sceneTransition_.Draw();
}

void DirectXGameScene::InitializeLighting()
{
	SceneLighting::Defaults defaults{};
	defaults.light.color = { 1.0f, 0.94f, 0.88f, 1.0f };
	defaults.light.direction = MyMath::Normalize(Vector3{ -0.55f, -1.0f, -0.45f });
	defaults.light.intensity = 0.9f;
	defaults.light.ambientColor = { 0.48f, 0.52f, 0.62f, 1.0f };
	defaults.light.ambientIntensity = 0.34f;
	defaults.light.specularStrength = 0.16f;
	defaults.light.enable = 1;
	SceneLighting::ApplyDefaults(defaults);
}

void DirectXGameScene::InitializeWorld()
{
	Engine::Base::TextureManager::GetInstance()->LoadTextures({
		kEnvironmentTexturePath,
		});

	player_ = std::make_unique<Player>();
	player_->Initialize();

	playerManager_ = std::make_unique<PlayerManager>();
	playerManager_->Initialize(player_.get());
	playerManager_->LoadStatusFromCSV(DataPaths::kPlayerStatus);
	playerManager_->LoadWeaponUpgradeSettings(DataPaths::kWeaponUpgradeSettings);

	enemyManager_ = std::make_unique<EnemyManager>();
	enemyManager_->Initialize(DataPaths::Resolve(DataPaths::kEnemyTypes), player_.get(), playerManager_.get());

	gridPlane_ = std::make_unique<GridPlane>();
	gridPlane_->Initialize();
	gridPlane_->Update(player_->GetWorldPosition());

	skyDome_ = std::make_unique<SkyDome>();
	skyDome_->Initialize();
	skyDome_->Update();
}

void DirectXGameScene::InitializeUi()
{
	gameplayHud_.Initialize(playerManager_.get());
	levelUpSelectionHud_.Initialize();
	pauseBuildHud_.Initialize();

	startSeHandle_ = GameAudioCache::LoadWave("audio/se/se_exp.wav");
	pauseSeHandle_ = GameAudioCache::LoadWave("audio/se/se_pause.wav");
	levelUpSeHandle_ = GameAudioCache::LoadWave("audio/se/se_exp.wav");
	gameOverSeHandle_ = GameAudioCache::LoadWave("audio/se/se_death.wav");

	uiInitialized_ = true;
}

void DirectXGameScene::UpdateGamePlay(float deltaTime)
{
	if (player_ && enemyManager_) {
		Vector3 nearestEnemy{};
		float nearestDistance = 80.0f;
		if (enemyManager_->FindNearestEnemyPosition(player_->GetWorldPosition(), 120.0f, nearestEnemy)) {
			const Vector3 delta = nearestEnemy - player_->GetWorldPosition();
			nearestDistance = std::sqrt(delta.x * delta.x + delta.z * delta.z);
		}
		const float distanceRatio = Clamp01((nearestDistance - 8.0f) / 52.0f);
		player_->SetCombatCameraTarget(
			36.0f + distanceRatio * 20.0f,
			62.0f + distanceRatio * 26.0f);
	}
	if (player_) {
		player_->Update(deltaTime);
	}
	if (playerManager_) {
		playerManager_->Update(deltaTime);
	}
	if (enemyManager_) {
		enemyManager_->Update(deltaTime);
		enemyManager_->CheckCollisions(player_.get(), playerManager_.get());
	}
	if (gameplayFlow_.Is(GameplayState::Playing) &&
		playerManager_ &&
		playerManager_->IsLevelUpRequested()) {
		RequestLevelUp();
	}
	if (playerManager_ && playerManager_->IsDead()) {
		if (!gameOverSePlayed_ && gameOverSeHandle_ != 0) {
			GameAudioCache::Play(gameOverSeHandle_);
			GameAudioCache::SetVolumeFromTuning(gameOverSeHandle_, kAudioDeath, 1.0f);
			gameOverSePlayed_ = true;
		}
		if (player_) {
			playerDeathPresentation_.Start(*player_, particleEffects_);
		}
		gameplayFlow_.BeginDead();
	}
}

void DirectXGameScene::UpdateEffects()
{
	if (!playerManager_ || !player_) {
		return;
	}
	if (combatEffectsPresentation_.Update(
		*player_,
		*playerManager_,
		enemyManager_.get(),
		particleEffects_)) {
		gameplayHud_.TriggerHitFlash(0.18f);
	}
}

void DirectXGameScene::UpdateUi(float deltaTime)
{
	if (!uiInitialized_) {
		return;
	}

	Engine::InputSystem::Input* input = Engine::InputSystem::Input::GetInstance();
	const bool cursorHiddenState = gameplayFlow_.IsCursorHidden();
	if (input && cursorHiddenState && ScreenUtil::IsInsideDebugSceneViewport(input->GetMousePos())) {
		::SetCursor(nullptr);
	} else if (!cursorHiddenState) {
		::SetCursor(::LoadCursor(nullptr, IDC_ARROW));
	}

#ifdef _DEBUG
	const bool gameplayFrozen = debugContext_.IsGameplayFrozen();
#else
	const bool gameplayFrozen = false;
#endif
	gameplayHud_.Update(
		deltaTime,
		gameplayFrozen,
		gameplayFlow_,
		player_.get(),
		playerManager_.get(),
		enemyManager_.get(),
		input,
		playerDeathPresentation_.GetOverlayAlpha());

	const GameMenuInputState menuInput = GameMenuController::Update(
		Engine::InputSystem::Input::GetInstance(),
		navigationInputDevice_);
	navigationInputDevice_ = menuInput.device;

	if (gameplayFlow_.Is(GameplayState::Start)) {
		if (gameplayFlow_.IsIntroFinished() && menuInput.confirm) {
			EnterPlaying();
		}
		if (menuInput.cancel) {
			RequestSceneChange(SceneId::kTitle);
		}
		return;
	}

	if (gameplayFlow_.Is(GameplayState::Paused)) {
		if (!playerManager_) {
			return;
		}
		const PauseMenuAction action = pauseBuildHud_.Update(
			*playerManager_,
			gameplayHud_.GetAnimationTime(),
			menuInput.moveDelta,
			menuInput.confirm,
			menuInput.cancel,
			navigationInputDevice_);
		if (action == PauseMenuAction::Resume) {
			EnterPlaying();
		} else if (action == PauseMenuAction::BackToTitle) {
			RequestSceneChange(SceneId::kTitle);
		}
		return;
	}

	if (gameplayFlow_.Is(GameplayState::LevelUp)) {
		if (playerManager_ &&
			levelUpSelectionHud_.Update(
				*playerManager_,
				deltaTime,
				gameplayHud_.GetAnimationTime(),
				menuInput.moveDelta,
				menuInput.confirm,
				navigationInputDevice_)) {
			EnterPlaying();
		}
		return;
	}

	if (gameplayFlow_.IsCombatActive() && menuInput.pause) {
		TogglePause();
	}
}

void DirectXGameScene::DrawUi()
{
	if (!uiInitialized_) {
		return;
	}

	gameplayHud_.Draw(gameplayFlow_);
	if (gameplayFlow_.Is(GameplayState::Paused)) {
		gameplayHud_.DrawPauseMap();
		pauseBuildHud_.Draw();
	} else if (gameplayFlow_.Is(GameplayState::LevelUp)) {
		levelUpSelectionHud_.Draw();
	}
}

void DirectXGameScene::EnterPlaying()
{
	const bool enteringFromStart = gameplayFlow_.EnterPlaying();
	if (enteringFromStart && startSeHandle_ != 0) {
		GameAudioCache::Play(startSeHandle_);
		GameAudioCache::SetVolumeFromTuning(startSeHandle_, kAudioStart, 1.0f);
	}
	if (enteringFromStart && player_) {
		player_->SuppressNextDodgeTrigger();
	}
	playerDeathPresentation_.Reset();
	bossPresentation_.Reset();
}

void DirectXGameScene::TogglePause()
{
	if (gameplayFlow_.BeginPause()) {
		pauseBuildHud_.Start();
		if (pauseSeHandle_ != 0) {
			GameAudioCache::Play(pauseSeHandle_);
			GameAudioCache::SetVolumeFromTuning(pauseSeHandle_, kAudioPauseToggle, 0.5f);
		}
	} else if (gameplayFlow_.Is(GameplayState::Paused)) {
		if (pauseSeHandle_ != 0) {
			GameAudioCache::Play(pauseSeHandle_);
			GameAudioCache::SetVolumeFromTuning(pauseSeHandle_, kAudioPauseToggle, 0.5f);
		}
		EnterPlaying();
	}
}

void DirectXGameScene::StartBossPhase()
{
	if (!gameplayFlow_.BeginBossIntro()) {
		return;
	}
	if (!enemyManager_) {
		gameplayFlow_.EnterPlaying();
		return;
	}
	bossPresentation_.StartEntrance(*enemyManager_);
}

void DirectXGameScene::UpdateBossEntrance(float deltaTime)
{
	if (enemyManager_ &&
		bossPresentation_.UpdateEntrance(*enemyManager_, deltaTime)) {
		gameplayFlow_.EnterBoss();
	}
}

void DirectXGameScene::StartBossDefeatPresentation()
{
	if (!gameplayFlow_.BeginBossDefeated()) {
		return;
	}

	if (enemyManager_) {
		bossPresentation_.StartDefeat(*enemyManager_);
	}
	RecordResultSummary();
}

void DirectXGameScene::UpdateBossDefeatPresentation(float deltaTime)
{
	RecordResultSummary();
	if (enemyManager_ &&
		bossPresentation_.UpdateDefeat(
			*enemyManager_, particleEffects_, deltaTime)) {
		RequestResultScene();
	}
}

void DirectXGameScene::RequestLevelUp()
{
	if (!playerManager_) {
		return;
	}
	playerManager_->ClearLevelUpRequest();
	levelUpSelectionHud_.Start(*playerManager_);
	gameplayFlow_.BeginLevelUp();
	SpawnLevelUpConfetti();
	if (levelUpSeHandle_ != 0) {
		GameAudioCache::Play(levelUpSeHandle_);
		GameAudioCache::SetVolumeFromTuning(levelUpSeHandle_, kAudioLevelUp, 1.0f);
	}
}

void DirectXGameScene::RequestSceneChange(const char* sceneId)
{
	sceneTransition_.Request(sceneId);
}

void DirectXGameScene::RequestResultScene()
{
	RecordResultSummary();
	RequestSceneChange(SceneId::kResult);
}

void DirectXGameScene::RecordResultSummary()
{
	if (!sessionContext_) {
		return;
	}

	sessionContext_->SetResultSummary(
		sessionContext_->GetGameFrameCount(),
		playerManager_ ? static_cast<uint32_t>(playerManager_->GetLevel()) : 1u + (sessionContext_->GetGameFrameCount() / 180u),
		enemyManager_ ? static_cast<uint32_t>(enemyManager_->GetTotalKillCount()) : sessionContext_->GetGameFrameCount() / 60u,
		playerManager_ ? playerManager_->GetTotalEXP() : 0);
}

void DirectXGameScene::SpawnLevelUpConfetti()
{
	if (!player_) {
		return;
	}
	levelUpSelectionHud_.SpawnConfetti(*player_, particleEffects_);
}

void DirectXGameScene::QueueDebugDraw()
{
	GameplayDebugDraw::Queue(
		debugContext_.IsCollisionDrawEnabled(),
		debugContext_.IsLightDrawEnabled(),
		player_.get(),
		enemyManager_.get(),
		playerManager_.get());
}

void DirectXGameScene::UpdateDebugUi()
{
	const GameplayDebugUiAction action =
		GameplayDebugUiController::Update(
			debugContext_,
			gameplayFlow_,
			sceneTransition_,
			gameplayHud_,
			particleEffects_,
			pauseBuildHud_,
			player_.get(),
			playerManager_.get(),
			enemyManager_.get(),
			gridPlane_.get(),
			skyDome_.get(),
			sessionContext_.get(),
			navigationInputDevice_,
			uiInitialized_,
			playerDeathPresentation_.GetElapsedTime(),
			[this]() { SpawnLevelUpConfetti(); });
	if (action == GameplayDebugUiAction::RequestResult) {
		RequestResultScene();
	} else if (action == GameplayDebugUiAction::ForceBoss) {
		gameplayHud_.GetTimer().SetTime(kGameTimeLimitSeconds);
		StartBossPhase();
	} else if (action == GameplayDebugUiAction::BackToTitle) {
		RequestSceneChange(SceneId::kTitle);
	}
}

void DirectXGameScene::ApplyPostEffect() const
{
	Engine::Base::OffscreenRenderManager* offscreen = Engine::Base::OffscreenRenderManager::GetInstance();
	if (!offscreen) {
		return;
	}

	PostEffectType effect = PostEffectType::Fullscreen;
	switch (gameplayFlow_.GetState()) {
	case GameplayState::Paused:
		effect = PostEffectType::Fullscreen;
		break;
	case GameplayState::LevelUp:
		effect = PostEffectType::Fullscreen;
		break;
	case GameplayState::Dead:
		effect = PostEffectType::Grayscale;
		break;
	case GameplayState::Start:
	case GameplayState::Playing:
	case GameplayState::BossIntro:
	case GameplayState::Boss:
	case GameplayState::BossDefeated:
	default:
		effect = PostEffectType::Fullscreen;
		break;
	}
	offscreen->SetScenePostEffectType(effect);
}

}
