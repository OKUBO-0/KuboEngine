#include "game/directxgame/scene/DirectXGameScene.h"
#include "game/directxgame/core/DirectXGameDataPaths.h"
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
#include <cmath>
#include <optional>
#include <random>
#include <string>
#include <utility>
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
constexpr Vector2 kLevelUpChoiceSize{ 1280.0f, 720.0f };

constexpr std::array<std::pair<const char*, float>, 13> kAudioTuningDefaults{ {
	{ "title.bgm", 0.1f },
	{ "title.select", 1.0f },
	{ "title.decide", 1.0f },
	{ kAudioStart, 1.0f },
	{ kAudioPauseToggle, 0.5f },
	{ kAudioLevelUp, 1.0f },
	{ kAudioDeath, 1.0f },
	{ "combat.shot", 1.0f },
	{ "combat.enemyHit", 0.5f },
	{ "combat.enemyDeath", 1.0f },
	{ "combat.playerDamage", 0.8f },
	{ "combat.expPickup", 1.0f },
	{ "result.finish", 1.0f },
} };

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

std::string WeaponLevelTexturePath(const char* weaponDirectory, int32_t nextLevel)
{
	return std::string("ui/game/") + weaponDirectory + "/lv" + std::to_string(nextLevel) + ".png";
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
	LoadDebugTuning();
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
		choiceSprite.SetSize(kLevelUpChoiceSize);
	}
	for (UILabel& choiceIcon : levelUpChoiceIcons_) {
		choiceIcon.Initialize("ui/game/lvup_attack_icon.png", { 0.0f, 0.0f });
		choiceIcon.SetSize(kLevelUpChoiceSize);
		choiceIcon.SetVisible(false);
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
	particleManager->CreateParticleGroup(
		"DirectXGame.EnemyHitSpark",
		"Resources/DirectXGame/white1x1.png",
		Engine::Particle::VerticesType::Quad,
		std::make_unique<SparkParticleBehavior>(Vector4{ 1.0f, 0.62f, 0.18f, 1.0f }));
	particleManager->SetBehavior("DirectXGame.EnemyHitSpark", std::make_unique<SparkParticleBehavior>(Vector4{ 1.0f, 0.62f, 0.18f, 1.0f }));
	particleManager->CreateParticleGroup(
		"DirectXGame.ExpSpark",
		"Resources/DirectXGame/white1x1.png",
		Engine::Particle::VerticesType::Quad,
		std::make_unique<SparkParticleBehavior>(Vector4{ 0.35f, 1.0f, 0.58f, 1.0f }));
	particleManager->SetBehavior("DirectXGame.ExpSpark", std::make_unique<SparkParticleBehavior>(Vector4{ 0.35f, 1.0f, 0.58f, 1.0f }));
	particleManager->CreateParticleGroup(
		"DirectXGame.LightningSpark",
		"Resources/DirectXGame/white1x1.png",
		Engine::Particle::VerticesType::Quad,
		std::make_unique<SparkParticleBehavior>(Vector4{ 0.48f, 0.84f, 1.0f, 1.0f }));
	particleManager->SetBehavior("DirectXGame.LightningSpark", std::make_unique<SparkParticleBehavior>(Vector4{ 0.48f, 0.84f, 1.0f, 1.0f }));
	particleManager->CreateParticleGroup(
		"DirectXGame.PlayerDeathSpark",
		"Resources/DirectXGame/white1x1.png",
		Engine::Particle::VerticesType::Quad,
		std::make_unique<SparkParticleBehavior>(Vector4{ 1.0f, 0.18f, 0.12f, 1.0f }));
	particleManager->SetBehavior("DirectXGame.PlayerDeathSpark", std::make_unique<SparkParticleBehavior>(Vector4{ 1.0f, 0.18f, 0.12f, 1.0f }));
	particleManager->CreateParticleGroup(
		"DirectXGame.DeathSmoke",
		"Resources/DirectXGame/white1x1.png",
		Engine::Particle::VerticesType::Quad,
		std::make_unique<SmokeParticleBehavior>());
	particleManager->SetBehavior("DirectXGame.DeathSmoke", std::make_unique<SmokeParticleBehavior>());
	particleManager->CreateParticleGroup(
		"DirectXGame.Confetti",
		"Resources/DirectXGame/white1x1.png",
		Engine::Particle::VerticesType::Quad,
		std::make_unique<ConfettiParticleBehavior>());
	particleManager->SetBehavior("DirectXGame.Confetti", std::make_unique<ConfettiParticleBehavior>());
	ApplyParticleBehaviorTuning();
}

