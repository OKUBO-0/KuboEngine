#include "game/directxgame/scene/DirectXGameScene.h"
#include "game/directxgame/core/DirectXGameDataPaths.h"
#include "game/directxgame/core/GameSpriteFactory.h"
#include "game/directxgame/core/DirectXGameSceneId.h"
#include "game/directxgame/core/DirectXGameSessionContext.h"
#include "game/directxgame/core/UILayoutIO.h"
#include "game/directxgame/effects/DirectXGameParticleBehaviors.h"
#include "CameraManager.h"
#include "Input.h"
#include "Line.h"
#include "LineCommon.h"
#include "Object3DCommon.h"
#include "OffscreenRenderManager.h"
#include "ParticleManager.h"
#include "SceneManager.h"
#include "SpriteCommon.h"
#include <algorithm>
#include <random>
#ifdef _DEBUG
#include <imgui.h>
#endif

namespace {

constexpr float kGameTimeLimitSeconds = 300.0f;
constexpr char kAudioStart[] = "game.start";
constexpr char kAudioPauseToggle[] = "game.pauseToggle";
constexpr char kAudioLevelUp[] = "game.levelUp";
constexpr char kAudioDeath[] = "game.death";
constexpr float kLevelUpSlideSpeed = 4200.0f;
constexpr float kLevelUpConfettiGravity = 980.0f;

bool IsConfirmTriggered()
{
	Engine::InputSystem::Input* input = Engine::InputSystem::Input::GetInstance();
	return input->TriggerKey(DIK_SPACE) ||
		input->TriggerKey(DIK_RETURN) ||
		input->TriggerGamePadButton(XINPUT_GAMEPAD_A);
}

bool IsCancelTriggered()
{
	Engine::InputSystem::Input* input = Engine::InputSystem::Input::GetInstance();
	return input->TriggerKey(DIK_ESCAPE) ||
		input->TriggerGamePadButton(XINPUT_GAMEPAD_B);
}

bool IsPauseTriggered()
{
	Engine::InputSystem::Input* input = Engine::InputSystem::Input::GetInstance();
	return input->TriggerKey(DIK_ESCAPE) ||
		input->TriggerGamePadButton(XINPUT_GAMEPAD_START);
}

bool IsMenuUpTriggered()
{
	Engine::InputSystem::Input* input = Engine::InputSystem::Input::GetInstance();
	return input->TriggerKey(DIK_W) ||
		input->TriggerKey(DIK_UP) ||
		input->TriggerGamePadButton(XINPUT_GAMEPAD_DPAD_UP);
}

bool IsMenuDownTriggered()
{
	Engine::InputSystem::Input* input = Engine::InputSystem::Input::GetInstance();
	return input->TriggerKey(DIK_S) ||
		input->TriggerKey(DIK_DOWN) ||
		input->TriggerGamePadButton(XINPUT_GAMEPAD_DPAD_DOWN);
}

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
	Engine::CameraSystem::CameraManager::GetInstance()->Initialize();

	if (sessionContext_) {
		sessionContext_->OnEnterGameScene();
	}

	InitializeLighting();
	InitializeWorld();
	InitializeUi();
	InitializeParticles();
	curtain_ = std::make_unique<CurtainTransition>();
	curtain_->Initialize();
	curtain_->StartOpen(20.0f);
	ApplyPostEffect();
}

void DirectXGameScene::Finalize()
{
	if (startSeHandle_ != 0) { GameAudioCache::Stop(startSeHandle_); }
	if (pauseSeHandle_ != 0) { GameAudioCache::Stop(pauseSeHandle_); }
	if (levelUpSeHandle_ != 0) { GameAudioCache::Stop(levelUpSeHandle_); }
	if (gameOverSeHandle_ != 0) { GameAudioCache::Stop(gameOverSeHandle_); }
	Engine::CameraSystem::CameraManager::GetInstance()->RemoveCamera("directxgame_player");
}

void DirectXGameScene::Update()
{
	constexpr float kFixedDeltaTime = 1.0f / 60.0f;

	navigationInputDevice_ = GameInputBindings::DetectNavigationInputDevice(
		Engine::InputSystem::Input::GetInstance(),
		navigationInputDevice_);

	UpdateUi(kFixedDeltaTime);
	UpdateCurtain(kFixedDeltaTime);

	if (!pendingSceneId_.empty()) {
		ApplyPostEffect();
		UpdateDebugUi();
		return;
	}

	if (gameState_ == GameState::Playing) {
		if (timer_.GetTime() >= kGameTimeLimitSeconds) {
			RequestResultScene();
			ApplyPostEffect();
			UpdateDebugUi();
			return;
		}
		UpdateGamePlay(kFixedDeltaTime);
	}
	UpdateEffects();

	if (gridPlane_ && player_) {
		gridPlane_->Update(player_->GetWorldPosition());
	}
	if (skyDome_) {
		skyDome_->Update();
	}

	if (sessionContext_ && gameState_ == GameState::Playing) {
		sessionContext_->AdvanceGameFrame();
	}

	if (gameState_ == GameState::Dead) {
		deathTimer_ += kFixedDeltaTime;
		RecordResultSummary();
		if (deathTimer_ >= 1.15f || IsConfirmTriggered()) {
			RequestResultScene();
			ApplyPostEffect();
			return;
		}
	}

	ApplyPostEffect();
	UpdateDebugUi();
	QueueDebugDraw();
	QueueEffectDraw();
}

void DirectXGameScene::Draw()
{
	Engine::Graphics3D::Object3DCommon::GetInstance()->CommonDraw();
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
	Engine::Particle::ParticleManager::GetInstance()->Draw();
	Engine::LineSystem::LineCommon::GetInstance()->Draw();

	Engine::Graphics2D::SpriteCommon::GetInstance()->CommonDraw();
	DrawUi();
	if (curtain_) {
		curtain_->Draw();
	}
}

