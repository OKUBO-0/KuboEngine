#include "PlayScene.h"
#include "DataPaths.h"
#include "DebugDraw.h"
#include "DebugUI.h"
#include "GameAudioTuning.h"
#include "GameMenuController.h"
#include "GameModelCache.h"
#include "SceneId.h"
#include "GameSession.h"
#include "ScreenUtil.h"
#include "SceneLighting.h"
#include "CameraManager.h"
#include "Input.h"
#include "LineCommon.h"
#include "MyMath.h"
#include "Object3DCommon.h"
#include "OffscreenRenderManager.h"
#include "ParticleManager.h"
#include "SceneManager.h"
#include "SpriteCommon.h"
#include "SrvManager.h"
#include "TextureManager.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace {

constexpr float kGameTimeLimitSeconds = 180.0f;
constexpr char kGameBgmPath[] = "bgm/game_loop.wav";
constexpr char kBossBgmPath[] = "bgm/boss_loop.wav";
constexpr char kStartSePath[] = "se/game_start.wav";
constexpr char kPauseSePath[] = "se/pause_toggle.wav";
constexpr char kLevelUpSePath[] = "se/level_up.wav";
constexpr char kGameOverSePath[] = "se/game_over.wav";
constexpr char kBossPhaseSePath[] = "se/boss_phase.wav";
constexpr char kBossDefeatSePath[] = "se/boss_defeat.wav";
constexpr char kAudioGameBgm[] = "game.bgm";
constexpr char kAudioBossBgm[] = "boss.bgm";
constexpr char kAudioStart[] = "game.start";
constexpr char kAudioPauseToggle[] = "game.pauseToggle";
constexpr char kAudioLevelUp[] = "game.levelUp";
constexpr char kAudioGameOver[] = "game.over";
constexpr char kAudioBossPhase[] = "combat.bossPhase";
constexpr char kAudioBossDefeat[] = "boss.defeat";
constexpr char kEnvironmentTexturePath[] = "Resources/textures/skybox/test.dds";

const char* CharacterStatsKey(DirectXGame::CharacterId id)
{
	switch (id) {
	case DirectXGame::CharacterId::Octopus: return "Octopus";
	case DirectXGame::CharacterId::Flame: return "Flame";
	case DirectXGame::CharacterId::Blade: return "Blade";
	case DirectXGame::CharacterId::Storm: return "Storm";
	}
	return "Octopus";
}

}