void DirectXGameScene::LoadDebugTuning()
{
	const UILayoutIO::LayoutMap tuning = UILayoutIO::LoadOrDefault(DataPaths::kDebugTuning, {});

	GameAudioCache::SetMasterVolume(UILayoutIO::GetFloat(tuning, "audio.master", GameAudioCache::GetMasterVolume()));
	for (const auto& [key, fallback] : kAudioTuningDefaults) {
		GameAudioCache::SetTunedVolume(key, UILayoutIO::GetFloat(tuning, std::string("audio.") + key, fallback));
	}

	particleTuning_.playerDamageSparkCount = static_cast<int32_t>(UILayoutIO::GetFloat(tuning, "particle.playerDamageSparkCount", static_cast<float>(particleTuning_.playerDamageSparkCount)));
	particleTuning_.playerDamageRippleCount = static_cast<int32_t>(UILayoutIO::GetFloat(tuning, "particle.playerDamageRippleCount", static_cast<float>(particleTuning_.playerDamageRippleCount)));
	particleTuning_.enemyHitSparkCount = static_cast<int32_t>(UILayoutIO::GetFloat(tuning, "particle.enemyHitSparkCount", static_cast<float>(particleTuning_.enemyHitSparkCount)));
	particleTuning_.enemyDeathSparkCount = static_cast<int32_t>(UILayoutIO::GetFloat(tuning, "particle.enemyDeathSparkCount", static_cast<float>(particleTuning_.enemyDeathSparkCount)));
	particleTuning_.enemyDeathSmokeCount = static_cast<int32_t>(UILayoutIO::GetFloat(tuning, "particle.enemyDeathSmokeCount", static_cast<float>(particleTuning_.enemyDeathSmokeCount)));
	particleTuning_.enemyDeathRippleCount = static_cast<int32_t>(UILayoutIO::GetFloat(tuning, "particle.enemyDeathRippleCount", static_cast<float>(particleTuning_.enemyDeathRippleCount)));
	particleTuning_.expRippleCount = static_cast<int32_t>(UILayoutIO::GetFloat(tuning, "particle.expRippleCount", static_cast<float>(particleTuning_.expRippleCount)));
	particleTuning_.expSparkCount = static_cast<int32_t>(UILayoutIO::GetFloat(tuning, "particle.expSparkCount", static_cast<float>(particleTuning_.expSparkCount)));
	particleTuning_.lightningSparkCount = static_cast<int32_t>(UILayoutIO::GetFloat(tuning, "particle.lightningSparkCount", static_cast<float>(particleTuning_.lightningSparkCount)));
	particleTuning_.lightningRippleCount = static_cast<int32_t>(UILayoutIO::GetFloat(tuning, "particle.lightningRippleCount", static_cast<float>(particleTuning_.lightningRippleCount)));
	particleTuning_.levelUpConfettiCount = static_cast<int32_t>(UILayoutIO::GetFloat(tuning, "particle.levelUpConfettiCount", static_cast<float>(particleTuning_.levelUpConfettiCount)));
	particleTuning_.playerDeathSparkCount = static_cast<int32_t>(UILayoutIO::GetFloat(tuning, "particle.playerDeathSparkCount", static_cast<float>(particleTuning_.playerDeathSparkCount)));
	particleTuning_.playerDeathSmokeCount = static_cast<int32_t>(UILayoutIO::GetFloat(tuning, "particle.playerDeathSmokeCount", static_cast<float>(particleTuning_.playerDeathSmokeCount)));
	particleTuning_.playerDeathRippleCount = static_cast<int32_t>(UILayoutIO::GetFloat(tuning, "particle.playerDeathRippleCount", static_cast<float>(particleTuning_.playerDeathRippleCount)));
	particleTuning_.sparkLifetime = UILayoutIO::GetFloat(tuning, "particle.sparkLifetime", particleTuning_.sparkLifetime);
	particleTuning_.sparkVelocityScale = UILayoutIO::GetFloat(tuning, "particle.sparkVelocityScale", particleTuning_.sparkVelocityScale);
	particleTuning_.sparkScaleMultiplier = UILayoutIO::GetFloat(tuning, "particle.sparkScaleMultiplier", particleTuning_.sparkScaleMultiplier);
	particleTuning_.smokeLifetime = UILayoutIO::GetFloat(tuning, "particle.smokeLifetime", particleTuning_.smokeLifetime);
	particleTuning_.smokeScaleMultiplier = UILayoutIO::GetFloat(tuning, "particle.smokeScaleMultiplier", particleTuning_.smokeScaleMultiplier);
	particleTuning_.rippleLifetime = UILayoutIO::GetFloat(tuning, "particle.rippleLifetime", particleTuning_.rippleLifetime);
	particleTuning_.rippleExpandSpeed = UILayoutIO::GetFloat(tuning, "particle.rippleExpandSpeed", particleTuning_.rippleExpandSpeed);
	particleTuning_.confettiVelocityScale = UILayoutIO::GetFloat(tuning, "particle.confettiVelocityScale", particleTuning_.confettiVelocityScale);
	particleTuning_.confettiScaleMultiplier = UILayoutIO::GetFloat(tuning, "particle.confettiScaleMultiplier", particleTuning_.confettiScaleMultiplier);

	if (player_) {
		player_->SetCameraHeight(UILayoutIO::GetFloat(tuning, "camera.height", player_->GetCameraHeight()));
		player_->SetCameraDistance(UILayoutIO::GetFloat(tuning, "camera.distance", player_->GetCameraDistance()));
		player_->SetCameraPitch(UILayoutIO::GetFloat(tuning, "camera.pitch", player_->GetCameraPitch()));
		player_->SetMouseAimEnabled(UILayoutIO::GetFloat(tuning, "camera.mouseAimEnabled", player_->IsMouseAimEnabled() ? 1.0f : 0.0f) > 0.5f);
		const int32_t cameraMode = static_cast<int32_t>(UILayoutIO::GetFloat(tuning, "camera.mode", static_cast<float>(player_->GetCameraMode())));
		if (cameraMode >= 0 && cameraMode <= static_cast<int32_t>(Player::CameraMode::TopDown)) {
			player_->SetCameraMode(static_cast<Player::CameraMode>(cameraMode));
		}
	}
}