void DirectXGameScene::InitializeLighting()
{
	DirectionalLight directional{};
	directional.color = { 1.05f, 1.0f, 0.95f, 1.0f };
	directional.direction = { -0.35f, -1.0f, -0.4f };
	directional.intensity = 1.0f;
	directional.enable = true;
	lightSettings_.SetDirectionalLight(directional);
	lightSettings_.SetLightingEnabled(true);
}

void DirectXGameScene::InitializeWorld()
{
	player_ = std::make_unique<Player>();
	player_->Initialize();
	player_->SetLightSettings(lightSettings_);

	playerManager_ = std::make_unique<PlayerManager>();
	playerManager_->Initialize(player_.get());
	playerManager_->SetLightSettings(lightSettings_);
	playerManager_->LoadStatusFromCSV(DataPaths::kPlayerStatus);
	playerManager_->LoadWeaponUpgradeSettings(DataPaths::kWeaponUpgradeSettings);

	enemyManager_ = std::make_unique<EnemyManager>();
	enemyManager_->SetLightSettings(lightSettings_);
	enemyManager_->Initialize(DataPaths::Resolve(DataPaths::kEnemyTypes), player_.get(), playerManager_.get());

	gridPlane_ = std::make_unique<GridPlane>();
	gridPlane_->Initialize();
	gridPlane_->SetLightSettings(lightSettings_);
	gridPlane_->Update(player_->GetWorldPosition());

	skyDome_ = std::make_unique<SkyDome>();
	skyDome_->Initialize();
	skyDome_->SetLightSettings(lightSettings_);
	skyDome_->Update();
}

void DirectXGameScene::InitializeUi()
{
	timer_.Initialize();
	hpGauge_.Initialize();
	expGauge_.Initialize();
	keyUI_.Initialize();
	miniMap_.Initialize();

	startOverlay_.Initialize("ui/game/start.png", { 0.0f, 0.0f });
	startOverlay_.SetSize({ 1280.0f, 720.0f });
	pauseOverlay_.Initialize("ui/game/pause.png", { 0.0f, 0.0f });
	pauseOverlay_.SetSize({ 1280.0f, 720.0f });
	pauseCursor_.Initialize("ui/game/pause_arrow.png", { 790.0f, 318.0f });
	pauseCursor_.SetSize({ 64.0f, 64.0f });
	levelUpOverlay_.Initialize("ui/game/levelup.png", { 0.0f, 0.0f });
	levelUpOverlay_.SetSize({ 1280.0f, 720.0f });
	hitFlashOverlay_.Initialize("white1x1.png", { 0.0f, 0.0f });
	hitFlashOverlay_.SetSize({ 1280.0f, 720.0f });
	hitFlashOverlay_.SetColor({ 1.0f, 0.12f, 0.08f, 1.0f });
	hitFlashOverlay_.SetAlpha(0.0f);
	hitFlashOverlay_.SetVisible(false);
	deathOverlay_.Initialize("ui/game/death.png", { 0.0f, 0.0f });
	deathOverlay_.SetSize({ 1280.0f, 720.0f });
	deathOverlay_.SetAlpha(0.0f);
	deathOverlay_.SetVisible(false);

	for (UILabel& choiceSprite : levelUpChoiceSprites_) {
		choiceSprite.Initialize("ui/game/lvup_attack.png", { 0.0f, 0.0f });
		choiceSprite.SetSize({ 1280.0f, 720.0f });
	}
	InitializePauseBuildUi();

	startSeHandle_ = GameAudioCache::LoadWave("audio/se/se_exp.wav");
	pauseSeHandle_ = GameAudioCache::LoadWave("audio/se/se_pause.wav");
	levelUpSeHandle_ = GameAudioCache::LoadWave("audio/se/se_exp.wav");
	gameOverSeHandle_ = GameAudioCache::LoadWave("audio/se/se_death.wav");

	hpGauge_.SetHP(playerManager_ ? playerManager_->GetHP() : 1, playerManager_ ? playerManager_->GetMaxHP() : 1);
	expGauge_.SetEXP(playerManager_ ? playerManager_->GetEXP() : 0, playerManager_ ? playerManager_->GetNextLevelEXP() : 1);
	expGauge_.SetLevel(playerManager_ ? playerManager_->GetLevel() : 1);
	previousHp_ = playerManager_ ? playerManager_->GetHP() : 0;
	previousEffectHp_ = previousHp_;
	previousEffectTotalExp_ = playerManager_ ? playerManager_->GetTotalEXP() : 0;
	uiInitialized_ = true;
}

void DirectXGameScene::InitializePauseBuildUi()
{
	const UILayoutIO::LayoutMap layout = UILayoutIO::LoadOrDefault(DataPaths::kPauseLayout, {});
	pauseBuildLayout_.position = UILayoutIO::GetVector2(layout, "pauseBuildPosition", pauseBuildLayout_.position);
	pauseBuildLayout_.stepX = UILayoutIO::GetFloat(layout, "pauseBuildStepX", pauseBuildLayout_.stepX);
	pauseBuildLayout_.iconSize = UILayoutIO::GetVector2(layout, "pauseBuildIconSize", pauseBuildLayout_.iconSize);
	pauseBuildLayout_.visible = UILayoutIO::GetFloat(layout, "pauseBuildVisible", pauseBuildLayout_.visible ? 1.0f : 0.0f) > 0.5f;

	const std::array<const char*, 5> iconPaths{
		"ui/game/normal/icon.png",
		"ui/game/orbit/icon.png",
		"ui/game/drone/icon.png",
		"ui/game/lightning/icon.png",
		"ui/game/lvup_attack_icon.png",
	};

	for (size_t index = 0; index < pauseBuildIcons_.size(); ++index) {
		pauseBuildIcons_[index].Initialize(iconPaths[index], {
			pauseBuildLayout_.position.x + pauseBuildLayout_.stepX * static_cast<float>(index),
			pauseBuildLayout_.position.y,
		});
		pauseBuildIcons_[index].SetSize(pauseBuildLayout_.iconSize);
		pauseBuildIcons_[index].SetVisible(false);
	}
}