namespace DirectXGame {

PlayScene::PlayScene(std::shared_ptr<GameSession> sessionContext)
	: sessionContext_(std::move(sessionContext))
{
}

void PlayScene::Initialize()
{
	LoadGameAudioTuning();
	GameAudioCache::StopBus(AudioBus::Bgm);
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
	floatingNumberPresentation_.Initialize();
	if (playerManager_) {
		combatEffectsPresentation_.Reset(*playerManager_);
	}
	sceneTransition_.Initialize();
	ApplyPostEffect();
}

void PlayScene::Finalize()
{
	GameAudioCache::StopBus(AudioBus::Bgm);
	Engine::CameraSystem::CameraManager::GetInstance()->RemoveCamera("directxgame_player");
	debugContext_.FinalizeCamera();
}

void PlayScene::Update()
{
	constexpr float kFixedDeltaTime = 1.0f / 60.0f;
	if (UpdateSceneStressTelemetry()) {
		return;
	}

	navigationInputDevice_ = GameInputBindings::DetectNavigationInputDevice(
		Engine::InputSystem::Input::GetInstance(),
		navigationInputDevice_);

	UpdateUi(kFixedDeltaTime);
	if (UpdatePendingSceneTransition(kFixedDeltaTime)) {
		return;
	}

#ifdef _DEBUG
	const bool gameplayFrozen = debugContext_.IsGameplayFrozen();
#else
	const bool gameplayFrozen = false;
	Engine::InputSystem::Input* input = Engine::InputSystem::Input::GetInstance();
	if (input && input->TriggerKey(DIK_F9) && playerManager_) {
		playerManager_->MakeDebugStrongest();
	}
	if (input && input->TriggerKey(DIK_F11)) {
		gameplayHud_.GetTimer().SetTime(kGameTimeLimitSeconds);
		StartBossPhase();
	}
#endif

	UpdateGameplayPhase(kFixedDeltaTime, gameplayFrozen);
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
	UpdateDebugUI();
	QueueDebugDraw();
}

void PlayScene::Draw()
{
	Engine::Graphics3D::Object3DCommon* objectCommon =
		Engine::Graphics3D::Object3DCommon::GetInstance();
	if (player_ && objectCommon->BeginShadowPass(player_->GetWorldPosition())) {
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
	floatingNumberPresentation_.Draw();
	DrawUi();
	sceneTransition_.Draw();
}

void PlayScene::InitializeLighting()
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

void PlayScene::InitializeWorld()
{
	Engine::Base::TextureManager::GetInstance()->LoadTextures({
		kEnvironmentTexturePath,
		"Resources/DirectXGame/white1x1.png",
		});
	GameModelCache::LoadBatch({
		"cube.obj",
		"bullet.obj",
		"ExpOrb.obj",
		"plane.obj",
		"skydome.obj",
		"Enemy1.obj",
		"Enemy2.obj",
		"Enemy3.obj",
		"Enemy4.obj",
		"octopus.obj",
		});

	player_ = std::make_unique<Player>();
	player_->Initialize();

	playerManager_ = std::make_unique<PlayerManager>();
	playerManager_->Initialize(player_.get());
	playerManager_->LoadStatusFromCSV(DataPaths::kPlayerStatus);
	if (sessionContext_) {
		playerManager_->LoadCharacterStats(
			DataPaths::Resolve(DataPaths::kCharacterStats),
			CharacterStatsKey(sessionContext_->GetSelectedCharacterId()));
		playerManager_->ApplyPermanentUpgrades(
			sessionContext_->GetPermanentMaxHPLevel(),
			sessionContext_->GetPermanentAttackLevel(),
			sessionContext_->GetPermanentMoveSpeedLevel(),
			sessionContext_->GetPermanentExpPickupRangeLevel(),
			sessionContext_->GetPermanentCoinGainLevel());
	}
	playerManager_->LoadWeaponUpgradeSettings(DataPaths::kWeaponUpgradeSettings);
	if (sessionContext_) {
		switch (sessionContext_->GetSelectedCharacterId()) {
		case CharacterId::Storm:
			playerManager_->AddLightning();
			break;
		case CharacterId::Octopus:
		case CharacterId::Flame:
		case CharacterId::Blade:
		default:
			break;
		}
	}

	enemyManager_ = std::make_unique<EnemyManager>();
	if (sessionContext_) {
		enemyManager_->SetSession(sessionContext_.get());
		enemyManager_->SetRandomSeed(
			sessionContext_->GetRunRandomSeed());
	}
	enemyManager_->Initialize(DataPaths::Resolve(DataPaths::kEnemyTypes), player_.get(), playerManager_.get());

	gridPlane_ = std::make_unique<GridPlane>();
	gridPlane_->Initialize();
	gridPlane_->Update(player_->GetWorldPosition());

	skyDome_ = std::make_unique<SkyDome>();
	skyDome_->Initialize();
	skyDome_->Update();
}

void PlayScene::InitializeUi()
{
	gameplayHud_.Initialize(playerManager_.get());
	levelUpSelectionHud_.Initialize();
	pauseBuildHud_.Initialize();

	gameBgmHandle_ = GameAudioCache::LoadWave(kGameBgmPath, AudioBus::Bgm);
	bossBgmHandle_ = GameAudioCache::LoadWave(kBossBgmPath, AudioBus::Bgm);
	startSeHandle_ = GameAudioCache::LoadWave(kStartSePath);
	pauseSeHandle_ = GameAudioCache::LoadWave(kPauseSePath);
	levelUpSeHandle_ = GameAudioCache::LoadWave(kLevelUpSePath);
	gameOverSeHandle_ = GameAudioCache::LoadWave(kGameOverSePath);
	bossPhaseSeHandle_ = GameAudioCache::LoadWave(kBossPhaseSePath);
	bossDefeatSeHandle_ = GameAudioCache::LoadWave(kBossDefeatSePath);
	if (gameBgmHandle_) {
		GameAudioCache::StopBus(AudioBus::Bgm);
		GameAudioCache::SetVolumeFromTuning(gameBgmHandle_, kAudioGameBgm, 0.34f);
		GameAudioCache::PlayLoop(gameBgmHandle_);
	}
	if (bossBgmHandle_) {
		GameAudioCache::SetVolumeFromTuning(bossBgmHandle_, kAudioBossBgm, 0.4f);
	}

	uiInitialized_ = true;
}

bool PlayScene::UpdateSceneStressTelemetry()
{
	if (!sessionContext_ || !sessionContext_->IsSceneStressEnabled()) {
		return false;
	}

	sessionContext_->AdvanceSceneStressFrame();
	if (enemyManager_) {
		enemyManager_->AppendCollisionTelemetryCsv(
			sessionContext_->GetSceneStressFrameCount());
		sessionContext_->RecordSceneObjectCounts(
			static_cast<uint32_t>(enemyManager_->GetActiveEnemyCount()),
			static_cast<uint32_t>(enemyManager_->GetEnemies().size()),
			static_cast<uint32_t>(enemyManager_->GetExpOrbCount()));
	}
	if (Engine::Base::SrvManager* srvManager =
		Engine::Graphics3D::Object3DCommon::GetInstance()->GetSrvManager()) {
		const Engine::Base::SrvManager::UsageSummary usage =
			srvManager->GetUsageSummary();
		sessionContext_->RecordSrvUsage(
			GameSession::SceneStressStage::Game,
			srvManager->GetUsedCount(),
			srvManager->GetHighWatermark(),
			usage.texture2D,
			usage.textureCube,
			usage.structuredBuffer,
			usage.shadowMap,
			usage.other);
	}
	if (sessionContext_->GetSceneStressFrameCount() <
		sessionContext_->GetSceneStressGameFrameTarget()) {
		return false;
	}

	Engine::Scene::SceneManager::GetInstance()->ChangeScene(SceneId::kResult);
	return true;
}

bool PlayScene::UpdatePendingSceneTransition(float deltaTime)
{
	if (sceneTransition_.Update(deltaTime)) {
		Engine::Scene::SceneManager::GetInstance()->ChangeScene(
			sceneTransition_.GetPendingSceneId());
	}
	if (!sceneTransition_.HasPendingScene()) {
		return false;
	}

	ApplyPostEffect();
	UpdateDebugUI();
	return true;
}

void PlayScene::UpdateGameplayPhase(float deltaTime, bool gameplayFrozen)
{
	if (gameplayFlow_.Is(GameplayState::Playing) && !gameplayFrozen) {
		if (gameplayHud_.GetTimer().GetTime() >= kGameTimeLimitSeconds) {
			StartBossPhase();
		}
		UpdateGamePlay(deltaTime);
	} else if (gameplayFlow_.Is(GameplayState::BossIntro) && !gameplayFrozen) {
		UpdateBossEntrance(deltaTime);
	} else if (gameplayFlow_.Is(GameplayState::Boss) && !gameplayFrozen) {
		UpdateGamePlay(deltaTime);
		if (enemyManager_ && enemyManager_->IsBossDefeated()) {
			StartBossDefeatPresentation();
		}
	} else if (gameplayFlow_.Is(GameplayState::BossDefeated)) {
		UpdateBossDefeatPresentation(deltaTime);
	} else if (gameplayFlow_.Is(GameplayState::Start)) {
		gameplayFlow_.UpdateIntro(deltaTime);
		if (player_) {
			player_->UpdateIntroPresentation(
				gameplayFlow_.GetIntroElapsed(),
				GameplayFlowController::kIntroDuration);
		}
		if (gameplayFlow_.IsIntroFinished()) {
			EnterPlaying();
		}
	}
}

void PlayScene::UpdateGamePlay(float deltaTime)
{
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
		if (!gameOverSePlayed_ && gameOverSeHandle_) {
			GameAudioCache::PlayTuned(gameOverSeHandle_, kAudioGameOver, 0.78f);
			gameOverSePlayed_ = true;
		}
		if (player_) {
			playerDeathPresentation_.Start(*player_, particleEffects_);
		}
		gameplayFlow_.BeginDead();
	}
}

void PlayScene::UpdateEffects()
{
	if (!playerManager_ || !player_) {
		return;
	}
	if (enemyManager_) {
		floatingNumberPresentation_.AddEvents(
			enemyManager_->GetRecentFloatingNumberEvents());
	}
	if (combatEffectsPresentation_.Update(
		*player_,
		*playerManager_,
		enemyManager_.get(),
		particleEffects_)) {
		gameplayHud_.TriggerHitFlash(0.18f);
	}
	floatingNumberPresentation_.Update(1.0f / 60.0f);
}

void PlayScene::UpdateUi(float deltaTime)
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
		playerDeathPresentation_.GetOverlayAlpha(),
		sessionContext_ ? sessionContext_->GetRunCoins() : 0);
	if (playerManager_) {
		pauseBuildHud_.TrackAcquisitions(*playerManager_);
	}

	const GameMenuInputState menuInput = GameMenuController::Update(
		Engine::InputSystem::Input::GetInstance(),
		navigationInputDevice_);
	navigationInputDevice_ = menuInput.device;

	if (gameplayFlow_.Is(GameplayState::Start)) {
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
		} else if (action == PauseMenuAction::Restart) {
			if (sessionContext_) {
				sessionContext_->BeginNewRun();
			}
			RequestSceneChange(SceneId::kGame);
		} else if (action == PauseMenuAction::Exit) {
			RequestResultScene();
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

void PlayScene::DrawUi()
{
	if (!uiInitialized_) {
		return;
	}

	gameplayHud_.Draw(gameplayFlow_);
	if (gameplayFlow_.Is(GameplayState::Paused)) {
		pauseBuildHud_.Draw();
	} else if (gameplayFlow_.Is(GameplayState::LevelUp)) {
		levelUpSelectionHud_.Draw();
	}
}

void PlayScene::EnterPlaying()
{
	const bool enteringFromStart = gameplayFlow_.EnterPlaying();
	if (enteringFromStart && startSeHandle_) {
		GameAudioCache::PlayTuned(startSeHandle_, kAudioStart, 0.72f);
	}
	if (enteringFromStart && player_) {
		player_->SuppressNextDodgeTrigger();
	}
	playerDeathPresentation_.Reset();
	bossPresentation_.Reset();
}

void PlayScene::TogglePause()
{
	if (gameplayFlow_.BeginPause()) {
		pauseBuildHud_.Start();
		if (pauseSeHandle_) {
			GameAudioCache::PlayTuned(pauseSeHandle_, kAudioPauseToggle, 0.58f, 0.08f);
		}
	} else if (gameplayFlow_.Is(GameplayState::Paused)) {
		if (pauseSeHandle_) {
			GameAudioCache::PlayTuned(pauseSeHandle_, kAudioPauseToggle, 0.58f, 0.08f);
		}
		EnterPlaying();
	}
}

void PlayScene::StartBossPhase()
{
	if (!gameplayFlow_.BeginBossIntro()) {
		return;
	}
	if (bossPhaseSeHandle_) {
		GameAudioCache::PlayTuned(bossPhaseSeHandle_, kAudioBossPhase, 0.54f);
	}
	if (gameBgmHandle_) {
		GameAudioCache::StopBus(AudioBus::Bgm);
	}
	if (bossBgmHandle_) {
		GameAudioCache::SetVolumeFromTuning(bossBgmHandle_, kAudioBossBgm, 0.4f);
		GameAudioCache::PlayLoop(bossBgmHandle_);
		bossBgmPlaying_ = true;
	}
	if (!enemyManager_) {
		gameplayFlow_.EnterPlaying();
		return;
	}
	bossPresentation_.StartEntrance(*enemyManager_, player_.get());
}

void PlayScene::UpdateBossEntrance(float deltaTime)
{
	if (enemyManager_ &&
		bossPresentation_.UpdateEntrance(
			*enemyManager_,
			particleEffects_,
			deltaTime)) {
		if (player_) {
			Vector3 bossPosition{};
			if (enemyManager_->GetBossPresentationPosition(bossPosition)) {
				player_->SyncCameraToTarget(bossPosition);
			}
		}
		gameplayFlow_.EnterBoss();
	}
}

void PlayScene::StartBossDefeatPresentation()
{
	if (!gameplayFlow_.BeginBossDefeated()) {
		return;
	}
	if (bossBgmPlaying_ && bossBgmHandle_) {
		GameAudioCache::Stop(bossBgmHandle_);
		bossBgmPlaying_ = false;
	}
	if (bossDefeatSeHandle_) {
		GameAudioCache::PlayTuned(bossDefeatSeHandle_, kAudioBossDefeat, 0.78f);
	}

	if (enemyManager_) {
		bossPresentation_.StartDefeat(*enemyManager_);
	}
	RecordResultSummary();
}

void PlayScene::UpdateBossDefeatPresentation(float deltaTime)
{
	RecordResultSummary();
	if (enemyManager_ &&
		bossPresentation_.UpdateDefeat(
			*enemyManager_, particleEffects_, deltaTime)) {
		RequestResultScene();
	}
}

void PlayScene::RequestLevelUp()
{
	if (!playerManager_) {
		return;
	}
	playerManager_->ClearLevelUpRequest();
	levelUpSelectionHud_.Start(*playerManager_);
	gameplayFlow_.BeginLevelUp();
	SpawnLevelUpConfetti();
	if (levelUpSeHandle_) {
		GameAudioCache::PlayTuned(levelUpSeHandle_, kAudioLevelUp, 0.72f);
	}
}

void PlayScene::RequestSceneChange(const char* sceneId)
{
	GameAudioCache::StopBus(AudioBus::Bgm);
	sceneTransition_.Request(sceneId);
}

void PlayScene::RequestResultScene()
{
	RecordResultSummary();
	RequestSceneChange(SceneId::kResult);
}

void PlayScene::RecordResultSummary()
{
	if (!sessionContext_) {
		return;
	}

	sessionContext_->SetResultSummary(
		sessionContext_->GetGameFrameCount(),
		playerManager_ ? static_cast<uint32_t>(playerManager_->GetLevel()) : 1u + (sessionContext_->GetGameFrameCount() / 180u),
		enemyManager_ ? static_cast<uint32_t>(enemyManager_->GetTotalKillCount()) : sessionContext_->GetGameFrameCount() / 60u,
		playerManager_ ? playerManager_->GetTotalEXP() : 0,
		sessionContext_->GetRunCoins());
}

void PlayScene::SpawnLevelUpConfetti()
{
	if (!player_) {
		return;
	}
	levelUpSelectionHud_.SpawnConfetti(*player_, particleEffects_);
}

void PlayScene::QueueDebugDraw()
{
	DebugDraw::Queue(
		debugContext_.IsCollisionDrawEnabled(),
		debugContext_.IsLightDrawEnabled(),
		player_.get(),
		enemyManager_.get(),
		playerManager_.get());
}

void PlayScene::UpdateDebugUI()
{
	const DebugUIAction action =
		DebugUI::Update(
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
	if (action == DebugUIAction::RequestResult) {
		RequestResultScene();
	} else if (action == DebugUIAction::ForceBoss) {
		gameplayHud_.GetTimer().SetTime(kGameTimeLimitSeconds);
		StartBossPhase();
	} else if (action == DebugUIAction::BackToTitle) {
		RequestSceneChange(SceneId::kTitle);
	}
}

void PlayScene::ApplyPostEffect() const
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