void DirectXGameScene::SaveDebugTuning() const
{
#ifdef _DEBUG
	std::vector<UILayoutIO::Entry> entries{
		{ "audio.master", { GameAudioCache::GetMasterVolume() } },
		{ "particle.playerDamageSparkCount", { static_cast<float>(particleTuning_.playerDamageSparkCount) } },
		{ "particle.playerDamageRippleCount", { static_cast<float>(particleTuning_.playerDamageRippleCount) } },
		{ "particle.enemyHitSparkCount", { static_cast<float>(particleTuning_.enemyHitSparkCount) } },
		{ "particle.enemyDeathSparkCount", { static_cast<float>(particleTuning_.enemyDeathSparkCount) } },
		{ "particle.enemyDeathSmokeCount", { static_cast<float>(particleTuning_.enemyDeathSmokeCount) } },
		{ "particle.enemyDeathRippleCount", { static_cast<float>(particleTuning_.enemyDeathRippleCount) } },
		{ "particle.expRippleCount", { static_cast<float>(particleTuning_.expRippleCount) } },
		{ "particle.expSparkCount", { static_cast<float>(particleTuning_.expSparkCount) } },
		{ "particle.lightningSparkCount", { static_cast<float>(particleTuning_.lightningSparkCount) } },
		{ "particle.lightningRippleCount", { static_cast<float>(particleTuning_.lightningRippleCount) } },
		{ "particle.levelUpConfettiCount", { static_cast<float>(particleTuning_.levelUpConfettiCount) } },
		{ "particle.playerDeathSparkCount", { static_cast<float>(particleTuning_.playerDeathSparkCount) } },
		{ "particle.playerDeathSmokeCount", { static_cast<float>(particleTuning_.playerDeathSmokeCount) } },
		{ "particle.playerDeathRippleCount", { static_cast<float>(particleTuning_.playerDeathRippleCount) } },
		{ "particle.sparkLifetime", { particleTuning_.sparkLifetime } },
		{ "particle.sparkVelocityScale", { particleTuning_.sparkVelocityScale } },
		{ "particle.sparkScaleMultiplier", { particleTuning_.sparkScaleMultiplier } },
		{ "particle.smokeLifetime", { particleTuning_.smokeLifetime } },
		{ "particle.smokeScaleMultiplier", { particleTuning_.smokeScaleMultiplier } },
		{ "particle.rippleLifetime", { particleTuning_.rippleLifetime } },
		{ "particle.rippleExpandSpeed", { particleTuning_.rippleExpandSpeed } },
		{ "particle.confettiVelocityScale", { particleTuning_.confettiVelocityScale } },
		{ "particle.confettiScaleMultiplier", { particleTuning_.confettiScaleMultiplier } },
	};

	for (const auto& [key, fallback] : kAudioTuningDefaults) {
		entries.push_back({ std::string("audio.") + key, { GameAudioCache::GetTunedVolume(key, fallback) } });
	}
	if (player_) {
		entries.push_back({ "camera.height", { player_->GetCameraHeight() } });
		entries.push_back({ "camera.distance", { player_->GetCameraDistance() } });
		entries.push_back({ "camera.pitch", { player_->GetCameraPitch() } });
		entries.push_back({ "camera.mode", { static_cast<float>(player_->GetCameraMode()) } });
		entries.push_back({ "camera.mouseAimEnabled", { player_->IsMouseAimEnabled() ? 1.0f : 0.0f } });
	}
	UILayoutIO::Save(DataPaths::kDebugTuning, entries);
#endif
}