void DirectXGameScene::InitializeParticles()
{
	Engine::Particle::ParticleManager* particleManager = Engine::Particle::ParticleManager::GetInstance();
	particleManager->CreateParticleGroup(
		"DirectXGame.Ripple",
		"Resources/DirectXGame/white1x1.png",
		Engine::Particle::VerticesType::Ring,
		std::make_unique<RippleParticleBehavior>());
	particleManager->SetBehavior("DirectXGame.Ripple", std::make_unique<RippleParticleBehavior>());
	particleManager->CreateParticleGroup(
		"DirectXGame.Spark",
		"Resources/DirectXGame/white1x1.png",
		Engine::Particle::VerticesType::Quad,
		std::make_unique<SparkParticleBehavior>());
	particleManager->SetBehavior("DirectXGame.Spark", std::make_unique<SparkParticleBehavior>());
}

void DirectXGameScene::UpdateGamePlay(float deltaTime)
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
	if (playerManager_ && playerManager_->IsLevelUpRequested()) {
		RequestLevelUp();
	}
	if (playerManager_ && playerManager_->IsDead()) {
		if (!gameOverSePlayed_ && gameOverSeHandle_ != 0) {
			GameAudioCache::Play(gameOverSeHandle_);
			GameAudioCache::SetVolumeFromTuning(gameOverSeHandle_, kAudioDeath, 1.0f);
			gameOverSePlayed_ = true;
		}
		gameState_ = GameState::Dead;
	}
}

void DirectXGameScene::UpdateEffects()
{
	if (!playerManager_ || !player_) {
		return;
	}

	Engine::Particle::ParticleManager* particleManager = Engine::Particle::ParticleManager::GetInstance();
	const Vector3 playerPosition = player_->GetWorldPosition();
	const int32_t hp = playerManager_->GetHP();
	if (hp < previousEffectHp_) {
		particleManager->Emit("DirectXGame.Spark", playerPosition, 18);
		particleManager->Emit("DirectXGame.Ripple", playerPosition, 1);
	}
	previousEffectHp_ = hp;

	const int32_t totalExp = playerManager_->GetTotalEXP();
	if (totalExp > previousEffectTotalExp_) {
		particleManager->Emit("DirectXGame.Ripple", playerPosition, 1);
	}
	previousEffectTotalExp_ = totalExp;

	const float lightningTimer = playerManager_->GetLightningEffectTimer();
	if (lightningTimer > previousLightningEffectTimer_) {
		for (const Vector3& target : playerManager_->GetLightningEffectTargets()) {
			particleManager->Emit("DirectXGame.Spark", target, 14);
			particleManager->Emit("DirectXGame.Ripple", target, 1);
		}
	}
	previousLightningEffectTimer_ = lightningTimer;
}

void DirectXGameScene::UpdateUi(float deltaTime)
{
	if (!uiInitialized_) {
		return;
	}

	if (playerManager_) {
		const int32_t currentHp = playerManager_->GetHP();
		if (currentHp < previousHp_) {
			hitFlashTimer_ = 0.28f;
		}
		previousHp_ = currentHp;
		hpGauge_.SetHP(playerManager_->GetHP(), playerManager_->GetMaxHP());
		expGauge_.SetEXP(playerManager_->GetEXP(), playerManager_->GetNextLevelEXP());
		expGauge_.SetLevel(playerManager_->GetLevel());
	}
	if (hitFlashTimer_ > 0.0f) {
		hitFlashTimer_ = (std::max)(0.0f, hitFlashTimer_ - deltaTime);
		const float alpha = 0.36f * Clamp01(hitFlashTimer_ / 0.28f);
		hitFlashOverlay_.SetVisible(true);
		hitFlashOverlay_.SetAlpha(alpha);
	} else {
		hitFlashOverlay_.SetVisible(false);
	}
	if (gameState_ == GameState::Dead) {
		deathOverlay_.SetVisible(true);
		deathOverlay_.SetAlpha(Clamp01(deathTimer_ / 0.45f));
	} else {
		deathOverlay_.SetVisible(false);
	}
	hpGauge_.Update();
	expGauge_.Update();
	if (gameState_ == GameState::Playing) {
		keyUI_.Update(Engine::InputSystem::Input::GetInstance());
	}
	if (gameState_ == GameState::Paused && player_ && enemyManager_) {
		miniMap_.Update(player_.get(), *enemyManager_);
		UpdatePauseBuildUi();
	}
	if (gameState_ == GameState::Playing) {
		timer_.Update(deltaTime);
	}

	if (gameState_ == GameState::Start) {
		if (IsConfirmTriggered()) {
			EnterPlaying();
		}
		if (IsCancelTriggered()) {
			RequestSceneChange(SceneId::kTitle);
		}
		return;
	}

	if (gameState_ == GameState::Paused) {
		if (IsMenuUpTriggered() || IsMenuDownTriggered()) {
			MoveMenuSelection(1);
		}
		pauseCursor_.SetPosition({ 790.0f, menuSelection_ == 0 ? 318.0f : 486.0f });
		if (IsConfirmTriggered()) {
			if (menuSelection_ == 0) {
				EnterPlaying();
			} else {
				RequestSceneChange(SceneId::kTitle);
			}
		}
		if (IsCancelTriggered()) {
			EnterPlaying();
		}
		return;
	}

	if (gameState_ == GameState::LevelUp) {
		UpdateLevelUpAnimation(deltaTime);
		UpdateLevelUpConfetti(deltaTime);
		if (levelUpAnimationState_ != LevelUpAnimationState::Idle) {
			return;
		}
		if (IsMenuUpTriggered()) {
			MoveMenuSelection(-1);
		}
		if (IsMenuDownTriggered()) {
			MoveMenuSelection(1);
		}
		for (size_t index = 0; index < levelUpChoiceSprites_.size(); ++index) {
			levelUpChoiceSprites_[index].SetColor(index == static_cast<size_t>(menuSelection_)
				? Vector4{ 1.15f, 1.15f, 0.75f, 1.0f }
				: Vector4{ 1.0f, 1.0f, 1.0f, 1.0f });
		}
		if (IsConfirmTriggered()) {
			levelUpSelectionPending_ = true;
			levelUpAnimationState_ = LevelUpAnimationState::Exiting;
		}
		return;
	}

	if (gameState_ == GameState::Playing && IsPauseTriggered()) {
		TogglePause();
	}
}

