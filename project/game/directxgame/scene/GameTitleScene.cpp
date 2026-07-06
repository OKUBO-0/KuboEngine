#include "GameTitleScene.h"
#include "DirectXCommon.h"
#include "DataPaths.h"
#include "ResourceProbe.h"
#include "GameAudioTuning.h"
#include "GameMenuController.h"
#include "SceneId.h"
#include "GameSession.h"
#include "GameInputBindings.h"
#include "GameModelCache.h"
#include "GameSpriteFactory.h"
#include "ScreenUtil.h"
#include "SceneLighting.h"
#include "TitleSceneDebugUIController.h"
#include "UILayoutIO.h"
#include "CurtainTransition.h"
#include "DigitSpriteUtil.h"
#include "Camera.h"
#include "CameraManager.h"
#include "Input.h"
#include "Object3D.h"
#include "Object3DCommon.h"
#include "OffscreenRenderManager.h"
#include "SceneManager.h"
#include "SpriteCommon.h"
#include "SrvManager.h"
#include "TextureManager.h"
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>

namespace {

constexpr float kFixedDeltaTime = 1.0f / 60.0f;
constexpr char kTitleTexturePath[] = "ui/title/title.png";
constexpr char kCursorTexturePath[] = "ui/title/cursor.png";
constexpr char kShopTexturePath[] = "ui/title/shop.png";
constexpr char kNumberTexturePath[] = "ui/number/numbers.png";
constexpr char kPermanentMaxHPIconPath[] = "ui/game/lvup/maxhp_icon.png";
constexpr char kPermanentAttackIconPath[] = "ui/game/lvup/attack_icon.png";
constexpr char kPermanentMoveSpeedIconPath[] = "ui/game/lvup/speed_icon.png";
constexpr char kPermanentExpPickupRangeIconPath[] = "ui/game/lvup/heal_icon.png";
constexpr char kPermanentCoinGainIconPath[] = "ui/game/lvup/normal_icon.png";
constexpr char kTitleBgmPath[] = "audio/bgm/title.wav";
constexpr char kSelectSePath[] = "audio/se/se_pause.wav";
constexpr char kDecideSePath[] = "audio/se/se_exp.wav";
constexpr char kAudioTitleBgm[] = "title.bgm";
constexpr char kAudioTitleSelect[] = "title.select";
constexpr char kAudioTitleDecide[] = "title.decide";
constexpr char kEnvironmentTexturePath[] = "Resources/textures/skybox/test.dds";
constexpr char kTitleCameraName[] = "directxgame_title";
constexpr Vector2 kPermanentUpgradeIconBasePosition{ 48.0f, 86.0f };
constexpr Vector2 kPermanentUpgradeIconSize{ 44.0f, 44.0f };
constexpr Vector2 kPermanentUpgradeRowHitboxSize{ 150.0f, 44.0f };
constexpr float kPermanentUpgradeIconStepY = 54.0f;
constexpr Vector2 kPermanentUpgradeLevelOffset{ 52.0f, 8.0f };
constexpr Vector2 kPermanentUpgradeCostOffset{ 78.0f, 11.0f };
constexpr Vector2 kPermanentUpgradeCostDigitSize{ 15.0f, 20.0f };
constexpr float kPermanentUpgradeCostDigitStepX = 14.0f;
constexpr float kPermanentUpgradePurchaseFlashDuration = 0.42f;
constexpr std::array<int32_t, 5> kShopLevelCaps{ 3, 5, 3, 3, 3 };
constexpr Vector2 kCharacterIconBasePosition{ 1016.0f, 96.0f };
constexpr Vector2 kCharacterIconSize{ 48.0f, 48.0f };
constexpr Vector2 kCharacterRowHitboxSize{ 160.0f, 52.0f };
constexpr float kCharacterIconStepY = 58.0f;
constexpr Vector2 kCharacterCostOffset{ 58.0f, 15.0f };

enum class PermanentUpgradeType {
	MaxHP,
	Attack,
	MoveSpeed,
	ExpPickupRange,
	CoinGain,
};

enum class PermanentUpgradeCategory {
	Survival,
	Offense,
	Mobility,
	Utility,
};

struct PermanentUpgradeDefinition {
	PermanentUpgradeType type;
	PermanentUpgradeCategory category;
	const char* iconPath;
};

constexpr std::array<PermanentUpgradeDefinition, 5> kPermanentUpgradeDefinitions{ {
	{ PermanentUpgradeType::MaxHP, PermanentUpgradeCategory::Survival, kPermanentMaxHPIconPath },
	{ PermanentUpgradeType::Attack, PermanentUpgradeCategory::Offense, kPermanentAttackIconPath },
	{ PermanentUpgradeType::MoveSpeed, PermanentUpgradeCategory::Mobility, kPermanentMoveSpeedIconPath },
	{ PermanentUpgradeType::ExpPickupRange, PermanentUpgradeCategory::Utility, kPermanentExpPickupRangeIconPath },
	{ PermanentUpgradeType::CoinGain, PermanentUpgradeCategory::Utility, kPermanentCoinGainIconPath },
} };

struct CharacterUiDefinition {
	DirectXGame::CharacterId id;
	const char* iconPath;
};

constexpr std::array<CharacterUiDefinition, 4> kCharacterUiDefinitions{ {
	{ DirectXGame::CharacterId::Octopus, "ui/game/lvup/icon_common_unknown.png" },
	{ DirectXGame::CharacterId::Flame, "ui/game/lvup/icon_common_unknown.png" },
	{ DirectXGame::CharacterId::Blade, "ui/game/lvup/icon_common_unknown.png" },
	{ DirectXGame::CharacterId::Storm, "ui/game/lvup/lightning_icon.png" },
} };

bool IsPointInRect(const Vector2& point, const Vector2& rectPosition, const Vector2& rectSize)
{
	return point.x >= rectPosition.x && point.x <= rectPosition.x + rectSize.x &&
		point.y >= rectPosition.y && point.y <= rectPosition.y + rectSize.y;
}

Vector2 PermanentUpgradeIconPosition(int32_t index)
{
	return {
		kPermanentUpgradeIconBasePosition.x,
		kPermanentUpgradeIconBasePosition.y +
			kPermanentUpgradeIconStepY * static_cast<float>(index),
	};
}

Vector2 CharacterIconPosition(int32_t index)
{
	return {
		kCharacterIconBasePosition.x,
		kCharacterIconBasePosition.y +
			kCharacterIconStepY * static_cast<float>(index),
	};
}

Vector4 PermanentUpgradeCategoryColor(PermanentUpgradeCategory category, float alpha)
{
	switch (category) {
	case PermanentUpgradeCategory::Survival:
		return { 0.45f, 1.0f, 0.58f, alpha };
	case PermanentUpgradeCategory::Offense:
		return { 1.0f, 0.48f, 0.32f, alpha };
	case PermanentUpgradeCategory::Mobility:
		return { 0.42f, 0.78f, 1.0f, alpha };
	case PermanentUpgradeCategory::Utility:
	default:
		return { 1.0f, 0.86f, 0.32f, alpha };
	}
}

Vector4 PermanentUpgradeStatusColor(
	const PermanentUpgradeDefinition& definition,
	bool maxed,
	bool affordable,
	float purchaseFlashTimer)
{
	if (purchaseFlashTimer > 0.0f) {
		return { 0.35f, 1.0f, 0.45f, 1.0f };
	}
	if (maxed) {
		return { 0.45f, 0.75f, 1.0f, 0.95f };
	}
	if (affordable) {
		return PermanentUpgradeCategoryColor(definition.category, 0.95f);
	}
	return { 0.42f, 0.42f, 0.42f, 0.72f };
}

}