void DirectXGameScene::ApplyParticleBehaviorTuning()
{
	SparkParticleBehavior::Settings sparkSettings{};
	sparkSettings.lifetime = particleTuning_.sparkLifetime;
	sparkSettings.horizontalSpeed *= particleTuning_.sparkVelocityScale;
	sparkSettings.verticalSpeedMin *= particleTuning_.sparkVelocityScale;
	sparkSettings.verticalSpeedMax *= particleTuning_.sparkVelocityScale;
	sparkSettings.scaleMin *= particleTuning_.sparkScaleMultiplier;
	sparkSettings.scaleMax *= particleTuning_.sparkScaleMultiplier;

	SmokeParticleBehavior::Settings smokeSettings{};
	smokeSettings.lifetime = particleTuning_.smokeLifetime;
	smokeSettings.scaleMin *= particleTuning_.smokeScaleMultiplier;
	smokeSettings.scaleMax *= particleTuning_.smokeScaleMultiplier;

	RippleParticleBehavior::Settings rippleSettings{};
	rippleSettings.lifetime = particleTuning_.rippleLifetime;
	rippleSettings.expandSpeed = particleTuning_.rippleExpandSpeed;

	ConfettiParticleBehavior::Settings confettiSettings{};
	confettiSettings.velocityScale = particleTuning_.confettiVelocityScale;
	confettiSettings.scaleMultiplier = particleTuning_.confettiScaleMultiplier;

	Engine::Particle::ParticleManager* particleManager = Engine::Particle::ParticleManager::GetInstance();
	particleManager->SetBehavior("DirectXGame.Ripple", std::make_unique<RippleParticleBehavior>(Vector4{ 0.45f, 0.75f, 1.0f, 1.0f }, rippleSettings));
	particleManager->SetBehavior("DirectXGame.Spark", std::make_unique<SparkParticleBehavior>(Vector4{ 1.0f, 0.35f, 0.25f, 1.0f }, sparkSettings));
	particleManager->SetBehavior("DirectXGame.EnemyHitSpark", std::make_unique<SparkParticleBehavior>(Vector4{ 1.0f, 0.62f, 0.18f, 1.0f }, sparkSettings));
	particleManager->SetBehavior("DirectXGame.ExpSpark", std::make_unique<SparkParticleBehavior>(Vector4{ 0.35f, 1.0f, 0.58f, 1.0f }, sparkSettings));
	particleManager->SetBehavior("DirectXGame.LightningSpark", std::make_unique<SparkParticleBehavior>(Vector4{ 0.48f, 0.84f, 1.0f, 1.0f }, sparkSettings));
	particleManager->SetBehavior("DirectXGame.PlayerDeathSpark", std::make_unique<SparkParticleBehavior>(Vector4{ 1.0f, 0.18f, 0.12f, 1.0f }, sparkSettings));
	particleManager->SetBehavior("DirectXGame.DeathSmoke", std::make_unique<SmokeParticleBehavior>(Vector4{ 0.45f, 0.42f, 0.38f, 0.85f }, smokeSettings));
	particleManager->SetBehavior("DirectXGame.Confetti", std::make_unique<ConfettiParticleBehavior>(confettiSettings));
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
	if (enemyManager_) {
		for (const Vector3& hitPosition : enemyManager_->GetRecentHitEffectPositions()) {
			particleManager->Emit("DirectXGame.EnemyHitSpark", hitPosition, static_cast<uint32_t>((std::max)(0, particleTuning_.enemyHitSparkCount)));
		}
		for (const Vector3& deathPosition : enemyManager_->GetRecentDeathEffectPositions()) {
			particleManager->Emit("DirectXGame.EnemyHitSpark", deathPosition, static_cast<uint32_t>((std::max)(0, particleTuning_.enemyDeathSparkCount)));
			particleManager->Emit("DirectXGame.DeathSmoke", deathPosition, static_cast<uint32_t>((std::max)(0, particleTuning_.enemyDeathSmokeCount)));
			particleManager->Emit("DirectXGame.Ripple", deathPosition, static_cast<uint32_t>((std::max)(0, particleTuning_.enemyDeathRippleCount)));
		}
		enemyManager_->ClearRecentEffectPositions();
	}

	const int32_t hp = playerManager_->GetHP();
	if (hp < previousEffectHp_) {
		particleManager->Emit("DirectXGame.Spark", playerPosition, static_cast<uint32_t>((std::max)(0, particleTuning_.playerDamageSparkCount)));
		particleManager->Emit("DirectXGame.Ripple", playerPosition, static_cast<uint32_t>((std::max)(0, particleTuning_.playerDamageRippleCount)));
	}
	previousEffectHp_ = hp;
	if (gameState_ == GameState::Dead && !deathEffectEmitted_) {
		particleManager->Emit("DirectXGame.PlayerDeathSpark", playerPosition, static_cast<uint32_t>((std::max)(0, particleTuning_.playerDeathSparkCount)));
		particleManager->Emit("DirectXGame.DeathSmoke", playerPosition, static_cast<uint32_t>((std::max)(0, particleTuning_.playerDeathSmokeCount)));
		particleManager->Emit("DirectXGame.Ripple", playerPosition, static_cast<uint32_t>((std::max)(0, particleTuning_.playerDeathRippleCount)));
		deathEffectEmitted_ = true;
	}

	const int32_t totalExp = playerManager_->GetTotalEXP();
	if (totalExp > previousEffectTotalExp_) {
		particleManager->Emit("DirectXGame.Ripple", playerPosition, static_cast<uint32_t>((std::max)(0, particleTuning_.expRippleCount)));
		particleManager->Emit("DirectXGame.ExpSpark", playerPosition, static_cast<uint32_t>((std::max)(0, particleTuning_.expSparkCount)));
	}
	previousEffectTotalExp_ = totalExp;

	const float lightningTimer = playerManager_->GetLightningEffectTimer();
	if (lightningTimer > previousLightningEffectTimer_) {
		for (const Vector3& target : playerManager_->GetLightningEffectTargets()) {
			particleManager->Emit("DirectXGame.LightningSpark", target, static_cast<uint32_t>((std::max)(0, particleTuning_.lightningSparkCount)));
			particleManager->Emit("DirectXGame.Ripple", target, static_cast<uint32_t>((std::max)(0, particleTuning_.lightningRippleCount)));
		}
	}
	previousLightningEffectTimer_ = lightningTimer;
}