void DirectXGameScene::DrawUi()
{
	if (!uiInitialized_) {
		return;
	}

	timer_.Draw();
	hpGauge_.Draw();
	expGauge_.Draw();
	if (gameState_ == GameState::Playing) {
		keyUI_.Draw();
	}

	if (gameState_ == GameState::Start) {
		startOverlay_.Draw();
	} else if (gameState_ == GameState::Paused) {
		pauseOverlay_.Draw();
		miniMap_.Draw();
		DrawPauseBuildUi();
		pauseCursor_.Draw();
	} else if (gameState_ == GameState::LevelUp) {
		levelUpOverlay_.Draw();
		DrawLevelUpConfetti();
		for (UILabel& choiceSprite : levelUpChoiceSprites_) {
			choiceSprite.Draw();
		}
	}
	hitFlashOverlay_.Draw();
	deathOverlay_.Draw();
}

void DirectXGameScene::UpdatePauseBuildUi()
{
	if (!playerManager_) {
		return;
	}

	const bool showBuildIcons = pauseBuildLayout_.visible && gameState_ == GameState::Paused;
	const std::array<float, 5> alphas{
		1.0f,
		playerManager_->HasOrbitBullets() ? 1.0f : 0.25f,
		playerManager_->HasDrone() ? 1.0f : 0.25f,
		playerManager_->HasLightning() ? 1.0f : 0.25f,
		playerManager_->GetAttackPower() > 1 ? 1.0f : 0.4f,
	};

	for (size_t index = 0; index < pauseBuildIcons_.size(); ++index) {
		UILabel& icon = pauseBuildIcons_[index];
		icon.SetPosition({
			pauseBuildLayout_.position.x + pauseBuildLayout_.stepX * static_cast<float>(index),
			pauseBuildLayout_.position.y,
		});
		icon.SetSize(pauseBuildLayout_.iconSize);
		icon.SetVisible(showBuildIcons);
		icon.SetColor({ 1.0f, 1.0f, 1.0f, alphas[index] });
	}
}

void DirectXGameScene::UpdateLevelUpAnimation(float deltaTime)
{
	switch (levelUpAnimationState_) {
	case LevelUpAnimationState::Entering:
		levelUpSlideOffsetX_ = (std::max)(0.0f, levelUpSlideOffsetX_ - kLevelUpSlideSpeed * deltaTime);
		if (levelUpSlideOffsetX_ <= 0.0f) {
			levelUpSlideOffsetX_ = 0.0f;
			levelUpAnimationState_ = LevelUpAnimationState::Idle;
		}
		ApplyLevelUpLayout();
		break;
	case LevelUpAnimationState::Exiting:
		levelUpSlideOffsetX_ -= kLevelUpSlideSpeed * deltaTime;
		ApplyLevelUpLayout();
		if (levelUpSlideOffsetX_ <= -1280.0f) {
			if (levelUpSelectionPending_) {
				ApplySelectedLevelUpChoice();
				levelUpSelectionPending_ = false;
			}
			levelUpAnimationState_ = LevelUpAnimationState::Hidden;
			levelUpConfetti_.clear();
			EnterPlaying();
		}
		break;
	case LevelUpAnimationState::Idle:
	case LevelUpAnimationState::Hidden:
	default:
		break;
	}
}

void DirectXGameScene::UpdateLevelUpConfetti(float deltaTime)
{
	for (auto it = levelUpConfetti_.begin(); it != levelUpConfetti_.end();) {
		it->age += deltaTime;
		if (it->age >= it->lifetime) {
			it = levelUpConfetti_.erase(it);
			continue;
		}

		it->velocity.y += kLevelUpConfettiGravity * deltaTime;
		it->position.x += it->velocity.x * deltaTime;
		it->position.y += it->velocity.y * deltaTime;
		it->rotation += it->angularVelocity * deltaTime;

		if (it->sprite) {
			const float alpha = 1.0f - (it->age / it->lifetime);
			Vector4 color = it->color;
			color.w *= alpha;
			it->sprite->SetColor(color);
			it->sprite->SetPosition(it->position);
			it->sprite->SetRotation(it->rotation);
			it->sprite->Update();
		}

		++it;
	}
}

void DirectXGameScene::DrawPauseBuildUi()
{
	if (!pauseBuildLayout_.visible) {
		return;
	}

	for (UILabel& icon : pauseBuildIcons_) {
		icon.Draw();
	}
}

void DirectXGameScene::DrawLevelUpConfetti()
{
	for (ConfettiParticle& particle : levelUpConfetti_) {
		if (particle.sprite) {
			particle.sprite->Draw();
		}
	}
}

void DirectXGameScene::SavePauseBuildLayout() const
{
	UILayoutIO::Save(DataPaths::kPauseLayout,
		{
			{ "pauseBuildPosition", { pauseBuildLayout_.position.x, pauseBuildLayout_.position.y } },
			{ "pauseBuildStepX", { pauseBuildLayout_.stepX } },
			{ "pauseBuildIconSize", { pauseBuildLayout_.iconSize.x, pauseBuildLayout_.iconSize.y } },
			{ "pauseBuildVisible", { pauseBuildLayout_.visible ? 1.0f : 0.0f } },
		});
}