namespace DirectXGame {

TitleScene::TitleScene(std::shared_ptr<GameSession> sessionContext)
	: sessionContext_(std::move(sessionContext))
{
}

void TitleScene::Initialize()
{
	LoadGameAudioTuning();
	Engine::CameraSystem::CameraManager::GetInstance()->Initialize();
	if (Engine::Base::OffscreenRenderManager* offscreen = Engine::Base::OffscreenRenderManager::GetInstance()) {
		offscreen->SetScenePostEffectType(PostEffectType::Fullscreen);
	}

	if (sessionContext_) {
		sessionContext_->OnEnterTitleScene();
	}

	InitializeLighting();
	InitializeResources();
	InitializeCameraAndObjects();
	ResourceProbe::Verify();
	ApplyLayout();
}

void TitleScene::Finalize()
{
	if (titleBgmHandle_) {
		GameAudioCache::Stop(titleBgmHandle_);
	}

	Engine::CameraSystem::CameraManager::GetInstance()->RemoveCamera(kTitleCameraName);
}

void TitleScene::Update()
{
	if (sessionContext_ && sessionContext_->IsSceneStressEnabled()) {
		sessionContext_->AdvanceSceneStressFrame();
		if (sessionContext_->GetRunCount() == 0 &&
			sessionContext_->GetSceneStressFrameCount() == 1) {
			Engine::Graphics3D::Object3DCommon::GetInstance()
				->GetDxCommon()->ResetGpuTimingStatistics();
			Engine::Graphics3D::Object3DCommon::GetInstance()
				->ResetShadowPassStatistics();
		}
		if (Engine::Base::SrvManager* srvManager =
			Engine::Graphics3D::Object3DCommon::GetInstance()->GetSrvManager()) {
			const Engine::Base::SrvManager::UsageSummary usage =
				srvManager->GetUsageSummary();
			sessionContext_->RecordSrvUsage(
				GameSession::SceneStressStage::Title,
				srvManager->GetUsedCount(),
				srvManager->GetHighWatermark(),
				usage.texture2D,
				usage.textureCube,
				usage.structuredBuffer,
				usage.shadowMap,
				usage.other);
		}
		if (sessionContext_->GetSceneStressFrameCount() >= 30) {
			sessionContext_->BeginNewRun();
			Engine::Scene::SceneManager::GetInstance()->ChangeScene(SceneId::kGame);
			return;
		}
	}

	UpdateCurtain();

	if (curtainStarted_) {
		if (curtain_ && curtain_->IsFinished()) {
			if (titleBgmHandle_) {
				GameAudioCache::Stop(titleBgmHandle_);
			}
			Engine::Scene::SceneManager::GetInstance()->ChangeScene(SceneId::kGame);
			DrawDebugUI();
			return;
		}
		DrawDebugUI();
		return;
	}

	if (curtainOpening_) {
		if (curtain_ && curtain_->GetState() == CurtainTransition::State::None) {
			curtainOpening_ = false;
		}
		DrawDebugUI();
		return;
	}

	UpdateAudio();
	UpdateNavigation();
	if (showingUpgradeScreen_) {
		UpdatePermanentUpgradeInput();
	}
	UpdateModelAnimation();
	UpdateCameraAnimation();
	UpdateCoinDisplay();
	UpdateShopLevelDisplay();

	if (titleObject_) {
		titleObject_->Update();
	}
	if (gridPlane_) {
		gridPlane_->Update(layoutSettings_.modelBasePosition);
	}
	if (skyDomeObject_) {
		skyDomeObject_->Update();
	}
	if (Engine::CameraSystem::CameraManager::GetInstance()->GetActiveCamera()) {
		Engine::CameraSystem::CameraManager::GetInstance()->GetActiveCamera()->Update();
	}

	DrawDebugUI();
}

void TitleScene::Draw()
{
	Engine::Graphics3D::Object3DCommon* objectCommon =
		Engine::Graphics3D::Object3DCommon::GetInstance();
	if (objectCommon->BeginShadowPass(layoutSettings_.modelBasePosition)) {
		if (titleObject_) {
			titleObject_->DrawShadow();
		}
		if (gridPlane_) {
			gridPlane_->DrawShadow();
		}
		objectCommon->EndShadowPass();
	}

	objectCommon->CommonDraw();
	if (skyDomeObject_) {
		skyDomeObject_->Draw();
	}
	if (gridPlane_) {
		gridPlane_->Draw();
	}
	if (titleObject_) {
		titleObject_->Draw();
	}

	Engine::Graphics2D::SpriteCommon::GetInstance()->CommonDraw();
	titleSprite_.Draw();
	if (showingUpgradeScreen_) {
		shopSprite_.Draw();
	}
	cursorSprite_.Draw();
	if (showingUpgradeScreen_) {
		DrawCoinDisplay();
		DrawShopDisplay();
	}
	if (curtain_) {
		curtain_->Draw();
	}
}

void TitleScene::InitializeResources()
{
	const UILayoutIO::LayoutMap titleLayout = UILayoutIO::LoadOrDefault(DataPaths::kTitleLayout, {});
	layoutSettings_.titlePosition = UILayoutIO::GetVector2(titleLayout, "titlePosition", layoutSettings_.titlePosition);
	layoutSettings_.titleSize = UILayoutIO::GetVector2(titleLayout, "titleSize", layoutSettings_.titleSize);
	layoutSettings_.cursorBasePosition = UILayoutIO::GetVector2(titleLayout, "cursorBasePosition", layoutSettings_.cursorBasePosition);
	layoutSettings_.cursorSize = UILayoutIO::GetVector2(titleLayout, "cursorSize", layoutSettings_.cursorSize);
	layoutSettings_.cursorShopOffset = UILayoutIO::GetVector2(titleLayout, "cursorShopOffset", layoutSettings_.cursorShopOffset);
	layoutSettings_.cursorQuitOffset = UILayoutIO::GetVector2(titleLayout, "cursorQuitOffset", layoutSettings_.cursorQuitOffset);
	layoutSettings_.cursorEasingSpeed = UILayoutIO::GetFloat(titleLayout, "cursorEasingSpeed", layoutSettings_.cursorEasingSpeed);
	layoutSettings_.menuHitboxPosition = UILayoutIO::GetVector2(titleLayout, "menuHitboxPosition", layoutSettings_.menuHitboxPosition);
	layoutSettings_.menuHitboxSize = UILayoutIO::GetVector2(titleLayout, "menuHitboxSize", layoutSettings_.menuHitboxSize);
	for (int32_t index = 0; index < kPermanentUpgradeCount; ++index) {
		layoutSettings_.shopItemPositions[static_cast<size_t>(index)] = UILayoutIO::GetVector2(
			titleLayout,
			"shopItemPosition" + std::to_string(index),
			layoutSettings_.shopItemPositions[static_cast<size_t>(index)]);
	}
	layoutSettings_.shopItemHitboxSize = UILayoutIO::GetVector2(titleLayout, "shopItemHitboxSize", layoutSettings_.shopItemHitboxSize);
	layoutSettings_.shopLevelSquareSize = UILayoutIO::GetVector2(titleLayout, "shopLevelSquareSize", layoutSettings_.shopLevelSquareSize);
	layoutSettings_.shopLevelSquareStepX = UILayoutIO::GetFloat(titleLayout, "shopLevelSquareStepX", layoutSettings_.shopLevelSquareStepX);
	layoutSettings_.shopLevelSquareOffsetY = UILayoutIO::GetFloat(titleLayout, "shopLevelSquareOffsetY", layoutSettings_.shopLevelSquareOffsetY);
	layoutSettings_.shopPriceOffset = UILayoutIO::GetVector2(titleLayout, "shopPriceOffset", layoutSettings_.shopPriceOffset);
	layoutSettings_.shopPriceDigitSize = UILayoutIO::GetVector2(titleLayout, "shopPriceDigitSize", layoutSettings_.shopPriceDigitSize);
	layoutSettings_.shopPriceDigitStepX = UILayoutIO::GetFloat(titleLayout, "shopPriceDigitStepX", layoutSettings_.shopPriceDigitStepX);
	layoutSettings_.shopCoinPosition = UILayoutIO::GetVector2(titleLayout, "shopCoinPosition", layoutSettings_.shopCoinPosition);
	layoutSettings_.shopCoinDigitSize = UILayoutIO::GetVector2(titleLayout, "shopCoinDigitSize", layoutSettings_.shopCoinDigitSize);
	layoutSettings_.shopCoinDigitStepX = UILayoutIO::GetFloat(titleLayout, "shopCoinDigitStepX", layoutSettings_.shopCoinDigitStepX);
	layoutSettings_.modelBasePosition = UILayoutIO::GetVector3(titleLayout, "modelBasePosition", layoutSettings_.modelBasePosition);
	layoutSettings_.modelScale = UILayoutIO::GetVector3(titleLayout, "modelScale", layoutSettings_.modelScale);
	layoutSettings_.cameraTarget = UILayoutIO::GetVector3(titleLayout, "cameraTarget", layoutSettings_.cameraTarget);
	layoutSettings_.cameraDistance = UILayoutIO::GetFloat(titleLayout, "cameraDistance", layoutSettings_.cameraDistance);
	layoutSettings_.cameraHeight = UILayoutIO::GetFloat(titleLayout, "cameraHeight", layoutSettings_.cameraHeight);
	layoutSettings_.cameraPitch = UILayoutIO::GetFloat(titleLayout, "cameraPitch", layoutSettings_.cameraPitch);
	layoutSettings_.cameraYaw = UILayoutIO::GetFloat(titleLayout, "cameraYaw", layoutSettings_.cameraYaw);
	layoutSettings_.cameraOrbitSpeed = UILayoutIO::GetFloat(titleLayout, "cameraOrbitSpeed", layoutSettings_.cameraOrbitSpeed);
#ifdef _DEBUG
	auto loadWindowVisible = [&titleLayout](std::string_view key, bool fallback) {
		return UILayoutIO::GetFloat(titleLayout, key, fallback ? 1.0f : 0.0f) != 0.0f;
	};
	debugWindows_.windowSwitcher = loadWindowVisible("debug.windowSwitcher", debugWindows_.windowSwitcher);
	debugWindows_.titleView = loadWindowVisible("debug.titleView", debugWindows_.titleView);
	debugWindows_.statisticsView = loadWindowVisible("debug.statisticsView", debugWindows_.statisticsView);
	debugWindows_.titleSettings = loadWindowVisible("debug.titleSettings", debugWindows_.titleSettings);
	debugWindows_.offscreenSettings = loadWindowVisible("debug.offscreenSettings", debugWindows_.offscreenSettings);
	debugWindows_.lightSettings = loadWindowVisible("debug.lightSettings", debugWindows_.lightSettings);
	debugWindows_.audio = loadWindowVisible("debug.audio", debugWindows_.audio);
	debugWindows_.keyInputDebug = loadWindowVisible("debug.keyInputDebug", debugWindows_.keyInputDebug);
#endif

	const std::string titleTexturePath =
		ResourcePaths::MakeTexturePath(kTitleTexturePath);
	const std::string cursorTexturePath =
		ResourcePaths::MakeTexturePath(kCursorTexturePath);
	const std::string shopTexturePath =
		ResourcePaths::MakeTexturePath(kShopTexturePath);
	Engine::Base::TextureManager::GetInstance()->LoadTextures({
		titleTexturePath,
		cursorTexturePath,
		shopTexturePath,
		ResourcePaths::MakeTexturePath(kNumberTexturePath),
		ResourcePaths::MakeTexturePath(kPermanentMaxHPIconPath),
		ResourcePaths::MakeTexturePath(kPermanentAttackIconPath),
		ResourcePaths::MakeTexturePath(kPermanentMoveSpeedIconPath),
		ResourcePaths::MakeTexturePath(kPermanentExpPickupRangeIconPath),
		ResourcePaths::MakeTexturePath(kPermanentCoinGainIconPath),
		ResourcePaths::MakeTexturePath(kCharacterUiDefinitions[0].iconPath),
		ResourcePaths::MakeTexturePath(kCharacterUiDefinitions[1].iconPath),
		ResourcePaths::MakeTexturePath(kCharacterUiDefinitions[2].iconPath),
		ResourcePaths::MakeTexturePath(kCharacterUiDefinitions[3].iconPath),
		kEnvironmentTexturePath,
		});
	titleSprite_.Initialize(titleTexturePath, layoutSettings_.titlePosition);
	cursorSprite_.Initialize(cursorTexturePath, layoutSettings_.cursorBasePosition);
	shopSprite_.Initialize(shopTexturePath, { 0.0f, 0.0f });
	shopSprite_.SetSize({ 1280.0f, 720.0f });
	for (int32_t itemIndex = 0; itemIndex < kPermanentUpgradeCount; ++itemIndex) {
		const int32_t cap = kShopLevelCaps[static_cast<size_t>(itemIndex)];
		const float rowWidth = layoutSettings_.shopLevelSquareSize.x +
			layoutSettings_.shopLevelSquareStepX * static_cast<float>(cap - 1);
		const float startX = layoutSettings_.shopItemPositions[static_cast<size_t>(itemIndex)].x +
			(layoutSettings_.shopItemHitboxSize.x - rowWidth) * 0.5f;
		shopHighlights_[static_cast<size_t>(itemIndex)].Initialize();
		for (int32_t levelIndex = 0; levelIndex < kShopMaxLevelSlots; ++levelIndex) {
			UIPanel& square = shopLevelSquares_[static_cast<size_t>(itemIndex)][static_cast<size_t>(levelIndex)];
			square.Initialize();
			square.SetPosition({
				startX + layoutSettings_.shopLevelSquareStepX * static_cast<float>(levelIndex),
				layoutSettings_.shopItemPositions[static_cast<size_t>(itemIndex)].y + layoutSettings_.shopLevelSquareOffsetY,
			});
			square.SetSize(layoutSettings_.shopLevelSquareSize);
			square.SetVisible(levelIndex < cap);
		}
	}
	coinDigitTexture_ = GameTextureCache::Load(kNumberTexturePath);
	for (int32_t index = 0; index < kCoinDigitCount; ++index) {
		coinDigits_[index] = GameSpriteFactory::Create(
			coinDigitTexture_,
			{ layoutSettings_.shopCoinPosition.x + layoutSettings_.shopCoinDigitStepX * static_cast<float>(index), layoutSettings_.shopCoinPosition.y });
		coinDigits_[index]->SetSize(layoutSettings_.shopCoinDigitSize);
		coinDigits_[index]->SetTextureSize({ 24.0f, 32.0f });
	}
	for (int32_t index = 0; index < kPermanentUpgradeCount; ++index) {
		const Vector2 iconPosition = PermanentUpgradeIconPosition(index);
		permanentUpgradeIcons_[index].Initialize(
			kPermanentUpgradeDefinitions[static_cast<size_t>(index)].iconPath,
			iconPosition);
		permanentUpgradeIcons_[index].SetSize(kPermanentUpgradeIconSize);
		permanentUpgradeLevelDigits_[index] = GameSpriteFactory::Create(
			coinDigitTexture_,
			{
				iconPosition.x + kPermanentUpgradeLevelOffset.x,
				iconPosition.y + kPermanentUpgradeLevelOffset.y,
			});
		permanentUpgradeLevelDigits_[index]->SetSize({ 20.0f, 26.0f });
		permanentUpgradeLevelDigits_[index]->SetTextureSize({ 24.0f, 32.0f });
		for (int32_t digitIndex = 0; digitIndex < kPermanentUpgradeCostDigitCount; ++digitIndex) {
			permanentUpgradeCostDigits_[index][digitIndex] = GameSpriteFactory::Create(
				coinDigitTexture_,
				{
					iconPosition.x + kPermanentUpgradeCostOffset.x +
						kPermanentUpgradeCostDigitStepX * static_cast<float>(digitIndex),
					iconPosition.y + kPermanentUpgradeCostOffset.y,
				});
			permanentUpgradeCostDigits_[index][digitIndex]->SetSize(kPermanentUpgradeCostDigitSize);
			permanentUpgradeCostDigits_[index][digitIndex]->SetTextureSize({ 24.0f, 32.0f });
		}
	}
	for (int32_t index = 0; index < kCharacterCount; ++index) {
		const Vector2 iconPosition = CharacterIconPosition(index);
		characterIcons_[index].Initialize(
			kCharacterUiDefinitions[static_cast<size_t>(index)].iconPath,
			iconPosition);
		characterIcons_[index].SetSize(kCharacterIconSize);
		for (int32_t digitIndex = 0; digitIndex < kPermanentUpgradeCostDigitCount; ++digitIndex) {
			characterCostDigits_[index][digitIndex] = GameSpriteFactory::Create(
				coinDigitTexture_,
				{
					iconPosition.x + kCharacterCostOffset.x +
						kPermanentUpgradeCostDigitStepX * static_cast<float>(digitIndex),
					iconPosition.y + kCharacterCostOffset.y,
				});
			characterCostDigits_[index][digitIndex]->SetSize(kPermanentUpgradeCostDigitSize);
			characterCostDigits_[index][digitIndex]->SetTextureSize({ 24.0f, 32.0f });
		}
	}

	curtain_ = std::make_unique<CurtainTransition>();
	curtain_->Initialize();
	curtain_->StartOpen(20.0f);

	titleBgmHandle_ = GameAudioCache::LoadWave(kTitleBgmPath);
	selectSeHandle_ = GameAudioCache::LoadWave(kSelectSePath);
	decideSeHandle_ = GameAudioCache::LoadWave(kDecideSePath);
}

void TitleScene::InitializeCameraAndObjects()
{
	titleCamera_ = std::make_unique<Engine::CameraSystem::Camera>();
	UpdateCameraAnimation();
	Engine::CameraSystem::CameraManager::GetInstance()->AddCamera(kTitleCameraName, titleCamera_.get());
	Engine::CameraSystem::CameraManager::GetInstance()->SetActiveCamera(kTitleCameraName);

	GameModelCache::LoadBatch({
		"cube.obj",
		"skydome.obj",
		"plane.obj",
		"octopus.obj",
		});

	const ModelHandle titleModelHandle = GameModelCache::Load("cube.obj");
	titleObject_ = std::make_unique<Engine::Graphics3D::Object3D>();
	titleObject_->Initialize(Engine::Graphics3D::Object3DCommon::GetInstance());
	GameModelCache::ApplyToObject(*titleObject_, titleModelHandle);
	titleObject_->SetSkyboxFilePath(kEnvironmentTexturePath);
	titleObject_->SetEnvironmentReflectionStrength(0.0f);
	titleObject_->SetEnvironmentRoughness(1.0f);
	titleObject_->SetRotate({ 0.0f, -2.618f, 0.0f });
	titleObject_->SetScale(layoutSettings_.modelScale);
	titleObject_->SetTranslate(layoutSettings_.modelBasePosition);

	const ModelHandle skydomeHandle = GameModelCache::Load("skydome.obj");
	skyDomeObject_ = std::make_unique<Engine::Graphics3D::Object3D>();
	skyDomeObject_->Initialize(Engine::Graphics3D::Object3DCommon::GetInstance());
	GameModelCache::ApplyToObject(*skyDomeObject_, skydomeHandle);
	skyDomeObject_->SetSkyboxFilePath(kEnvironmentTexturePath);
	skyDomeObject_->SetEnvironmentReflectionStrength(0.0f);
	skyDomeObject_->SetEnvironmentRoughness(1.0f);
	skyDomeObject_->SetLighting(false);
	skyDomeObject_->SetCastsShadow(false);
	skyDomeObject_->SetScale({ 28.0f, 28.0f, 28.0f });
	skyDomeObject_->SetTranslate({ 0.0f, 0.0f, 0.0f });

	gridPlane_ = std::make_unique<GridPlane>();
	gridPlane_->Initialize();
	gridPlane_->Update(layoutSettings_.modelBasePosition);
}

void TitleScene::InitializeLighting()
{
	const UILayoutIO::LayoutMap tuning = UILayoutIO::LoadOrDefault(DataPaths::kDebugTuning, {});

	SceneLighting::Defaults defaults{};
	defaults.light.color = { 1.0f, 0.96f, 0.86f, 1.0f };
	defaults.light.direction = { -0.55f, -1.0f, -0.45f };
	defaults.light.intensity = 0.9f;
	defaults.light.ambientColor = { 0.48f, 0.52f, 0.62f, 1.0f };
	defaults.light.ambientIntensity = 0.34f;
	defaults.light.specularStrength = 0.16f;
	defaults.light.enable = 1;
	SceneLighting::ApplyDefaults(defaults);
	SceneLighting::Load(tuning, "title.");
}

void TitleScene::ApplyLayout()
{
	titleSprite_.SetPosition(layoutSettings_.titlePosition);
	titleSprite_.SetSize(layoutSettings_.titleSize);
	cursorPosition_ = layoutSettings_.cursorBasePosition;
	cursorSprite_.SetPosition(cursorPosition_);
	cursorSprite_.SetSize(layoutSettings_.cursorSize);

	if (titleObject_) {
		titleObject_->SetScale(layoutSettings_.modelScale);
		titleObject_->SetTranslate(layoutSettings_.modelBasePosition);
		titleObject_->Update();
	}
	UpdateCameraAnimation();
	if (gridPlane_) {
		gridPlane_->Update(layoutSettings_.modelBasePosition);
	}
	if (skyDomeObject_) {
		skyDomeObject_->Update();
	}
	if (Engine::CameraSystem::CameraManager::GetInstance()->GetActiveCamera()) {
		Engine::CameraSystem::CameraManager::GetInstance()->GetActiveCamera()->Update();
	}
}

void TitleScene::UpdateCurtain()
{
	if (curtain_) {
		curtain_->Update(kFixedDeltaTime);
	}
}

void TitleScene::UpdateNavigation()
{
	Engine::InputSystem::Input* input = Engine::InputSystem::Input::GetInstance();
	const GameMenuInputState menuInput = GameMenuController::Update(input, navigationInputDevice_);
	navigationInputDevice_ = menuInput.device;

	if (showingUpgradeScreen_) {
		cursorSprite_.SetColor({ 1.0f, 1.0f, 1.0f, 0.0f });
		if (menuInput.cancel) {
			showingUpgradeScreen_ = false;
			cursorSprite_.SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
		}
		return;
	}

	const bool mouseInsideScene = ScreenUtil::IsInsideDebugSceneViewport(input->GetMousePos());
	const Vector2 mousePosition = ScreenUtil::ToGamePosition(input->GetMousePos());

	int32_t hoveredMenuIndex = -1;
	if (mouseInsideScene) {
		for (int32_t index = 0; index < 3; ++index) {
			const Vector2 offset = index == 1
				? layoutSettings_.cursorShopOffset
				: (index == 2 ? layoutSettings_.cursorQuitOffset : Vector2{});
			const Vector2 rectPosition{
				layoutSettings_.menuHitboxPosition.x + offset.x,
				layoutSettings_.menuHitboxPosition.y + offset.y,
			};
			if (IsPointInRect(mousePosition, rectPosition, layoutSettings_.menuHitboxSize)) {
				hoveredMenuIndex = index;
				break;
			}
		}
	}

	const int32_t previousIndex = menuIndex_;
	switch (navigationInputDevice_) {
	case GameInputBindings::NavigationInputDevice::Mouse:
		if (hoveredMenuIndex >= 0) {
			menuIndex_ = hoveredMenuIndex;
		}
		break;
	case GameInputBindings::NavigationInputDevice::Keyboard:
	case GameInputBindings::NavigationInputDevice::Gamepad:
		if (GameInputBindings::IsMenuRightTriggered(input)) { menuIndex_ = 1; }
		if (GameInputBindings::IsMenuLeftTriggered(input)) { menuIndex_ = 0; }
		if (GameInputBindings::IsMenuDownTriggered(input)) { menuIndex_ = 2; }
		if (GameInputBindings::IsMenuUpTriggered(input)) { menuIndex_ = 0; }
		break;
	case GameInputBindings::NavigationInputDevice::None:
	default:
		break;
	}

	if (menuIndex_ != previousIndex && selectSeHandle_) {
		GameAudioCache::Play(selectSeHandle_);
		GameAudioCache::SetVolumeFromTuning(selectSeHandle_, kAudioTitleSelect, 1.0f);
	}

	const Vector2 cursorOffset = menuIndex_ == 1
		? layoutSettings_.cursorShopOffset
		: (menuIndex_ == 2 ? layoutSettings_.cursorQuitOffset : Vector2{});
	const Vector2 cursorTarget{
		layoutSettings_.cursorBasePosition.x + cursorOffset.x,
		layoutSettings_.cursorBasePosition.y + cursorOffset.y,
	};
	const float easing = 1.0f - std::exp(-layoutSettings_.cursorEasingSpeed * kFixedDeltaTime);
	cursorPosition_.x += (cursorTarget.x - cursorPosition_.x) * easing;
	cursorPosition_.y += (cursorTarget.y - cursorPosition_.y) * easing;
	cursorSprite_.SetPosition(cursorPosition_);

	const bool confirmTriggered =
		navigationInputDevice_ == GameInputBindings::NavigationInputDevice::Mouse
		? IsMouseMenuConfirm(hoveredMenuIndex)
		: menuInput.confirm;

	if (!confirmTriggered) {
		return;
	}

	if (decideSeHandle_) {
		GameAudioCache::Play(decideSeHandle_);
		GameAudioCache::SetVolumeFromTuning(decideSeHandle_, kAudioTitleDecide, 1.0f);
	}

	switch (menuIndex_) {
	case 0:
		StartGameTransition();
		break;
	case 1:
		showingUpgradeScreen_ = true;
		cursorSprite_.SetColor({ 1.0f, 1.0f, 1.0f, 0.0f });
		break;
	case 2:
		PostQuitMessage(0);
		break;
	default:
		break;
	}
}

void TitleScene::UpdatePermanentUpgradeInput()
{
	if (!sessionContext_ || curtainStarted_) {
		return;
	}

	Engine::InputSystem::Input* input = Engine::InputSystem::Input::GetInstance();
	if (!ScreenUtil::IsInsideDebugSceneViewport(input->GetMousePos())) {
		return;
	}

	const Vector2 mousePosition = ScreenUtil::ToGamePosition(input->GetMousePos());
	for (int32_t index = 0; index < kPermanentUpgradeCount; ++index) {
		if (!IsPointInRect(
			mousePosition,
			layoutSettings_.shopItemPositions[static_cast<size_t>(index)],
			layoutSettings_.shopItemHitboxSize)) {
			continue;
		}
		shopItemIndex_ = index;
		if (!input->TriggerMouse(0)) {
			break;
		}

		bool purchased = false;
		switch (kPermanentUpgradeDefinitions[static_cast<size_t>(index)].type) {
		case PermanentUpgradeType::MaxHP:
			purchased = sessionContext_->TryPurchasePermanentMaxHP();
			break;
		case PermanentUpgradeType::Attack:
			purchased = sessionContext_->TryPurchasePermanentAttack();
			break;
		case PermanentUpgradeType::MoveSpeed:
			purchased = sessionContext_->TryPurchasePermanentMoveSpeed();
			break;
		case PermanentUpgradeType::ExpPickupRange:
			purchased = sessionContext_->TryPurchasePermanentExpPickupRange();
			break;
		case PermanentUpgradeType::CoinGain:
			purchased = sessionContext_->TryPurchasePermanentCoinGain();
			break;
		default:
			break;
		}
		if (purchased) {
			permanentUpgradePurchaseFlashTimers_[static_cast<size_t>(index)] =
				kPermanentUpgradePurchaseFlashDuration;
		}
		if (purchased && decideSeHandle_) {
			GameAudioCache::Play(decideSeHandle_);
			GameAudioCache::SetVolumeFromTuning(
				decideSeHandle_,
				kAudioTitleDecide,
				1.0f);
		}
		break;
	}
}

void TitleScene::UpdateCharacterSelectionInput()
{
	if (!sessionContext_ || curtainStarted_) {
		return;
	}

	Engine::InputSystem::Input* input = Engine::InputSystem::Input::GetInstance();
	if (!input->TriggerMouse(0) ||
		!ScreenUtil::IsInsideDebugSceneViewport(input->GetMousePos())) {
		return;
	}

	const Vector2 mousePosition = ScreenUtil::ToGamePosition(input->GetMousePos());
	for (int32_t index = 0; index < kCharacterCount; ++index) {
		if (!IsPointInRect(
			mousePosition,
			CharacterIconPosition(index),
			kCharacterRowHitboxSize)) {
			continue;
		}

		const CharacterId id =
			kCharacterUiDefinitions[static_cast<size_t>(index)].id;
		const bool changed = sessionContext_->IsCharacterUnlocked(id)
			? sessionContext_->TrySelectCharacter(id)
			: sessionContext_->TryUnlockCharacter(id);
		if (changed && decideSeHandle_) {
			GameAudioCache::Play(decideSeHandle_);
			GameAudioCache::SetVolumeFromTuning(
				decideSeHandle_,
				kAudioTitleDecide,
				1.0f);
		}
		break;
	}
}

void TitleScene::UpdateAudio()
{
	if (!titleBgmHandle_) {
		return;
	}

	if (!GameAudioCache::IsPlaying(titleBgmHandle_)) {
		GameAudioCache::PlayLoop(titleBgmHandle_);
		GameAudioCache::SetVolumeFromTuning(titleBgmHandle_, kAudioTitleBgm, 0.1f);
	}
}

void TitleScene::UpdateModelAnimation()
{
	animationTime_ += kFixedDeltaTime;

	if (!titleObject_) {
		return;
	}

	titleObject_->SetRotate({ 0.0f, -2.618f, 0.0f });
	titleObject_->SetTranslate(layoutSettings_.modelBasePosition);
}

void TitleScene::UpdateCameraAnimation()
{
	if (!titleCamera_) {
		return;
	}

	Vector3 target = layoutSettings_.modelBasePosition;
	if (titleObject_) {
		target = titleObject_->GetTransform().translate;
	}
	target.x += layoutSettings_.cameraTarget.x;
	target.y += layoutSettings_.cameraTarget.y;
	target.z += layoutSettings_.cameraTarget.z;

	const float yaw = layoutSettings_.cameraYaw + animationTime_ * layoutSettings_.cameraOrbitSpeed;
	const Vector3 forward{
		std::sin(yaw),
		0.0f,
		std::cos(yaw),
	};

	titleCamera_->SetTranslate({
		target.x - forward.x * layoutSettings_.cameraDistance,
		target.y + layoutSettings_.cameraHeight,
		target.z - forward.z * layoutSettings_.cameraDistance,
		});
	titleCamera_->SetRotate({ layoutSettings_.cameraPitch, yaw, 0.0f });
	titleCamera_->SetFarClip(500.0f);
	titleCamera_->Update();

	Engine::CameraSystem::CameraManager* cameraManager = Engine::CameraSystem::CameraManager::GetInstance();
	if (cameraManager->GetCamera(kTitleCameraName) == titleCamera_.get()) {
		cameraManager->SetActiveCamera(kTitleCameraName);
	}
}

void TitleScene::DrawDebugUI()
{
	TitleSceneDebugUIController::Draw(*this);
}

void TitleScene::UpdateCoinDisplay()
{
	const int32_t ownedCoins = sessionContext_
		? std::clamp(sessionContext_->GetOwnedCoins(), 0, 999999)
		: 0;
	int32_t divisor = 100000;
	bool nonZeroSeen = false;
	for (int32_t index = 0; index < kCoinDigitCount; ++index) {
		if (!coinDigits_[index]) {
			divisor /= 10;
			continue;
		}
		const int32_t digit = divisor > 0 ? (ownedCoins / divisor) % 10 : 0;
		nonZeroSeen = nonZeroSeen || digit > 0 || index == kCoinDigitCount - 1;
		DigitSpriteUtil::SetDigitSprite(
			*coinDigits_[index],
			24.0f,
			{ 24.0f, 32.0f },
			digit);
		coinDigits_[index]->SetPosition({
			layoutSettings_.shopCoinPosition.x + layoutSettings_.shopCoinDigitStepX * static_cast<float>(index),
			layoutSettings_.shopCoinPosition.y });
		coinDigits_[index]->SetSize(layoutSettings_.shopCoinDigitSize);
		coinDigits_[index]->SetColor({
			1.0f,
			0.86f,
			0.22f,
			nonZeroSeen ? 1.0f : 0.0f });
		divisor /= 10;
	}
}

void TitleScene::UpdatePermanentUpgradeDisplay()
{
	const std::array<int32_t, kPermanentUpgradeCount> levels{
		sessionContext_ ? sessionContext_->GetPermanentMaxHPLevel() : 0,
		sessionContext_ ? sessionContext_->GetPermanentAttackLevel() : 0,
		sessionContext_ ? sessionContext_->GetPermanentMoveSpeedLevel() : 0,
		sessionContext_ ? sessionContext_->GetPermanentExpPickupRangeLevel() : 0,
		sessionContext_ ? sessionContext_->GetPermanentCoinGainLevel() : 0,
	};
	const std::array<int32_t, kPermanentUpgradeCount> costs{
		sessionContext_ ? sessionContext_->GetPermanentMaxHPCost() : 0,
		sessionContext_ ? sessionContext_->GetPermanentAttackCost() : 0,
		sessionContext_ ? sessionContext_->GetPermanentMoveSpeedCost() : 0,
		sessionContext_ ? sessionContext_->GetPermanentExpPickupRangeCost() : 0,
		sessionContext_ ? sessionContext_->GetPermanentCoinGainCost() : 0,
	};
	const int32_t ownedCoins = sessionContext_ ? sessionContext_->GetOwnedCoins() : 0;
	for (int32_t index = 0; index < kPermanentUpgradeCount; ++index) {
		float& purchaseFlashTimer =
			permanentUpgradePurchaseFlashTimers_[static_cast<size_t>(index)];
		purchaseFlashTimer = (std::max)(0.0f, purchaseFlashTimer - kFixedDeltaTime);
		const bool maxed = costs[index] <= 0;
		const bool affordable = !maxed && ownedCoins >= costs[index];
		const Vector4 color = PermanentUpgradeStatusColor(
			kPermanentUpgradeDefinitions[static_cast<size_t>(index)],
			maxed,
			affordable,
			purchaseFlashTimer);
		permanentUpgradeIcons_[index].SetColor(color);
		if (!permanentUpgradeLevelDigits_[index]) {
			continue;
		}
		const Vector2 iconPosition = PermanentUpgradeIconPosition(index);
		DigitSpriteUtil::SetDigitSprite(
			*permanentUpgradeLevelDigits_[index],
			24.0f,
			{ 24.0f, 32.0f },
			std::clamp(levels[index], 0, 9));
		permanentUpgradeLevelDigits_[index]->SetPosition({
			iconPosition.x + kPermanentUpgradeLevelOffset.x,
			iconPosition.y + kPermanentUpgradeLevelOffset.y,
			});
		permanentUpgradeLevelDigits_[index]->SetSize({ 20.0f, 26.0f });
		permanentUpgradeLevelDigits_[index]->SetColor(color);

		const int32_t displayCost = maxed ? 0 : std::clamp(costs[index], 0, 9999);
		int32_t divisor = 1000;
		bool nonZeroSeen = false;
		for (int32_t digitIndex = 0; digitIndex < kPermanentUpgradeCostDigitCount; ++digitIndex) {
			std::unique_ptr<Engine::Graphics2D::Sprite>& digit =
				permanentUpgradeCostDigits_[index][digitIndex];
			if (!digit) {
				divisor /= 10;
				continue;
			}
			const int32_t digitValue = divisor > 0 ? (displayCost / divisor) % 10 : 0;
			nonZeroSeen = nonZeroSeen ||
				digitValue > 0 ||
				digitIndex == kPermanentUpgradeCostDigitCount - 1;
			DigitSpriteUtil::SetDigitSprite(
				*digit,
				24.0f,
				{ 24.0f, 32.0f },
				digitValue);
			digit->SetPosition({
				iconPosition.x + kPermanentUpgradeCostOffset.x +
					kPermanentUpgradeCostDigitStepX * static_cast<float>(digitIndex),
				iconPosition.y + kPermanentUpgradeCostOffset.y,
				});
			digit->SetSize(kPermanentUpgradeCostDigitSize);
			digit->SetColor({
				maxed ? 0.45f : (affordable ? 1.0f : 0.7f),
				maxed ? 0.75f : (affordable ? 0.86f : 0.35f),
				maxed ? 1.0f : (affordable ? 0.22f : 0.35f),
				maxed ? 0.0f : (nonZeroSeen ? 0.95f : 0.0f),
				});
			divisor /= 10;
		}
	}
}

void TitleScene::UpdateCharacterSelectionDisplay()
{
	const CharacterId selectedId = sessionContext_
		? sessionContext_->GetSelectedCharacterId()
		: CharacterId::Octopus;
	const int32_t ownedCoins = sessionContext_ ? sessionContext_->GetOwnedCoins() : 0;

	for (int32_t index = 0; index < kCharacterCount; ++index) {
		const CharacterId id =
			kCharacterUiDefinitions[static_cast<size_t>(index)].id;
		const bool unlocked = sessionContext_ ? sessionContext_->IsCharacterUnlocked(id) : id == CharacterId::Octopus;
		const bool selected = unlocked && id == selectedId;
		const int32_t unlockCost = sessionContext_ ? sessionContext_->GetCharacterUnlockCost(id) : 0;
		const bool affordable = !unlocked && ownedCoins >= unlockCost;
		const Vector4 iconColor = selected
			? Vector4{ 1.0f, 1.0f, 1.0f, 1.0f }
			: (unlocked
				? Vector4{ 0.45f, 0.78f, 1.0f, 0.9f }
				: (affordable
					? Vector4{ 1.0f, 0.86f, 0.32f, 0.78f }
					: Vector4{ 0.36f, 0.36f, 0.36f, 0.62f }));
		characterIcons_[index].SetColor(iconColor);

		const Vector2 iconPosition = CharacterIconPosition(index);
		const int32_t displayCost = unlocked ? 0 : std::clamp(unlockCost, 0, 9999);
		int32_t divisor = 1000;
		bool nonZeroSeen = false;
		for (int32_t digitIndex = 0; digitIndex < kPermanentUpgradeCostDigitCount; ++digitIndex) {
			std::unique_ptr<Engine::Graphics2D::Sprite>& digit =
				characterCostDigits_[index][digitIndex];
			if (!digit) {
				divisor /= 10;
				continue;
			}
			const int32_t digitValue = divisor > 0 ? (displayCost / divisor) % 10 : 0;
			nonZeroSeen = nonZeroSeen ||
				digitValue > 0 ||
				digitIndex == kPermanentUpgradeCostDigitCount - 1;
			DigitSpriteUtil::SetDigitSprite(
				*digit,
				24.0f,
				{ 24.0f, 32.0f },
				digitValue);
			digit->SetPosition({
				iconPosition.x + kCharacterCostOffset.x +
					kPermanentUpgradeCostDigitStepX * static_cast<float>(digitIndex),
				iconPosition.y + kCharacterCostOffset.y,
				});
			digit->SetSize(kPermanentUpgradeCostDigitSize);
			digit->SetColor({
				affordable ? 1.0f : 0.7f,
				affordable ? 0.86f : 0.35f,
				affordable ? 0.22f : 0.35f,
				unlocked ? 0.0f : (nonZeroSeen ? 0.95f : 0.0f),
				});
			divisor /= 10;
		}
	}
}

void TitleScene::DrawCoinDisplay()
{
	for (const std::unique_ptr<Engine::Graphics2D::Sprite>& digit : coinDigits_) {
		if (!digit) {
			continue;
		}
		digit->Update();
		digit->Draw();
	}
}

void TitleScene::DrawPermanentUpgradeDisplay()
{
	for (UILabel& icon : permanentUpgradeIcons_) {
		icon.Draw();
	}
	for (const std::unique_ptr<Engine::Graphics2D::Sprite>& digit : permanentUpgradeLevelDigits_) {
		if (!digit) {
			continue;
		}
		digit->Update();
		digit->Draw();
	}
	for (const auto& costDigits : permanentUpgradeCostDigits_) {
		for (const std::unique_ptr<Engine::Graphics2D::Sprite>& digit : costDigits) {
			if (!digit) {
				continue;
			}
			digit->Update();
			digit->Draw();
		}
	}
}

void TitleScene::DrawCharacterSelectionDisplay()
{
	for (UILabel& icon : characterIcons_) {
		icon.Draw();
	}
	for (const auto& costDigits : characterCostDigits_) {
		for (const std::unique_ptr<Engine::Graphics2D::Sprite>& digit : costDigits) {
			if (!digit) {
				continue;
			}
			digit->Update();
			digit->Draw();
		}
	}
}

void TitleScene::UpdateShopLevelDisplay()
{
	const std::array<int32_t, kPermanentUpgradeCount> levels{
		sessionContext_ ? sessionContext_->GetPermanentMaxHPLevel() : 0,
		sessionContext_ ? sessionContext_->GetPermanentAttackLevel() : 0,
		sessionContext_ ? sessionContext_->GetPermanentMoveSpeedLevel() : 0,
		sessionContext_ ? sessionContext_->GetPermanentExpPickupRangeLevel() : 0,
		sessionContext_ ? sessionContext_->GetPermanentCoinGainLevel() : 0,
	};
	const std::array<int32_t, kPermanentUpgradeCount> costs{
		sessionContext_ ? sessionContext_->GetPermanentMaxHPCost() : 0,
		sessionContext_ ? sessionContext_->GetPermanentAttackCost() : 0,
		sessionContext_ ? sessionContext_->GetPermanentMoveSpeedCost() : 0,
		sessionContext_ ? sessionContext_->GetPermanentExpPickupRangeCost() : 0,
		sessionContext_ ? sessionContext_->GetPermanentCoinGainCost() : 0,
	};
	const int32_t ownedCoins = sessionContext_ ? sessionContext_->GetOwnedCoins() : 0;
	for (int32_t itemIndex = 0; itemIndex < kPermanentUpgradeCount; ++itemIndex) {
		const int32_t cap = kShopLevelCaps[static_cast<size_t>(itemIndex)];
		const Vector2 itemPosition = layoutSettings_.shopItemPositions[static_cast<size_t>(itemIndex)];
		const float rowWidth = layoutSettings_.shopLevelSquareSize.x +
			layoutSettings_.shopLevelSquareStepX * static_cast<float>(cap - 1);
		const float squareStartX = itemPosition.x +
			(layoutSettings_.shopItemHitboxSize.x - rowWidth) * 0.5f;
		UIPanel& highlight = shopHighlights_[static_cast<size_t>(itemIndex)];
		highlight.SetPosition(itemPosition);
		highlight.SetSize(layoutSettings_.shopItemHitboxSize);
		highlight.SetColor(itemIndex == shopItemIndex_
			? Vector4{ 1.0f, 0.88f, 0.12f, 0.22f }
			: Vector4{ 0.0f, 0.0f, 0.0f, 0.0f });
		for (int32_t levelIndex = 0; levelIndex < kShopMaxLevelSlots; ++levelIndex) {
			UIPanel& square = shopLevelSquares_[static_cast<size_t>(itemIndex)][static_cast<size_t>(levelIndex)];
			square.SetPosition({
				squareStartX + layoutSettings_.shopLevelSquareStepX * static_cast<float>(levelIndex),
				itemPosition.y + layoutSettings_.shopLevelSquareOffsetY,
			});
			square.SetSize(layoutSettings_.shopLevelSquareSize);
			square.SetVisible(levelIndex < cap);
			square.SetColor(levelIndex < levels[static_cast<size_t>(itemIndex)]
				? Vector4{ 0.12f, 0.42f, 1.0f, 1.0f }
				: Vector4{ 0.95f, 0.12f, 0.12f, 1.0f });
		}

		const bool maxed = costs[static_cast<size_t>(itemIndex)] <= 0;
		const int32_t displayCost = std::clamp(costs[static_cast<size_t>(itemIndex)], 0, 9999);
		int32_t divisor = 1000;
		bool nonZeroSeen = false;
		for (int32_t digitIndex = 0; digitIndex < kPermanentUpgradeCostDigitCount; ++digitIndex) {
			auto& digit = permanentUpgradeCostDigits_[static_cast<size_t>(itemIndex)][static_cast<size_t>(digitIndex)];
			if (!digit) {
				divisor /= 10;
				continue;
			}
			const int32_t value = divisor > 0 ? (displayCost / divisor) % 10 : 0;
			nonZeroSeen = nonZeroSeen || value > 0 || digitIndex == kPermanentUpgradeCostDigitCount - 1;
			DigitSpriteUtil::SetDigitSprite(*digit, 24.0f, { 24.0f, 32.0f }, value);
			digit->SetPosition({
				itemPosition.x + layoutSettings_.shopPriceOffset.x +
					layoutSettings_.shopPriceDigitStepX * static_cast<float>(digitIndex),
				itemPosition.y + layoutSettings_.shopPriceOffset.y,
			});
			digit->SetSize(layoutSettings_.shopPriceDigitSize);
			const bool affordable = ownedCoins >= displayCost;
			digit->SetColor({
				affordable ? 1.0f : 0.7f,
				affordable ? 0.86f : 0.3f,
				affordable ? 0.2f : 0.3f,
				maxed ? 0.0f : (nonZeroSeen ? 1.0f : 0.0f),
			});
			divisor /= 10;
		}
	}
}

void TitleScene::DrawShopDisplay()
{
	for (UIPanel& highlight : shopHighlights_) {
		highlight.Draw();
	}
	for (size_t itemIndex = 0; itemIndex < shopLevelSquares_.size(); ++itemIndex) {
		auto& itemSquares = shopLevelSquares_[itemIndex];
		for (UIPanel& square : itemSquares) {
			square.Draw();
		}
		for (auto& digit : permanentUpgradeCostDigits_[itemIndex]) {
			if (digit) {
				digit->Update();
				digit->Draw();
			}
		}
	}
}

void TitleScene::SaveLayout() const
{
	UILayoutIO::Save(DataPaths::kTitleLayout,
		{
			{ "titlePosition", { layoutSettings_.titlePosition.x, layoutSettings_.titlePosition.y } },
			{ "titleSize", { layoutSettings_.titleSize.x, layoutSettings_.titleSize.y } },
			{ "cursorBasePosition", { layoutSettings_.cursorBasePosition.x, layoutSettings_.cursorBasePosition.y } },
			{ "cursorSize", { layoutSettings_.cursorSize.x, layoutSettings_.cursorSize.y } },
			{ "cursorShopOffset", { layoutSettings_.cursorShopOffset.x, layoutSettings_.cursorShopOffset.y } },
			{ "cursorQuitOffset", { layoutSettings_.cursorQuitOffset.x, layoutSettings_.cursorQuitOffset.y } },
			{ "cursorEasingSpeed", { layoutSettings_.cursorEasingSpeed } },
			{ "menuHitboxPosition", { layoutSettings_.menuHitboxPosition.x, layoutSettings_.menuHitboxPosition.y } },
			{ "menuHitboxSize", { layoutSettings_.menuHitboxSize.x, layoutSettings_.menuHitboxSize.y } },
			{ "shopItemPosition0", { layoutSettings_.shopItemPositions[0].x, layoutSettings_.shopItemPositions[0].y } },
			{ "shopItemPosition1", { layoutSettings_.shopItemPositions[1].x, layoutSettings_.shopItemPositions[1].y } },
			{ "shopItemPosition2", { layoutSettings_.shopItemPositions[2].x, layoutSettings_.shopItemPositions[2].y } },
			{ "shopItemPosition3", { layoutSettings_.shopItemPositions[3].x, layoutSettings_.shopItemPositions[3].y } },
			{ "shopItemPosition4", { layoutSettings_.shopItemPositions[4].x, layoutSettings_.shopItemPositions[4].y } },
			{ "shopItemHitboxSize", { layoutSettings_.shopItemHitboxSize.x, layoutSettings_.shopItemHitboxSize.y } },
			{ "shopLevelSquareSize", { layoutSettings_.shopLevelSquareSize.x, layoutSettings_.shopLevelSquareSize.y } },
			{ "shopLevelSquareStepX", { layoutSettings_.shopLevelSquareStepX } },
			{ "shopLevelSquareOffsetY", { layoutSettings_.shopLevelSquareOffsetY } },
			{ "shopPriceOffset", { layoutSettings_.shopPriceOffset.x, layoutSettings_.shopPriceOffset.y } },
			{ "shopPriceDigitSize", { layoutSettings_.shopPriceDigitSize.x, layoutSettings_.shopPriceDigitSize.y } },
			{ "shopPriceDigitStepX", { layoutSettings_.shopPriceDigitStepX } },
			{ "shopCoinPosition", { layoutSettings_.shopCoinPosition.x, layoutSettings_.shopCoinPosition.y } },
			{ "shopCoinDigitSize", { layoutSettings_.shopCoinDigitSize.x, layoutSettings_.shopCoinDigitSize.y } },
			{ "shopCoinDigitStepX", { layoutSettings_.shopCoinDigitStepX } },
			{ "modelBasePosition", { layoutSettings_.modelBasePosition.x, layoutSettings_.modelBasePosition.y, layoutSettings_.modelBasePosition.z } },
			{ "modelScale", { layoutSettings_.modelScale.x, layoutSettings_.modelScale.y, layoutSettings_.modelScale.z } },
			{ "cameraTarget", { layoutSettings_.cameraTarget.x, layoutSettings_.cameraTarget.y, layoutSettings_.cameraTarget.z } },
			{ "cameraDistance", { layoutSettings_.cameraDistance } },
			{ "cameraHeight", { layoutSettings_.cameraHeight } },
			{ "cameraPitch", { layoutSettings_.cameraPitch } },
			{ "cameraYaw", { layoutSettings_.cameraYaw } },
			{ "cameraOrbitSpeed", { layoutSettings_.cameraOrbitSpeed } },
#ifdef _DEBUG
			{ "debug.windowSwitcher", { debugWindows_.windowSwitcher ? 1.0f : 0.0f } },
			{ "debug.titleView", { debugWindows_.titleView ? 1.0f : 0.0f } },
			{ "debug.statisticsView", { debugWindows_.statisticsView ? 1.0f : 0.0f } },
			{ "debug.titleSettings", { debugWindows_.titleSettings ? 1.0f : 0.0f } },
			{ "debug.offscreenSettings", { debugWindows_.offscreenSettings ? 1.0f : 0.0f } },
			{ "debug.lightSettings", { debugWindows_.lightSettings ? 1.0f : 0.0f } },
			{ "debug.audio", { debugWindows_.audio ? 1.0f : 0.0f } },
			{ "debug.keyInputDebug", { debugWindows_.keyInputDebug ? 1.0f : 0.0f } },
#endif
		});
}

void TitleScene::ReloadDebugData()
{
	const UILayoutIO::LayoutMap titleLayout = UILayoutIO::LoadOrDefault(DataPaths::kTitleLayout, {});
	layoutSettings_.titlePosition = UILayoutIO::GetVector2(titleLayout, "titlePosition", layoutSettings_.titlePosition);
	layoutSettings_.titleSize = UILayoutIO::GetVector2(titleLayout, "titleSize", layoutSettings_.titleSize);
	layoutSettings_.cursorBasePosition = UILayoutIO::GetVector2(titleLayout, "cursorBasePosition", layoutSettings_.cursorBasePosition);
	layoutSettings_.cursorSize = UILayoutIO::GetVector2(titleLayout, "cursorSize", layoutSettings_.cursorSize);
	layoutSettings_.cursorShopOffset = UILayoutIO::GetVector2(titleLayout, "cursorShopOffset", layoutSettings_.cursorShopOffset);
	layoutSettings_.cursorQuitOffset = UILayoutIO::GetVector2(titleLayout, "cursorQuitOffset", layoutSettings_.cursorQuitOffset);
	layoutSettings_.cursorEasingSpeed = UILayoutIO::GetFloat(titleLayout, "cursorEasingSpeed", layoutSettings_.cursorEasingSpeed);
	layoutSettings_.menuHitboxPosition = UILayoutIO::GetVector2(titleLayout, "menuHitboxPosition", layoutSettings_.menuHitboxPosition);
	layoutSettings_.menuHitboxSize = UILayoutIO::GetVector2(titleLayout, "menuHitboxSize", layoutSettings_.menuHitboxSize);
	for (int32_t index = 0; index < kPermanentUpgradeCount; ++index) {
		layoutSettings_.shopItemPositions[static_cast<size_t>(index)] = UILayoutIO::GetVector2(
			titleLayout,
			"shopItemPosition" + std::to_string(index),
			layoutSettings_.shopItemPositions[static_cast<size_t>(index)]);
	}
	layoutSettings_.shopItemHitboxSize = UILayoutIO::GetVector2(titleLayout, "shopItemHitboxSize", layoutSettings_.shopItemHitboxSize);
	layoutSettings_.shopLevelSquareSize = UILayoutIO::GetVector2(titleLayout, "shopLevelSquareSize", layoutSettings_.shopLevelSquareSize);
	layoutSettings_.shopLevelSquareStepX = UILayoutIO::GetFloat(titleLayout, "shopLevelSquareStepX", layoutSettings_.shopLevelSquareStepX);
	layoutSettings_.shopLevelSquareOffsetY = UILayoutIO::GetFloat(titleLayout, "shopLevelSquareOffsetY", layoutSettings_.shopLevelSquareOffsetY);
	layoutSettings_.shopPriceOffset = UILayoutIO::GetVector2(titleLayout, "shopPriceOffset", layoutSettings_.shopPriceOffset);
	layoutSettings_.shopPriceDigitSize = UILayoutIO::GetVector2(titleLayout, "shopPriceDigitSize", layoutSettings_.shopPriceDigitSize);
	layoutSettings_.shopPriceDigitStepX = UILayoutIO::GetFloat(titleLayout, "shopPriceDigitStepX", layoutSettings_.shopPriceDigitStepX);
	layoutSettings_.shopCoinPosition = UILayoutIO::GetVector2(titleLayout, "shopCoinPosition", layoutSettings_.shopCoinPosition);
	layoutSettings_.shopCoinDigitSize = UILayoutIO::GetVector2(titleLayout, "shopCoinDigitSize", layoutSettings_.shopCoinDigitSize);
	layoutSettings_.shopCoinDigitStepX = UILayoutIO::GetFloat(titleLayout, "shopCoinDigitStepX", layoutSettings_.shopCoinDigitStepX);
	layoutSettings_.modelBasePosition = UILayoutIO::GetVector3(titleLayout, "modelBasePosition", layoutSettings_.modelBasePosition);
	layoutSettings_.modelScale = UILayoutIO::GetVector3(titleLayout, "modelScale", layoutSettings_.modelScale);
	layoutSettings_.cameraTarget = UILayoutIO::GetVector3(titleLayout, "cameraTarget", layoutSettings_.cameraTarget);
	layoutSettings_.cameraDistance = UILayoutIO::GetFloat(titleLayout, "cameraDistance", layoutSettings_.cameraDistance);
	layoutSettings_.cameraHeight = UILayoutIO::GetFloat(titleLayout, "cameraHeight", layoutSettings_.cameraHeight);
	layoutSettings_.cameraPitch = UILayoutIO::GetFloat(titleLayout, "cameraPitch", layoutSettings_.cameraPitch);
	layoutSettings_.cameraYaw = UILayoutIO::GetFloat(titleLayout, "cameraYaw", layoutSettings_.cameraYaw);
	layoutSettings_.cameraOrbitSpeed = UILayoutIO::GetFloat(titleLayout, "cameraOrbitSpeed", layoutSettings_.cameraOrbitSpeed);
#ifdef _DEBUG
	auto loadWindowVisible = [&titleLayout](std::string_view key, bool fallback) {
		return UILayoutIO::GetFloat(titleLayout, key, fallback ? 1.0f : 0.0f) != 0.0f;
	};
	debugWindows_.windowSwitcher = loadWindowVisible("debug.windowSwitcher", debugWindows_.windowSwitcher);
	debugWindows_.titleView = loadWindowVisible("debug.titleView", debugWindows_.titleView);
	debugWindows_.statisticsView = loadWindowVisible("debug.statisticsView", debugWindows_.statisticsView);
	debugWindows_.titleSettings = loadWindowVisible("debug.titleSettings", debugWindows_.titleSettings);
	debugWindows_.offscreenSettings = loadWindowVisible("debug.offscreenSettings", debugWindows_.offscreenSettings);
	debugWindows_.lightSettings = loadWindowVisible("debug.lightSettings", debugWindows_.lightSettings);
	debugWindows_.audio = loadWindowVisible("debug.audio", debugWindows_.audio);
	debugWindows_.keyInputDebug = loadWindowVisible("debug.keyInputDebug", debugWindows_.keyInputDebug);
#endif
	InitializeLighting();
	ApplyLayout();
}

bool TitleScene::IsMouseMenuConfirm(int32_t hoveredMenuIndex) const
{
	return hoveredMenuIndex >= 0 && GameInputBindings::IsMouseConfirmTriggered(Engine::InputSystem::Input::GetInstance());
}

void TitleScene::StartGameTransition()
{
	if (curtain_ && curtain_->GetState() == CurtainTransition::State::None) {
		curtain_->StartClose();
		curtainStarted_ = true;
		if (sessionContext_) {
			sessionContext_->BeginNewRun();
		}
	}
}

}