void DirectXGameScene::UpdateUi(float deltaTime)
{
	if (!uiInitialized_) {
		return;
	}

	uiAnimationTime_ += deltaTime;

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
		const float cursorPulse = 0.5f + 0.5f * std::sin(uiAnimationTime_ * 8.0f);
		pauseCursor_.SetScale(1.0f + cursorPulse * 0.08f);
		pauseCursor_.SetAlpha(0.72f + cursorPulse * 0.28f);
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
		if (levelUpAnimationState_ != LevelUpAnimationState::Idle) {
			return;
		}
		if (IsMenuUpTriggered()) {
			MoveMenuSelection(-1);
		}
		if (IsMenuDownTriggered()) {
			MoveMenuSelection(1);
		}
		const float selectedPulse = 0.5f + 0.5f * std::sin(uiAnimationTime_ * 7.2f);
		for (size_t index = 0; index < levelUpChoiceSprites_.size(); ++index) {
			const bool selected = index == static_cast<size_t>(menuSelection_);
			const Vector4 choiceColor = selected
				? Vector4{ 1.08f + selectedPulse * 0.10f, 1.08f + selectedPulse * 0.10f, 0.74f + selectedPulse * 0.16f, 1.0f }
				: Vector4{ 0.86f, 0.86f, 0.86f, 1.0f };
			levelUpChoiceSprites_[index].SetColor(choiceColor);
			levelUpChoiceIcons_[index].SetColor(choiceColor);
			levelUpChoiceSprites_[index].SetAlpha(selected ? 1.0f : 0.78f);
			levelUpChoiceIcons_[index].SetAlpha(selected ? 1.0f : 0.78f);
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
		for (UILabel& choiceSprite : levelUpChoiceSprites_) {
			choiceSprite.Draw();
		}
		for (UILabel& choiceIcon : levelUpChoiceIcons_) {
			choiceIcon.Draw();
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
		const float pulse = 0.5f + 0.5f * std::sin(uiAnimationTime_ * 3.8f + static_cast<float>(index) * 0.65f);
		const bool enabled = alphas[index] >= 0.95f;
		icon.SetPosition({
			pauseBuildLayout_.position.x + pauseBuildLayout_.stepX * static_cast<float>(index),
			pauseBuildLayout_.position.y,
		});
		icon.SetSize(pauseBuildLayout_.iconSize);
		icon.SetVisible(showBuildIcons);
		icon.SetScale(enabled ? 1.0f + pulse * 0.035f : 1.0f);
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
			EnterPlaying();
		}
		break;
	case LevelUpAnimationState::Idle:
	case LevelUpAnimationState::Hidden:
	default:
		break;
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
	deathEffectEmitted_ = false;
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

	const auto addChoice = [this](std::string texturePath, std::string iconPath, std::function<void()> apply) {
		levelUpChoices_.push_back({
			std::move(texturePath),
			std::move(iconPath),
			std::move(apply),
			});
		};

	if (!playerManager_->IsNormalBulletMaxLevel()) {
		addChoice(
			WeaponLevelTexturePath("normal", playerManager_->GetNormalBulletLevel() + 1),
			"ui/game/normal/icon.png",
			[this]() { playerManager_->UpgradeNormalBullets(); });
	}
	if (!playerManager_->IsOrbitBulletMaxLevel()) {
		addChoice(
			playerManager_->HasOrbitBullets() ? WeaponLevelTexturePath("orbit", playerManager_->GetOrbitBulletLevel() + 1) : "ui/game/orbit/add.png",
			"ui/game/orbit/icon.png",
			[this]() { playerManager_->UpgradeOrbitBullets(); });
	}
	if (!playerManager_->IsDroneMaxLevel()) {
		addChoice(
			playerManager_->HasDrone() ? WeaponLevelTexturePath("drone", playerManager_->GetDroneLevel() + 1) : "ui/game/drone/add.png",
			"ui/game/drone/icon.png",
			[this]() { playerManager_->UpgradeDrone(); });
	}
	if (!playerManager_->IsLightningMaxLevel()) {
		addChoice(
			playerManager_->HasLightning() ? WeaponLevelTexturePath("lightning", playerManager_->GetLightningLevel() + 1) : "ui/game/lightning/add.png",
			"ui/game/lightning/icon.png",
			[this]() { playerManager_->UpgradeLightning(); });
	}
	addChoice("ui/game/lvup_attack.png", "ui/game/lvup_attack_icon.png", [this]() { playerManager_->UpgradeAttackPower(); });
	addChoice("ui/game/lvup_maxhp.png", "ui/game/lvup_maxhp_icon.png", [this]() { playerManager_->IncreaseMaxHP(); });
	addChoice("ui/game/lvup_speed.png", "ui/game/lvup_speed_icon.png", [this]() { playerManager_->UpgradeMoveSpeed(); });
	if (playerManager_->GetHP() < playerManager_->GetMaxHP()) {
		addChoice("ui/game/lvup_heal.png", "ui/game/lvup_heal_icon.png", [this]() { playerManager_->RecoverHP(); });
	}

	static std::mt19937 rng{ std::random_device{}() };
	std::shuffle(levelUpChoices_.begin(), levelUpChoices_.end(), rng);
	if (levelUpChoices_.size() > levelUpChoiceSprites_.size()) {
		levelUpChoices_.resize(levelUpChoiceSprites_.size());
	}

	for (size_t index = 0; index < levelUpChoiceSprites_.size(); ++index) {
		const std::string path = index < levelUpChoices_.size() ? levelUpChoices_[index].texturePath : "ui/game/lvup_attack.png";
		levelUpChoiceSprites_[index].Initialize(path, { 0.0f, 140.0f * static_cast<float>(index) });
		levelUpChoiceSprites_[index].SetSize(kLevelUpChoiceSize);
		const std::string iconPath = index < levelUpChoices_.size() ? levelUpChoices_[index].iconPath : "ui/game/lvup_attack_icon.png";
		levelUpChoiceIcons_[index].Initialize(iconPath, {
			0.0f,
			140.0f * static_cast<float>(index),
		});
		levelUpChoiceIcons_[index].SetSize(kLevelUpChoiceSize);
		levelUpChoiceIcons_[index].SetVisible(index < levelUpChoices_.size());
	}
	ApplyLevelUpLayout();
}

void DirectXGameScene::ApplySelectedLevelUpChoice()
{
	if (!playerManager_ || levelUpChoices_.empty()) {
		return;
	}

	const LevelUpChoice& choice = levelUpChoices_[static_cast<size_t>(std::clamp(menuSelection_, 0, static_cast<int32_t>(levelUpChoices_.size()) - 1))];
	if (choice.apply) {
		choice.apply();
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
		levelUpChoiceSprites_[index].SetSize(kLevelUpChoiceSize);
		levelUpChoiceIcons_[index].SetPosition({
			levelUpSlideOffsetX_,
			140.0f * static_cast<float>(index),
		});
		levelUpChoiceIcons_[index].SetSize(kLevelUpChoiceSize);
	}
}

void DirectXGameScene::SpawnLevelUpConfetti()
{
	if (!player_) {
		return;
	}

	Engine::Particle::ParticleManager::GetInstance()->Emit(
		"DirectXGame.Confetti",
		player_->GetWorldPosition(),
		static_cast<uint32_t>((std::max)(0, particleTuning_.levelUpConfettiCount)));
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
	ImGui::Text("Stage 17.1 particle tuning scene");
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
	if (ImGui::Button("Save Debug Tuning")) {
		SaveDebugTuning();
	}
	ImGui::SameLine();
	if (ImGui::Button("Reload Debug Tuning")) {
		LoadDebugTuning();
		ApplyParticleBehaviorTuning();
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
	if (ImGui::CollapsingHeader("Gameplay Dashboard", ImGuiTreeNodeFlags_DefaultOpen)) {
		const size_t enemyCount = enemyManager_ ? enemyManager_->GetActiveEnemyCount() : 0;
		const size_t orbCount = enemyManager_ ? enemyManager_->GetExpOrbCount() : 0;
		const size_t normalBulletCount = playerManager_ ? playerManager_->GetNormalBullets().size() : 0;
		const size_t orbitBulletCount = playerManager_ ? playerManager_->GetOrbitBullets().size() : 0;
		size_t droneBulletCount = 0;
		if (playerManager_ && playerManager_->HasDrone() && playerManager_->GetDrone()) {
			droneBulletCount = playerManager_->GetDrone()->GetBullets().size();
		}

		Engine::Particle::ParticleManager* particleManager = Engine::Particle::ParticleManager::GetInstance();
		ImGui::Text("State: %s", GetGameStateName());
		ImGui::Text("Enemies: %zu  EXP Orbs: %zu  Kills: %d", enemyCount, orbCount, enemyManager_ ? enemyManager_->GetTotalKillCount() : 0);
		ImGui::Text("Bullets: Normal %zu / Orbit %zu / Drone %zu", normalBulletCount, orbitBulletCount, droneBulletCount);
		ImGui::Text("Particles: %zu active across %zu groups",
			particleManager->GetTotalActiveParticleCount(),
			particleManager->GetParticleGroupCount());
		if (ImGui::TreeNode("Particle Groups")) {
			constexpr std::array<const char*, 8> kDirectXGameParticleGroups{
				"DirectXGame.Ripple",
				"DirectXGame.Spark",
				"DirectXGame.EnemyHitSpark",
				"DirectXGame.ExpSpark",
				"DirectXGame.LightningSpark",
				"DirectXGame.PlayerDeathSpark",
				"DirectXGame.DeathSmoke",
				"DirectXGame.Confetti",
			};
			for (const char* groupName : kDirectXGameParticleGroups) {
				const std::optional<size_t> activeCount = particleManager->GetActiveParticleCount(groupName);
				ImGui::Text("%s: %zu", groupName, activeCount.value_or(0));
			}
			ImGui::TreePop();
		}
		if (playerManager_) {
			ImGui::Text("Weapons: Normal Lv%d Dmg%d Interval %.2f",
				playerManager_->GetNormalBulletLevel(),
				playerManager_->GetNormalBulletDamage(),
				playerManager_->GetNormalBulletInterval());
			ImGui::Text("Orbit Lv%d Dmg%d | Drone Lv%d Dmg%d | Lightning Lv%d Dmg%d Count%d Radius %.1f",
				playerManager_->GetOrbitBulletLevel(),
				playerManager_->GetOrbitBulletDamage(),
				playerManager_->GetDroneLevel(),
				playerManager_->GetDroneDamage(),
				playerManager_->GetLightningLevel(),
				playerManager_->GetLightningDamage(),
				playerManager_->GetLightningStrikeCount(),
				playerManager_->GetLightningRadius());
		}
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
		if (ImGui::CollapsingHeader("Particle Tuning")) {
			ImGui::SliderInt("Damage Spark Count", &particleTuning_.playerDamageSparkCount, 0, 100);
			ImGui::SliderInt("Damage Ripple Count", &particleTuning_.playerDamageRippleCount, 0, 12);
			ImGui::SliderInt("Enemy Hit Spark Count", &particleTuning_.enemyHitSparkCount, 0, 80);
			ImGui::SliderInt("Enemy Death Spark Count", &particleTuning_.enemyDeathSparkCount, 0, 120);
			ImGui::SliderInt("Enemy Death Smoke Count", &particleTuning_.enemyDeathSmokeCount, 0, 80);
			ImGui::SliderInt("Enemy Death Ripple Count", &particleTuning_.enemyDeathRippleCount, 0, 12);
			ImGui::SliderInt("EXP Ripple Count", &particleTuning_.expRippleCount, 0, 12);
			ImGui::SliderInt("EXP Spark Count", &particleTuning_.expSparkCount, 0, 80);
			ImGui::SliderInt("Lightning Spark Count", &particleTuning_.lightningSparkCount, 0, 100);
			ImGui::SliderInt("Lightning Ripple Count", &particleTuning_.lightningRippleCount, 0, 12);
			ImGui::SliderInt("LevelUp Confetti Count", &particleTuning_.levelUpConfettiCount, 0, 160);
			ImGui::SliderInt("Player Death Spark Count", &particleTuning_.playerDeathSparkCount, 0, 160);
			ImGui::SliderInt("Player Death Smoke Count", &particleTuning_.playerDeathSmokeCount, 0, 100);
			ImGui::SliderInt("Player Death Ripple Count", &particleTuning_.playerDeathRippleCount, 0, 12);
			bool particleBehaviorChanged = false;
			particleBehaviorChanged |= ImGui::SliderFloat("Spark Lifetime", &particleTuning_.sparkLifetime, 0.08f, 1.2f);
			particleBehaviorChanged |= ImGui::SliderFloat("Spark Velocity Scale", &particleTuning_.sparkVelocityScale, 0.2f, 3.0f);
			particleBehaviorChanged |= ImGui::SliderFloat("Spark Scale Multiplier", &particleTuning_.sparkScaleMultiplier, 0.25f, 3.0f);
			particleBehaviorChanged |= ImGui::SliderFloat("Smoke Lifetime", &particleTuning_.smokeLifetime, 0.2f, 2.0f);
			particleBehaviorChanged |= ImGui::SliderFloat("Smoke Scale Multiplier", &particleTuning_.smokeScaleMultiplier, 0.25f, 3.0f);
			particleBehaviorChanged |= ImGui::SliderFloat("Ripple Lifetime", &particleTuning_.rippleLifetime, 0.1f, 1.2f);
			particleBehaviorChanged |= ImGui::SliderFloat("Ripple Expand Speed", &particleTuning_.rippleExpandSpeed, 1.0f, 9.0f);
			particleBehaviorChanged |= ImGui::SliderFloat("Confetti Velocity Scale", &particleTuning_.confettiVelocityScale, 0.2f, 3.0f);
			particleBehaviorChanged |= ImGui::SliderFloat("Confetti Scale Multiplier", &particleTuning_.confettiScaleMultiplier, 0.25f, 3.0f);
			if (particleBehaviorChanged) {
				ApplyParticleBehaviorTuning();
			}
			if (ImGui::Button("Emit Player Spark") && player_) {
				Engine::Particle::ParticleManager::GetInstance()->Emit(
					"DirectXGame.Spark",
					player_->GetWorldPosition(),
					static_cast<uint32_t>((std::max)(0, particleTuning_.playerDamageSparkCount)));
			}
			ImGui::SameLine();
			if (ImGui::Button("Emit Enemy Death") && player_) {
				Engine::Particle::ParticleManager::GetInstance()->Emit(
					"DirectXGame.EnemyHitSpark",
					player_->GetWorldPosition(),
					static_cast<uint32_t>((std::max)(0, particleTuning_.enemyDeathSparkCount)));
				Engine::Particle::ParticleManager::GetInstance()->Emit(
					"DirectXGame.DeathSmoke",
					player_->GetWorldPosition(),
					static_cast<uint32_t>((std::max)(0, particleTuning_.enemyDeathSmokeCount)));
			}
			ImGui::SameLine();
			if (ImGui::Button("Emit Player Death") && player_) {
				Engine::Particle::ParticleManager::GetInstance()->Emit(
					"DirectXGame.PlayerDeathSpark",
					player_->GetWorldPosition(),
					static_cast<uint32_t>((std::max)(0, particleTuning_.playerDeathSparkCount)));
				Engine::Particle::ParticleManager::GetInstance()->Emit(
					"DirectXGame.DeathSmoke",
					player_->GetWorldPosition(),
					static_cast<uint32_t>((std::max)(0, particleTuning_.playerDeathSmokeCount)));
				Engine::Particle::ParticleManager::GetInstance()->Emit(
					"DirectXGame.Ripple",
					player_->GetWorldPosition(),
					static_cast<uint32_t>((std::max)(0, particleTuning_.playerDeathRippleCount)));
			}
			ImGui::SameLine();
			if (ImGui::Button("Emit LevelUp Confetti")) {
				SpawnLevelUpConfetti();
			}
		}
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

const char* DirectXGameScene::GetGameStateName() const
{
	switch (gameState_) {
	case GameState::Start:
		return "Start";
	case GameState::Playing:
		return "Playing";
	case GameState::Paused:
		return "Paused";
	case GameState::LevelUp:
		return "LevelUp";
	case GameState::Dead:
		return "Dead";
	default:
		return "Unknown";
	}
}

}