void DirectXGameScene::EnterPlaying()
{
	if (gameState_ == GameState::Start && startSeHandle_ != 0) {
		GameAudioCache::Play(startSeHandle_);
		GameAudioCache::SetVolumeFromTuning(startSeHandle_, kAudioStart, 1.0f);
	}
	gameState_ = GameState::Playing;
	deathTimer_ = 0.0f;
	menuSelection_ = 0;
}

void DirectXGameScene::TogglePause()
{
	if (gameState_ == GameState::Playing) {
		gameState_ = GameState::Paused;
		menuSelection_ = 0;
		if (pauseSeHandle_ != 0) {
			GameAudioCache::Play(pauseSeHandle_);
			GameAudioCache::SetVolumeFromTuning(pauseSeHandle_, kAudioPauseToggle, 0.5f);
		}
	} else if (gameState_ == GameState::Paused) {
		if (pauseSeHandle_ != 0) {
			GameAudioCache::Play(pauseSeHandle_);
			GameAudioCache::SetVolumeFromTuning(pauseSeHandle_, kAudioPauseToggle, 0.5f);
		}
		EnterPlaying();
	}
}

void DirectXGameScene::RequestLevelUp()
{
	if (!playerManager_) {
		return;
	}
	playerManager_->ClearLevelUpRequest();
	BuildLevelUpChoices();
	gameState_ = GameState::LevelUp;
	menuSelection_ = 0;
	levelUpSlideOffsetX_ = 1280.0f;
	levelUpAnimationState_ = LevelUpAnimationState::Entering;
	levelUpSelectionPending_ = false;
	SpawnLevelUpConfetti();
	ApplyLevelUpLayout();
	if (levelUpSeHandle_ != 0) {
		GameAudioCache::Play(levelUpSeHandle_);
		GameAudioCache::SetVolumeFromTuning(levelUpSeHandle_, kAudioLevelUp, 1.0f);
	}
}

void DirectXGameScene::RequestSceneChange(const char* sceneId)
{
	if (!pendingSceneId_.empty()) {
		return;
	}
	pendingSceneId_ = sceneId;
	if (curtain_) {
		curtain_->StartClose(24.0f);
	}
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

void DirectXGameScene::UpdateCurtain(float deltaTime)
{
	if (!curtain_) {
		return;
	}
	curtain_->Update(deltaTime);
	if (!pendingSceneId_.empty() && curtain_->IsFinished()) {
		Engine::Scene::SceneManager::GetInstance()->ChangeScene(pendingSceneId_);
	}
}

void DirectXGameScene::BuildLevelUpChoices()
{
	levelUpChoices_.clear();
	if (!playerManager_) {
		return;
	}

	if (!playerManager_->IsNormalBulletMaxLevel()) {
		levelUpChoices_.push_back({ UpgradeType::Normal, "ui/game/normal/choice.png" });
	}
	if (!playerManager_->IsOrbitBulletMaxLevel()) {
		levelUpChoices_.push_back({ UpgradeType::Orbit, playerManager_->HasOrbitBullets() ? "ui/game/orbit/upgrade.png" : "ui/game/orbit/add.png" });
	}
	if (!playerManager_->IsDroneMaxLevel()) {
		levelUpChoices_.push_back({ UpgradeType::Drone, playerManager_->HasDrone() ? "ui/game/drone/upgrade.png" : "ui/game/drone/add.png" });
	}
	if (!playerManager_->IsLightningMaxLevel()) {
		levelUpChoices_.push_back({ UpgradeType::Lightning, playerManager_->HasLightning() ? "ui/game/lightning/upgrade.png" : "ui/game/lightning/add.png" });
	}
	levelUpChoices_.push_back({ UpgradeType::Attack, "ui/game/lvup_attack.png" });
	levelUpChoices_.push_back({ UpgradeType::MaxHP, "ui/game/lvup_maxhp.png" });
	levelUpChoices_.push_back({ UpgradeType::MoveSpeed, "ui/game/lvup_speed.png" });
	if (playerManager_->GetHP() < playerManager_->GetMaxHP()) {
		levelUpChoices_.push_back({ UpgradeType::Heal, "ui/game/lvup_heal.png" });
	}

	static std::mt19937 rng{ std::random_device{}() };
	std::shuffle(levelUpChoices_.begin(), levelUpChoices_.end(), rng);
	if (levelUpChoices_.size() > levelUpChoiceSprites_.size()) {
		levelUpChoices_.resize(levelUpChoiceSprites_.size());
	}

	for (size_t index = 0; index < levelUpChoiceSprites_.size(); ++index) {
		const std::string path = index < levelUpChoices_.size() ? levelUpChoices_[index].texturePath : "ui/game/lvup_attack.png";
		levelUpChoiceSprites_[index].Initialize(path, { 0.0f, 140.0f * static_cast<float>(index) });
		levelUpChoiceSprites_[index].SetSize({ 1280.0f, 720.0f });
	}
	ApplyLevelUpLayout();
}

void DirectXGameScene::ApplySelectedLevelUpChoice()
{
	if (!playerManager_ || levelUpChoices_.empty()) {
		return;
	}

	const LevelUpChoice& choice = levelUpChoices_[static_cast<size_t>(std::clamp(menuSelection_, 0, static_cast<int32_t>(levelUpChoices_.size()) - 1))];
	switch (choice.type) {
	case UpgradeType::Normal:
		playerManager_->UpgradeNormalBullets();
		break;
	case UpgradeType::Orbit:
		playerManager_->UpgradeOrbitBullets();
		break;
	case UpgradeType::Drone:
		playerManager_->UpgradeDrone();
		break;
	case UpgradeType::Lightning:
		playerManager_->UpgradeLightning();
		break;
	case UpgradeType::Attack:
		playerManager_->UpgradeAttackPower();
		break;
	case UpgradeType::MaxHP:
		playerManager_->IncreaseMaxHP();
		break;
	case UpgradeType::MoveSpeed:
		playerManager_->UpgradeMoveSpeed();
		break;
	case UpgradeType::Heal:
		playerManager_->RecoverHP();
		break;
	}
}

void DirectXGameScene::ApplyLevelUpLayout()
{
	levelUpOverlay_.SetPosition({ levelUpSlideOffsetX_, 0.0f });
	for (size_t index = 0; index < levelUpChoiceSprites_.size(); ++index) {
		levelUpChoiceSprites_[index].SetPosition({
			levelUpSlideOffsetX_,
			140.0f * static_cast<float>(index),
		});
		levelUpChoiceSprites_[index].SetSize({ 1280.0f, 720.0f });
	}
}

void DirectXGameScene::SpawnLevelUpConfetti()
{
	levelUpConfetti_.clear();

	static std::mt19937 rng{ std::random_device{}() };
	std::uniform_real_distribution<float> spawnXDist(120.0f, 1160.0f);
	std::uniform_real_distribution<float> velocityXDist(-180.0f, 180.0f);
	std::uniform_real_distribution<float> velocityYDist(-980.0f, -520.0f);
	std::uniform_real_distribution<float> sizeXDist(8.0f, 18.0f);
	std::uniform_real_distribution<float> sizeYDist(14.0f, 30.0f);
	std::uniform_real_distribution<float> rotationDist(0.0f, 6.28318f);
	std::uniform_real_distribution<float> angularVelocityDist(-7.0f, 7.0f);
	std::uniform_real_distribution<float> lifetimeDist(0.8f, 1.5f);
	std::uniform_int_distribution<int> colorIndexDist(0, 5);

	constexpr std::array<Vector4, 6> kColors{
		Vector4{ 1.0f, 0.35f, 0.35f, 1.0f },
		Vector4{ 1.0f, 0.82f, 0.22f, 1.0f },
		Vector4{ 0.35f, 0.86f, 0.52f, 1.0f },
		Vector4{ 0.30f, 0.72f, 1.0f, 1.0f },
		Vector4{ 0.98f, 0.52f, 0.88f, 1.0f },
		Vector4{ 1.0f, 1.0f, 1.0f, 1.0f },
	};

	constexpr int32_t kParticleCount = 28;
	levelUpConfetti_.reserve(kParticleCount);
	for (int32_t index = 0; index < kParticleCount; ++index) {
		ConfettiParticle particle;
		particle.position = { spawnXDist(rng), 744.0f };
		particle.velocity = { velocityXDist(rng), velocityYDist(rng) };
		particle.size = { sizeXDist(rng), sizeYDist(rng) };
		particle.rotation = rotationDist(rng);
		particle.angularVelocity = angularVelocityDist(rng);
		particle.lifetime = lifetimeDist(rng);
		particle.color = kColors[static_cast<size_t>(colorIndexDist(rng))];
		particle.sprite = GameSpriteFactory::Create("white1x1.png", particle.position);
		particle.sprite->SetSize(particle.size);
		particle.sprite->SetAnchorPoint({ 0.5f, 0.5f });
		particle.sprite->SetRotation(particle.rotation);
		particle.sprite->SetColor(particle.color);
		levelUpConfetti_.push_back(std::move(particle));
	}
}

void DirectXGameScene::MoveMenuSelection(int32_t delta)
{
	const int32_t maxSelection = gameState_ == GameState::Paused ? 1 : static_cast<int32_t>(levelUpChoices_.empty() ? 0 : levelUpChoices_.size() - 1);
	menuSelection_ += delta;
	if (menuSelection_ < 0) {
		menuSelection_ = maxSelection;
	} else if (menuSelection_ > maxSelection) {
		menuSelection_ = 0;
	}
}

void DirectXGameScene::QueueDebugDraw()
{
	if (!debugDrawEnabled_ || !player_) {
		return;
	}

	Engine::LineSystem::Line line;
	const Vector3 playerPosition = player_->GetWorldPosition();
	line.DrawSphere(playerPosition, 1.35f, { 0.25f, 0.95f, 1.0f, 1.0f });
	line.DrawSphere(playerPosition, 50.0f, { 0.25f, 0.55f, 1.0f, 0.45f });

	if (enemyManager_) {
		for (const std::unique_ptr<Enemy>& enemy : enemyManager_->GetEnemies()) {
			if (!enemy || !enemy->IsActive()) {
				continue;
			}
			line.DrawSphere(enemy->GetPosition(), 1.55f, { 1.0f, 0.2f, 0.18f, 1.0f });
		}
	}
}

void DirectXGameScene::QueueEffectDraw()
{
	if (!player_ || !playerManager_) {
		return;
	}

	Engine::LineSystem::Line line;
	const Vector3 playerPosition = player_->GetWorldPosition();
	const float lightningTimer = playerManager_->GetLightningEffectTimer();
	if (lightningTimer > 0.0f) {
		const float alpha = Clamp01(lightningTimer / 0.22f);
		for (const Vector3& target : playerManager_->GetLightningEffectTargets()) {
			const Vector3 skyStart{ target.x, target.y + 18.0f, target.z };
			const Vector3 targetTop{ target.x, target.y + 1.8f, target.z };
			line.Draw(skyStart, targetTop, { 0.55f, 0.85f, 1.0f, alpha });
			line.Draw({ target.x - playerManager_->GetLightningRadius(), target.y + 0.12f, target.z },
				{ target.x + playerManager_->GetLightningRadius(), target.y + 0.12f, target.z },
				{ 0.45f, 0.7f, 1.0f, alpha * 0.55f });
			line.Draw({ target.x, target.y + 0.12f, target.z - playerManager_->GetLightningRadius() },
				{ target.x, target.y + 0.12f, target.z + playerManager_->GetLightningRadius() },
				{ 0.45f, 0.7f, 1.0f, alpha * 0.55f });
		}
	}

	if (enemyManager_) {
		for (const std::unique_ptr<ExpOrb>& orb : enemyManager_->GetExpOrbs()) {
			if (!orb || !orb->IsActive()) {
				continue;
			}
			const Vector3 orbPosition = orb->GetPosition();
			const float dx = orbPosition.x - playerPosition.x;
			const float dz = orbPosition.z - playerPosition.z;
			const float distanceSq = dx * dx + dz * dz;
			if (distanceSq <= 144.0f) {
				line.Draw({ playerPosition.x, playerPosition.y + 1.0f, playerPosition.z },
					{ orbPosition.x, orbPosition.y + 0.5f, orbPosition.z },
					{ 0.35f, 1.0f, 0.65f, 0.45f });
			}
		}
	}
}

void DirectXGameScene::UpdateDebugUi()
{
#ifdef _DEBUG
	Engine::InputSystem::Input* input = Engine::InputSystem::Input::GetInstance();
	if (input->TriggerKey(DIK_F6) && playerManager_) {
		playerManager_->ForceDebugDeath();
	}
	if (input->TriggerKey(DIK_F8) && playerManager_) {
		playerManager_->AddEXP(10);
	}
	if (input->TriggerKey(DIK_F9) && playerManager_) {
		playerManager_->MaxAllWeapons();
	}
	if (input->TriggerKey(DIK_F10)) {
		RequestResultScene();
		return;
	}
	if (input->TriggerKey(DIK_F11)) {
		timer_.SetTime(kGameTimeLimitSeconds);
		RequestResultScene();
		return;
	}

	ImGui::Begin("DirectXGame Game");
	ImGui::Text("Stage 14 visual / debug draw scene");
	ImGui::Text("Input Device: %s", GameInputBindings::ToDisplayName(navigationInputDevice_));
	ImGui::Text("WASD / Left Stick: Move");
	ImGui::Text("Mouse / Right Stick: Aim");
	ImGui::Text("Confirm: %s", GameInputBindings::GetConfirmLabel(navigationInputDevice_));
	ImGui::Text("Pause: %s", GameInputBindings::GetPauseLabel(navigationInputDevice_));
	ImGui::Text("Cancel / Title on Start: %s", GameInputBindings::GetCancelLabel(navigationInputDevice_));
	ImGui::Text("Debug: F6 Death / F8 EXP / F9 Max Weapons / F10 Result / F11 Time-up Result");
	float masterVolume = GameAudioCache::GetMasterVolume();
	if (ImGui::SliderFloat("Master Volume", &masterVolume, 0.0f, 1.0f)) {
		GameAudioCache::SetMasterVolume(masterVolume);
	}
	if (ImGui::CollapsingHeader("Audio Balance")) {
		auto tuneVolume = [](const char* label, const char* key, float fallback) {
			float volume = GameAudioCache::GetTunedVolume(key, fallback);
			if (ImGui::SliderFloat(label, &volume, 0.0f, 1.0f)) {
				GameAudioCache::SetTunedVolume(key, volume);
			}
		};
		tuneVolume("Title BGM", "title.bgm", 0.1f);
		tuneVolume("Title Select", "title.select", 1.0f);
		tuneVolume("Title Decide", "title.decide", 1.0f);
		tuneVolume("Game Start", kAudioStart, 1.0f);
		tuneVolume("Pause Toggle", kAudioPauseToggle, 0.5f);
		tuneVolume("Level Up", kAudioLevelUp, 1.0f);
		tuneVolume("Player Death", kAudioDeath, 1.0f);
		tuneVolume("Shot", "combat.shot", 1.0f);
		tuneVolume("Enemy Hit", "combat.enemyHit", 0.5f);
		tuneVolume("Enemy Death", "combat.enemyDeath", 1.0f);
		tuneVolume("Player Damage", "combat.playerDamage", 0.8f);
		tuneVolume("EXP Pickup", "combat.expPickup", 1.0f);
		tuneVolume("Result Finish", "result.finish", 1.0f);
	}
	if (sessionContext_) {
		ImGui::Separator();
		ImGui::Text("Run Count: %u", sessionContext_->GetRunCount());
		ImGui::Text("Frame Count: %u", sessionContext_->GetGameFrameCount());
	}
	if (player_) {
		const Vector3& position = player_->GetWorldPosition();
		ImGui::Separator();
		ImGui::Text("Player Position: %.2f, %.2f, %.2f", position.x, position.y, position.z);
		ImGui::Text("Player Rotation Y: %.2f", player_->GetWorldRotationY());
		float moveSpeed = player_->GetMoveSpeed();
		if (ImGui::DragFloat("Move Speed", &moveSpeed, 0.1f, 1.0f, 120.0f)) {
			player_->SetMoveSpeed(moveSpeed);
		}
		bool mouseAimEnabled = player_->IsMouseAimEnabled();
		if (ImGui::Checkbox("Mouse Aim", &mouseAimEnabled)) {
			player_->SetMouseAimEnabled(mouseAimEnabled);
		}
		float cameraHeight = player_->GetCameraHeight();
		if (ImGui::DragFloat("Camera Height", &cameraHeight, 0.5f, 20.0f, 160.0f)) {
			player_->SetCameraHeight(cameraHeight);
		}
		float cameraDistance = player_->GetCameraDistance();
		if (ImGui::DragFloat("Camera Distance", &cameraDistance, 0.5f, 10.0f, 120.0f)) {
			player_->SetCameraDistance(cameraDistance);
		}
		float cameraPitch = player_->GetCameraPitch();
		if (ImGui::DragFloat("Camera Pitch", &cameraPitch, 0.01f, 0.2f, 1.5f)) {
			player_->SetCameraPitch(cameraPitch);
		}
		constexpr const char* kCameraModeLabels[] = {
			"World Back",
			"Player Back",
			"World Front",
			"Top Down",
		};
		int32_t cameraMode = static_cast<int32_t>(player_->GetCameraMode());
		if (ImGui::Combo("Camera Mode", &cameraMode, kCameraModeLabels, static_cast<int32_t>(std::size(kCameraModeLabels)))) {
			player_->SetCameraMode(static_cast<Player::CameraMode>(cameraMode));
		}
	}
	if (playerManager_) {
		ImGui::Separator();
		ImGui::Text("HP: %d / %d", playerManager_->GetHP(), playerManager_->GetMaxHP());
		ImGui::Text("EXP: %d / %d", playerManager_->GetEXP(), playerManager_->GetNextLevelEXP());
		ImGui::Text("Level: %d", playerManager_->GetLevel());
		ImGui::Text("Normal Lv: %d  Active: %zu", playerManager_->GetNormalBulletLevel(), playerManager_->GetNormalBullets().size());
		ImGui::Text("Orbit Lv: %d  Active: %zu", playerManager_->GetOrbitBulletLevel(), playerManager_->GetOrbitBullets().size());
		ImGui::Text("Drone Lv: %d  Enabled: %s", playerManager_->GetDroneLevel(), playerManager_->HasDrone() ? "true" : "false");
		ImGui::Text("Lightning Lv: %d  Enabled: %s", playerManager_->GetLightningLevel(), playerManager_->HasLightning() ? "true" : "false");
		if (ImGui::Button("Add EXP +10")) {
			playerManager_->AddEXP(10);
		}
		ImGui::SameLine();
		if (ImGui::Button("Normal Up")) {
			playerManager_->UpgradeNormalBullets();
		}
		ImGui::SameLine();
		if (ImGui::Button("Orbit Up")) {
			playerManager_->UpgradeOrbitBullets();
		}
		if (ImGui::Button("Drone Up")) {
			playerManager_->UpgradeDrone();
		}
		ImGui::SameLine();
		if (ImGui::Button("Lightning Up")) {
			playerManager_->UpgradeLightning();
		}
		ImGui::SameLine();
		if (ImGui::Button("Max Weapons")) {
			playerManager_->MaxAllWeapons();
		}
	}
	if (uiInitialized_) {
		ImGui::Separator();
		ImGui::Checkbox("DebugDraw collision / spawn range", &debugDrawEnabled_);
		ImGui::Text("Hit Flash: %.2f  Death Timer: %.2f", hitFlashTimer_, deathTimer_);
		timer_.DebugDrawImGui();
		hpGauge_.DebugDrawImGui();
		expGauge_.DebugDrawImGui();
		keyUI_.DebugDrawImGui();
		miniMap_.DebugDrawImGui();
		if (ImGui::CollapsingHeader("Pause Build Icons")) {
			ImGui::Checkbox("Show Pause Build Icons", &pauseBuildLayout_.visible);
			ImGui::Checkbox("Enable HUD Debug##PauseBuildIcons", &pauseBuildLayout_.debugEnabled);
			if (pauseBuildLayout_.debugEnabled) {
				float position[2]{ pauseBuildLayout_.position.x, pauseBuildLayout_.position.y };
				if (ImGui::DragFloat2("Pause Build Position", position, 1.0f, -400.0f, 1280.0f)) {
					pauseBuildLayout_.position = { position[0], position[1] };
					UpdatePauseBuildUi();
				}
				if (ImGui::DragFloat("Pause Build Spacing", &pauseBuildLayout_.stepX, 1.0f, 32.0f, 240.0f)) {
					UpdatePauseBuildUi();
				}
				float iconSize[2]{ pauseBuildLayout_.iconSize.x, pauseBuildLayout_.iconSize.y };
				if (ImGui::DragFloat2("Pause Build Icon Size", iconSize, 1.0f, 16.0f, 256.0f)) {
					pauseBuildLayout_.iconSize = { (std::max)(16.0f, iconSize[0]), (std::max)(16.0f, iconSize[1]) };
					UpdatePauseBuildUi();
				}
				if (ImGui::Button("Save Pause Build Layout")) {
					SavePauseBuildLayout();
				}
			}
		}
	}
	if (enemyManager_) {
		ImGui::Separator();
		ImGui::Text("Enemies: %zu / %zu", enemyManager_->GetActiveEnemyCount(), enemyManager_->GetEnemies().size());
		ImGui::Text("EXP Orbs: %zu", enemyManager_->GetExpOrbCount());
		ImGui::Text("Kills: %d", enemyManager_->GetTotalKillCount());
		if (ImGui::Button("Debug Damage All Enemies")) {
			enemyManager_->DamageAllEnemies(999);
		}
	}
	if (ImGui::Button("Go To Result")) {
		RequestResultScene();
		ImGui::End();
		return;
	}
	ImGui::SameLine();
	if (ImGui::Button("Force Time Up")) {
		timer_.SetTime(kGameTimeLimitSeconds);
		RequestResultScene();
		ImGui::End();
		return;
	}
	ImGui::SameLine();
	if (ImGui::Button("Back To Title")) {
		Engine::Scene::SceneManager::GetInstance()->ChangeScene(SceneId::kTitle);
		ImGui::End();
		return;
	}
	ImGui::End();
#endif
}

void DirectXGameScene::ApplyPostEffect() const
{
	Engine::Base::OffscreenRenderManager* offscreen = Engine::Base::OffscreenRenderManager::GetInstance();
	if (!offscreen) {
		return;
	}

	PostEffectType effect = PostEffectType::Fullscreen;
	switch (gameState_) {
	case GameState::Paused:
		effect = PostEffectType::Vignette;
		break;
	case GameState::LevelUp:
		effect = PostEffectType::RadialBlur;
		break;
	case GameState::Dead:
		effect = PostEffectType::Grayscale;
		break;
	case GameState::Start:
	case GameState::Playing:
	default:
		effect = PostEffectType::Fullscreen;
		break;
	}
	offscreen->SetScenePostEffectType(effect);
}

}
