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
#include <memory>
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
constexpr char kSelectTexturePath[] = "ui/title/select.png";
constexpr char kNumberTexturePath[] = "ui/number/numbers.png";
constexpr char kTextFontTexture[] = "ui/font/noto_sans_jp_black.png";
constexpr char kTextFontMetadata[] = "ui/font/noto_sans_jp_black.json";
constexpr char kPermanentMaxHPIconPath[] = "ui/game/lvup/maxhp_icon.png";
constexpr char kPermanentAttackIconPath[] = "ui/game/lvup/attack_icon.png";
constexpr char kPermanentMoveSpeedIconPath[] = "ui/game/lvup/speed_icon.png";
constexpr char kPermanentExpPickupRangeIconPath[] = "ui/game/lvup/heal_icon.png";
constexpr char kPermanentCoinGainIconPath[] = "ui/game/lvup/normal_icon.png";
constexpr char kTitleBgmPath[] = "bgm/title_loop.wav";
constexpr char kSelectSePath[] = "se/ui_select.wav";
constexpr char kDecideSePath[] = "se/ui_decide.wav";
constexpr char kBackSePath[] = "se/ui_back.wav";
constexpr char kAudioTitleBgm[] = "title.bgm";
constexpr char kAudioUiSelect[] = "ui.select";
constexpr char kAudioUiDecide[] = "ui.decide";
constexpr char kAudioUiBack[] = "ui.back";
constexpr char kEnvironmentTexturePath[] = "Resources/textures/skybox/test.dds";
constexpr char kTitleCameraName[] = "directxgame_title";
constexpr char kTitlePlayerModelPath[] = "cube_world/cube_guy.glb";
constexpr float kTitlePlayerModelScaleMultiplier = 0.68f;
constexpr float kTitlePlayerModelBasePitch = 0.0f;
constexpr float kTitleIdleAnimationSpeed = 0.45f;
constexpr int kOpenCharacterSelectKey = DIK_C;
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
constexpr Vector2 kStartCharacterIconBasePosition{ 430.0f, 300.0f };
constexpr Vector2 kStartCharacterIconSize{ 96.0f, 96.0f };
constexpr Vector2 kStartCharacterHighlightPadding{ 10.0f, 10.0f };
constexpr float kStartCharacterIconStepX = 160.0f;

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
	const char* weaponIconPath;
	const char* modelPath;
	const char* weaponModelPath;
	const char* weaponDescription;
	Vector4 modelColor;
};

constexpr std::array<CharacterUiDefinition, 4> kCharacterUiDefinitions{ {
	{
		DirectXGame::CharacterId::Default,
		"ui/game/lvup/icon_common_unknown.png",
		"ui/game/lvup/icon_weapon_bow_arrow.png",
		"quaternius_characters/adventurer.glb",
		"quaternius_weapons/bow.glb",
		"標準的な初期装備",
		{ 1.0f, 1.0f, 1.0f, 1.0f },
	},
	{
		DirectXGame::CharacterId::Bow,
		"ui/game/lvup/icon_weapon_bow_arrow.png",
		"ui/game/lvup/icon_weapon_bow_arrow.png",
		"quaternius_characters/adventurer.glb",
		"quaternius_weapons/bow.glb",
		"遠距離から矢を放つ",
		{ 1.0f, 1.0f, 1.0f, 1.0f },
	},
	{
		DirectXGame::CharacterId::Sword,
		"ui/game/lvup/icon_weapon_sword.png",
		"ui/game/lvup/icon_weapon_sword.png",
		"quaternius_characters/king.glb",
		"quaternius_weapons/sword.glb",
		"近距離で前方を斬る",
		{ 1.0f, 1.0f, 1.0f, 1.0f },
	},
	{
		DirectXGame::CharacterId::Handgun,
		"ui/game/lvup/icon_weapon_handgun.png",
		"ui/game/lvup/icon_weapon_handgun.png",
		"quaternius_characters/swat.glb",
		"quaternius_weapons/pistol.glb",
		"素早く弾を撃つ",
		{ 1.0f, 1.0f, 1.0f, 1.0f },
	},
} };

const CharacterUiDefinition& CharacterUiForId(DirectXGame::CharacterId id)
{
	for (const CharacterUiDefinition& definition : kCharacterUiDefinitions) {
		if (definition.id == id) {
			return definition;
		}
	}
	return kCharacterUiDefinitions[0];
}

Vector3 TitlePlayerModelScale(const Vector3& layoutScale)
{
	return {
		layoutScale.x * kTitlePlayerModelScaleMultiplier,
		layoutScale.y * kTitlePlayerModelScaleMultiplier,
		layoutScale.z * kTitlePlayerModelScaleMultiplier,
	};
}

std::unique_ptr<Engine::Graphics3D::Object3D> CreateTitlePreviewObject(
	const char* modelPath,
	const Vector4& color)
{
	std::unique_ptr<Engine::Graphics3D::Object3D> object =
		std::make_unique<Engine::Graphics3D::Object3D>();
	object->Initialize(Engine::Graphics3D::Object3DCommon::GetInstance());
	DirectXGame::GameModelCache::ApplyToObject(
		*object,
		DirectXGame::GameModelCache::Load(modelPath));
	object->SetSkyboxFilePath(kEnvironmentTexturePath);
	object->SetEnvironmentReflectionStrength(0.0f);
	object->SetEnvironmentRoughness(1.0f);
	object->SetCastsShadow(true);
	object->SetColor(color);
	return object;
}

void ApplyPreviewTransform(
	Engine::Graphics3D::Object3D& object,
	const Vector3& position,
	const Vector3& scale,
	const Vector3& rotation,
	const Vector4& color)
{
	object.SetTranslate(position);
	object.SetScale(scale);
	object.SetRotate(rotation);
	object.SetColor(color);
	object.Update();
}

void ApplyIdleAnimation(Engine::Graphics3D::Object3D& object)
{
	const char* idleClip = object.HasAnimationClip("idle")
		? "idle"
		: "idle_hold";
	object.SetAnimationClip(idleClip);
	object.SetAnimationSpeed(kTitleIdleAnimationSpeed);
	object.SetAnimationLoop(true);
}

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

Vector2 StartCharacterIconPosition(int32_t index)
{
	return {
		kStartCharacterIconBasePosition.x +
			kStartCharacterIconStepX * static_cast<float>(index),
		kStartCharacterIconBasePosition.y,
	};
}

DirectXGame::CharacterId StartCharacterIdFromIndex(int32_t index)
{
	switch (index) {
	case 0: return DirectXGame::CharacterId::Bow;
	case 1: return DirectXGame::CharacterId::Sword;
	case 2: return DirectXGame::CharacterId::Handgun;
	default: return DirectXGame::CharacterId::Bow;
	}
}

int32_t StartCharacterIndexFromId(DirectXGame::CharacterId id)
{
	switch (id) {
	case DirectXGame::CharacterId::Sword: return 1;
	case DirectXGame::CharacterId::Handgun: return 2;
	case DirectXGame::CharacterId::Bow:
	default:
		return 0;
	}
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
	GameAudioCache::StopBus(AudioBus::Bgm);
	Engine::CameraSystem::CameraManager::GetInstance()->Initialize();
	if (Engine::Base::OffscreenRenderManager* offscreen = Engine::Base::OffscreenRenderManager::GetInstance()) {
		offscreen->SetScenePostEffectType(PostEffectType::Fullscreen);
	}

	if (sessionContext_) {
		sessionContext_->OnEnterTitleScene();
		sessionContext_->TrySelectCharacter(CharacterId::Bow);
	}

	InitializeLighting();
	InitializeResources();
	InitializeCameraAndObjects();
	ResourceProbe::Verify();
	ApplyLayout();
}

void TitleScene::Finalize()
{
	GameAudioCache::StopBus(AudioBus::Bgm);
	Engine::CameraSystem::CameraManager::GetInstance()->RemoveCamera(kTitleCameraName);
}

void TitleScene::Update()
{
	if (sessionContext_ && sessionContext_->IsSceneStressEnabled()) {
		sessionContext_->AdvanceSceneStressFrame();
		if (sessionContext_->GetRunCount() == 0 &&
			sessionContext_->GetSceneStressFrameCount() == 1) {
			Engine::Graphics3D::Object3DCommon* objectCommon =
				Engine::Graphics3D::Object3DCommon::GetInstance();
			const std::shared_ptr<Engine::Base::DirectXCommon> dxCommon =
				objectCommon->GetDxCommon();
			dxCommon->ResetGpuTimingStatistics();
			objectCommon->ResetShadowPassStatistics();
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
			GameAudioCache::StopBus(AudioBus::Bgm);
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
	if (awaitingCharacterSelect_) {
		UpdateStartCharacterSelectionInput();
	}
	if (showingUpgradeScreen_) {
		UpdatePermanentUpgradeInput();
	}
	UpdateModelAnimation();
	UpdateCameraAnimation();
	UpdateCoinDisplay();
	UpdateCharacterSelectionDisplay();
	UpdateStartCharacterSelectionDisplay();
	UpdateShopLevelDisplay();

	if (titleObject_) {
		titleObject_->Update();
	}
	if (startCharacterModel_) {
		UpdateStartCharacterModel();
		UpdateStartCharacterPreviewModels();
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
	Engine::Graphics3D::Object3D::ClearSubmittedDraws();
	Engine::Graphics3D::Object3D::ClearSubmittedShadows();
	const bool overlayScreenActive = showingUpgradeScreen_ || awaitingCharacterSelect_;
	const bool drawSelectModel = false;
	const Vector3 shadowOrigin = drawSelectModel
		? layoutSettings_.selectModelPosition
		: layoutSettings_.modelBasePosition;
	if (objectCommon->BeginShadowPass(shadowOrigin)) {
		if (!overlayScreenActive && titleObject_) {
			Engine::Graphics3D::Object3D::SubmitForShadow(titleObject_.get());
		}
		if (drawSelectModel) {
			Engine::Graphics3D::Object3D::SubmitForShadow(startCharacterModel_.get());
			if (startCharacterWeaponModel_) {
				Engine::Graphics3D::Object3D::SubmitForShadow(startCharacterWeaponModel_.get());
			}
			if (startCharacterDetailModel_) {
				Engine::Graphics3D::Object3D::SubmitForShadow(startCharacterDetailModel_.get());
			}
			if (startCharacterDetailWeaponModel_) {
				Engine::Graphics3D::Object3D::SubmitForShadow(startCharacterDetailWeaponModel_.get());
			}
			for (const auto& model : startCharacterPreviewModels_) {
				if (model) {
					Engine::Graphics3D::Object3D::SubmitForShadow(model.get());
				}
			}
			for (const auto& model : startCharacterPreviewWeaponModels_) {
				if (model) {
					Engine::Graphics3D::Object3D::SubmitForShadow(model.get());
				}
			}
		}
		if (gridPlane_) {
			gridPlane_->DrawShadow();
		}
		Engine::Graphics3D::Object3D::FlushSubmittedShadows();
		objectCommon->EndShadowPass();
	}

	objectCommon->CommonDraw();
	if (!overlayScreenActive && skyDomeObject_) {
		Engine::Graphics3D::Object3D::SubmitForDraw(skyDomeObject_.get());
	}
	if (gridPlane_) {
		gridPlane_->Draw();
	}
	if (!overlayScreenActive && titleObject_) {
		Engine::Graphics3D::Object3D::SubmitForDraw(titleObject_.get());
	}
	if (drawSelectModel) {
		for (const auto& model : startCharacterPreviewModels_) {
			if (model) {
				Engine::Graphics3D::Object3D::SubmitForDraw(model.get());
			}
		}
		for (const auto& model : startCharacterPreviewWeaponModels_) {
			if (model) {
				Engine::Graphics3D::Object3D::SubmitForDraw(model.get());
			}
		}
		Engine::Graphics3D::Object3D::SubmitForDraw(startCharacterModel_.get());
		if (startCharacterWeaponModel_) {
			Engine::Graphics3D::Object3D::SubmitForDraw(startCharacterWeaponModel_.get());
		}
		if (startCharacterDetailModel_) {
			Engine::Graphics3D::Object3D::SubmitForDraw(startCharacterDetailModel_.get());
		}
		if (startCharacterDetailWeaponModel_) {
			Engine::Graphics3D::Object3D::SubmitForDraw(startCharacterDetailWeaponModel_.get());
		}
	}
	Engine::Graphics3D::Object3D::FlushSubmittedDraws();

	Engine::Graphics2D::SpriteCommon::GetInstance()->CommonDraw();
	if (!overlayScreenActive) {
		titleSprite_.Draw();
	}
	if (awaitingCharacterSelect_) {
		DrawStartCharacterSelectionDisplay();
	}
	if (showingUpgradeScreen_) {
		shopSprite_.Draw();
	}
	if (!awaitingCharacterSelect_) {
		cursorSprite_.Draw();
	}
	if (showingUpgradeScreen_) {
		DrawShopDisplay();
		DrawCharacterSelectionDisplay();
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
	layoutSettings_.selectBackgroundPosition = UILayoutIO::GetVector2(titleLayout, "select.backgroundPosition", layoutSettings_.selectBackgroundPosition);
	layoutSettings_.selectBackgroundSize = UILayoutIO::GetVector2(titleLayout, "select.backgroundSize", layoutSettings_.selectBackgroundSize);
	layoutSettings_.selectTitleTextPosition = UILayoutIO::GetVector2(titleLayout, "select.titleTextPosition", layoutSettings_.selectTitleTextPosition);
	layoutSettings_.selectTitleTextScale = UILayoutIO::GetFloat(titleLayout, "select.titleTextScale", layoutSettings_.selectTitleTextScale);
	layoutSettings_.selectIconBasePosition = UILayoutIO::GetVector2(titleLayout, "select.iconBasePosition", layoutSettings_.selectIconBasePosition);
	layoutSettings_.selectIconSize = UILayoutIO::GetVector2(titleLayout, "select.iconSize", layoutSettings_.selectIconSize);
	layoutSettings_.selectIconHitboxSize = UILayoutIO::GetVector2(titleLayout, "select.iconHitboxSize", layoutSettings_.selectIconHitboxSize);
	layoutSettings_.selectIconStepX = UILayoutIO::GetFloat(titleLayout, "select.iconStepX", layoutSettings_.selectIconStepX);
	layoutSettings_.selectIconStepY = UILayoutIO::GetFloat(titleLayout, "select.iconStepY", layoutSettings_.selectIconStepY);
	layoutSettings_.selectIconModelBasePosition = UILayoutIO::GetVector3(titleLayout, "select.iconModelBasePosition", layoutSettings_.selectIconModelBasePosition);
	layoutSettings_.selectIconModelStep = UILayoutIO::GetVector3(titleLayout, "select.iconModelStep", layoutSettings_.selectIconModelStep);
	layoutSettings_.selectIconModelScale = UILayoutIO::GetVector3(titleLayout, "select.iconModelScale", layoutSettings_.selectIconModelScale);
	layoutSettings_.selectIconModelRotation = UILayoutIO::GetVector3(titleLayout, "select.iconModelRotation", layoutSettings_.selectIconModelRotation);
	layoutSettings_.selectIconWeaponOffset = UILayoutIO::GetVector3(titleLayout, "select.iconWeaponOffset", layoutSettings_.selectIconWeaponOffset);
	layoutSettings_.selectIconWeaponScale = UILayoutIO::GetVector3(titleLayout, "select.iconWeaponScale", layoutSettings_.selectIconWeaponScale);
	layoutSettings_.selectIconWeaponRotation = UILayoutIO::GetVector3(titleLayout, "select.iconWeaponRotation", layoutSettings_.selectIconWeaponRotation);
	layoutSettings_.selectDetailFacePosition = UILayoutIO::GetVector2(titleLayout, "select.detailFacePosition", layoutSettings_.selectDetailFacePosition);
	layoutSettings_.selectDetailFaceSize = UILayoutIO::GetVector2(titleLayout, "select.detailFaceSize", layoutSettings_.selectDetailFaceSize);
	layoutSettings_.selectWeaponIconPosition = UILayoutIO::GetVector2(titleLayout, "select.weaponIconPosition", layoutSettings_.selectWeaponIconPosition);
	layoutSettings_.selectWeaponIconSize = UILayoutIO::GetVector2(titleLayout, "select.weaponIconSize", layoutSettings_.selectWeaponIconSize);
	layoutSettings_.selectWeaponDescriptionPosition = UILayoutIO::GetVector2(titleLayout, "select.weaponDescriptionPosition", layoutSettings_.selectWeaponDescriptionPosition);
	layoutSettings_.selectWeaponDescriptionScale = UILayoutIO::GetFloat(titleLayout, "select.weaponDescriptionScale", layoutSettings_.selectWeaponDescriptionScale);
	layoutSettings_.selectWeaponDescriptionMaxWidth = UILayoutIO::GetFloat(titleLayout, "select.weaponDescriptionMaxWidth", layoutSettings_.selectWeaponDescriptionMaxWidth);
	layoutSettings_.selectActionButtonPosition = UILayoutIO::GetVector2(titleLayout, "select.actionButtonPosition", layoutSettings_.selectActionButtonPosition);
	layoutSettings_.selectActionButtonSize = UILayoutIO::GetVector2(titleLayout, "select.actionButtonSize", layoutSettings_.selectActionButtonSize);
	layoutSettings_.selectActionTextOffset = UILayoutIO::GetVector2(titleLayout, "select.actionTextOffset", layoutSettings_.selectActionTextOffset);
	layoutSettings_.selectActionTextScale = UILayoutIO::GetFloat(titleLayout, "select.actionTextScale", layoutSettings_.selectActionTextScale);
	layoutSettings_.selectModelPosition = UILayoutIO::GetVector3(titleLayout, "select.modelPosition", layoutSettings_.selectModelPosition);
	layoutSettings_.selectModelScale = UILayoutIO::GetVector3(titleLayout, "select.modelScale", layoutSettings_.selectModelScale);
	layoutSettings_.selectModelRotation = UILayoutIO::GetVector3(titleLayout, "select.modelRotation", layoutSettings_.selectModelRotation);
	layoutSettings_.selectModelWeaponOffset = UILayoutIO::GetVector3(titleLayout, "select.modelWeaponOffset", layoutSettings_.selectModelWeaponOffset);
	layoutSettings_.selectModelWeaponScale = UILayoutIO::GetVector3(titleLayout, "select.modelWeaponScale", layoutSettings_.selectModelWeaponScale);
	layoutSettings_.selectModelWeaponRotation = UILayoutIO::GetVector3(titleLayout, "select.modelWeaponRotation", layoutSettings_.selectModelWeaponRotation);
	layoutSettings_.selectDetailModelPosition = UILayoutIO::GetVector3(titleLayout, "select.detailModelPosition", layoutSettings_.selectDetailModelPosition);
	layoutSettings_.selectDetailModelScale = UILayoutIO::GetVector3(titleLayout, "select.detailModelScale", layoutSettings_.selectDetailModelScale);
	layoutSettings_.selectDetailModelRotation = UILayoutIO::GetVector3(titleLayout, "select.detailModelRotation", layoutSettings_.selectDetailModelRotation);
	layoutSettings_.selectDetailWeaponOffset = UILayoutIO::GetVector3(titleLayout, "select.detailWeaponOffset", layoutSettings_.selectDetailWeaponOffset);
	layoutSettings_.selectDetailWeaponScale = UILayoutIO::GetVector3(titleLayout, "select.detailWeaponScale", layoutSettings_.selectDetailWeaponScale);
	layoutSettings_.selectDetailWeaponRotation = UILayoutIO::GetVector3(titleLayout, "select.detailWeaponRotation", layoutSettings_.selectDetailWeaponRotation);
#ifdef _DEBUG
	auto loadWindowVisible = [&titleLayout](std::string_view key, bool fallback) {
		return UILayoutIO::GetFloat(titleLayout, key, fallback ? 1.0f : 0.0f) != 0.0f;
	};
	debugWindows_.windowSwitcher = loadWindowVisible("debug.windowSwitcher", debugWindows_.windowSwitcher);
	debugWindows_.titleView = loadWindowVisible("debug.titleView", debugWindows_.titleView);
	debugWindows_.statisticsView = loadWindowVisible("debug.statisticsView", debugWindows_.statisticsView);
	debugWindows_.titleSettings = loadWindowVisible("debug.titleSettings", debugWindows_.titleSettings);
	debugWindows_.titleModelSettings = loadWindowVisible("debug.titleModelSettings", debugWindows_.titleModelSettings);
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
		ResourcePaths::MakeTexturePath(kSelectTexturePath),
		ResourcePaths::MakeTexturePath(kNumberTexturePath),
		ResourcePaths::MakeTexturePath(kTextFontTexture),
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
	selectSprite_.Initialize(kSelectTexturePath, layoutSettings_.selectBackgroundPosition);
	selectSprite_.SetSize(layoutSettings_.selectBackgroundSize);
	selectTitleText_.Initialize(kTextFontTexture, kTextFontMetadata);
	selectTitleText_.SetText("キャラ選択");
	selectActionText_.Initialize(kTextFontTexture, kTextFontMetadata);
	selectedWeaponDescriptionText_.Initialize(kTextFontTexture, kTextFontMetadata);
	selectedWeaponDescriptionText_.SetColor({ 0.86f, 0.93f, 1.0f, 0.95f });
	selectActionButton_.Initialize();
	selectedCharacterFaceIcon_.Initialize(kCharacterUiDefinitions[1].iconPath, layoutSettings_.selectDetailFacePosition);
	selectedWeaponIcon_.Initialize(kCharacterUiDefinitions[1].weaponIconPath, layoutSettings_.selectWeaponIconPosition);
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
	for (int32_t index = 0; index < 3; ++index) {
		const CharacterId id = StartCharacterIdFromIndex(index);
		const size_t sourceIndex = static_cast<size_t>(static_cast<int32_t>(id));
		const Vector2 iconPosition = StartCharacterIconPosition(index);
		startCharacterHighlights_[index].Initialize();
		startCharacterHighlights_[index].SetPosition({
			iconPosition.x - kStartCharacterHighlightPadding.x,
			iconPosition.y - kStartCharacterHighlightPadding.y,
		});
		startCharacterHighlights_[index].SetSize({
			kStartCharacterIconSize.x + kStartCharacterHighlightPadding.x * 2.0f,
			kStartCharacterIconSize.y + kStartCharacterHighlightPadding.y * 2.0f,
		});
		startCharacterIcons_[index].Initialize(
			kCharacterUiDefinitions[sourceIndex].iconPath,
			iconPosition);
		startCharacterIcons_[index].SetSize(kStartCharacterIconSize);
	}
	ApplyStartCharacterSelectionLayout();

	curtain_ = std::make_unique<CurtainTransition>();
	curtain_->Initialize();
	curtain_->StartOpen(20.0f);

	titleBgmHandle_ = GameAudioCache::LoadWave(kTitleBgmPath, AudioBus::Bgm);
	selectSeHandle_ = GameAudioCache::LoadWave(kSelectSePath);
	decideSeHandle_ = GameAudioCache::LoadWave(kDecideSePath);
	backSeHandle_ = GameAudioCache::LoadWave(kBackSePath);
}

void TitleScene::InitializeCameraAndObjects()
{
	titleCamera_ = std::make_unique<Engine::CameraSystem::Camera>();
	UpdateCameraAnimation();
	Engine::CameraSystem::CameraManager::GetInstance()->AddCamera(kTitleCameraName, titleCamera_.get());
	Engine::CameraSystem::CameraManager::GetInstance()->SetActiveCamera(kTitleCameraName);

	GameModelCache::LoadBatch({
		"cube.obj",
		kTitlePlayerModelPath,
		"skydome.obj",
		"plane.obj",
		"cube_world/tree.glb",
		"cube_world/dead_tree.glb",
		"cube_world/rock.glb",
		"cube_world/grass.glb",
		"cube_world/grass_small.glb",
		"cube_world/bush.glb",
		"cube_world/flowers.glb",
		"cube_world/mushroom.glb",
		"cube_world/plant.glb",
		"quaternius_characters/adventurer.glb",
		"quaternius_characters/king.glb",
		"quaternius_characters/swat.glb",
		"quaternius_weapons/bow.glb",
		"quaternius_weapons/sword.glb",
		"quaternius_weapons/pistol.glb",
		});

	const ModelHandle titleModelHandle = GameModelCache::Load(kTitlePlayerModelPath);
	titleObject_ = std::make_unique<Engine::Graphics3D::Object3D>();
	titleObject_->Initialize(Engine::Graphics3D::Object3DCommon::GetInstance());
	GameModelCache::ApplyToObject(*titleObject_, titleModelHandle);
	titleObject_->SetSkyboxFilePath(kEnvironmentTexturePath);
	titleObject_->SetEnvironmentReflectionStrength(0.0f);
	titleObject_->SetEnvironmentRoughness(1.0f);
	titleObject_->SetCastsShadow(true);
	ApplyIdleAnimation(*titleObject_);
	titleObject_->SetRotate({ kTitlePlayerModelBasePitch, -2.618f, 0.0f });
	titleObject_->SetScale(TitlePlayerModelScale(layoutSettings_.modelScale));
	titleObject_->SetTranslate(layoutSettings_.modelBasePosition);

	startCharacterModel_ = std::make_unique<Engine::Graphics3D::Object3D>();
	startCharacterModel_->Initialize(Engine::Graphics3D::Object3DCommon::GetInstance());
	GameModelCache::ApplyToObject(
		*startCharacterModel_,
		GameModelCache::Load(kCharacterUiDefinitions[1].modelPath));
	startCharacterModel_->SetSkyboxFilePath(kEnvironmentTexturePath);
	startCharacterModel_->SetEnvironmentReflectionStrength(0.0f);
	startCharacterModel_->SetEnvironmentRoughness(1.0f);
	startCharacterModel_->SetCastsShadow(true);
	ApplyIdleAnimation(*startCharacterModel_);
	startCharacterWeaponModel_ = CreateTitlePreviewObject(
		kCharacterUiDefinitions[1].weaponModelPath,
		{ 1.0f, 1.0f, 1.0f, 1.0f });
	startCharacterDetailModel_ = CreateTitlePreviewObject(
		kCharacterUiDefinitions[1].modelPath,
		kCharacterUiDefinitions[1].modelColor);
	startCharacterDetailWeaponModel_ = CreateTitlePreviewObject(
		kCharacterUiDefinitions[1].weaponModelPath,
		{ 1.0f, 1.0f, 1.0f, 1.0f });
	for (int32_t index = 0; index < 3; ++index) {
		const CharacterUiDefinition& definition =
			CharacterUiForId(StartCharacterIdFromIndex(index));
		startCharacterPreviewModels_[static_cast<size_t>(index)] =
			CreateTitlePreviewObject(definition.modelPath, definition.modelColor);
		startCharacterPreviewWeaponModels_[static_cast<size_t>(index)] =
			CreateTitlePreviewObject(definition.weaponModelPath, { 1.0f, 1.0f, 1.0f, 1.0f });
	}
	UpdateStartCharacterModel();
	UpdateStartCharacterPreviewModels();

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
		ApplyIdleAnimation(*titleObject_);
		titleObject_->SetScale(TitlePlayerModelScale(layoutSettings_.modelScale));
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
	if (!input) {
		return;
	}
	const GameMenuInputState menuInput = GameMenuController::Update(input, navigationInputDevice_);
	navigationInputDevice_ = menuInput.device;

	if (awaitingCharacterSelect_) {
		cursorSprite_.SetColor({ 1.0f, 1.0f, 1.0f, 0.0f });
		return;
	}

	if (showingUpgradeScreen_) {
		cursorSprite_.SetColor({ 1.0f, 1.0f, 1.0f, 0.0f });
		if (menuInput.cancel) {
			if (backSeHandle_) {
				GameAudioCache::PlayTuned(backSeHandle_, kAudioUiBack, 0.52f);
			}
			showingUpgradeScreen_ = false;
			cursorSprite_.SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
		}
		return;
	}

	if (input->TriggerKey(kOpenCharacterSelectKey)) {
		awaitingCharacterSelect_ = true;
		showingUpgradeScreen_ = false;
		shopCharacterSelectionActive_ = false;
		characterItemIndex_ = StartCharacterIndexFromId(CharacterId::Bow);
		startCharacterSelectedIndex_ = characterItemIndex_;
		startCharacterInputDelayFrames_ = 1;
		startCharacterActionArmed_ = true;
		cursorSprite_.SetColor({ 1.0f, 1.0f, 1.0f, 0.0f });
		if (selectSeHandle_) {
			GameAudioCache::PlayTuned(selectSeHandle_, kAudioUiSelect, 0.55f, 0.04f);
		}
		return;
	}

	const bool mouseInputActive =
		!GameInputBindings::IsGameInputSuppressedByImGui() &&
		ScreenUtil::IsInsideDebugSceneViewport(input->GetMousePos());
	const Vector2 mousePosition = ScreenUtil::ToGamePosition(input->GetMousePos());

	int32_t hoveredMenuIndex = -1;
	if (mouseInputActive) {
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

	if (menuIndex_ != previousIndex && selectSeHandle_) {
		GameAudioCache::PlayTuned(selectSeHandle_, kAudioUiSelect, 0.55f, 0.04f);
	}

	const bool confirmTriggered =
		navigationInputDevice_ == GameInputBindings::NavigationInputDevice::Mouse
		? IsMouseMenuConfirm(hoveredMenuIndex)
		: menuInput.confirm;

	if (!confirmTriggered) {
		return;
	}

	if (decideSeHandle_) {
		GameAudioCache::PlayTuned(decideSeHandle_, kAudioUiDecide, 0.72f);
	}

	switch (menuIndex_) {
	case 0:
		if (sessionContext_) {
			sessionContext_->TrySelectCharacter(CharacterId::Bow);
		}
		if (curtain_ && curtain_->GetState() == CurtainTransition::State::None) {
			curtain_->StartClose();
			curtainStarted_ = true;
		}
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
	const GameMenuInputState menuInput =
		GameMenuController::Update(input, navigationInputDevice_);
	navigationInputDevice_ = menuInput.device;

	bool selectionChanged = false;
	bool handledMouseConfirm = false;
	const bool mouseConfirm =
		GameInputBindings::IsMouseConfirmTriggered(input);
	if (!GameInputBindings::IsGameInputSuppressedByImGui() &&
		ScreenUtil::IsInsideDebugSceneViewport(input->GetMousePos())) {
		const Vector2 mousePosition = ScreenUtil::ToGamePosition(input->GetMousePos());
		for (int32_t index = 0; index < kPermanentUpgradeCount; ++index) {
			if (IsPointInRect(
				mousePosition,
				layoutSettings_.shopItemPositions[static_cast<size_t>(index)],
				layoutSettings_.shopItemHitboxSize)) {
				selectionChanged =
					shopCharacterSelectionActive_ || shopItemIndex_ != index;
				shopCharacterSelectionActive_ = false;
				shopItemIndex_ = index;
				if (mouseConfirm) {
					handledMouseConfirm = true;
					TryPurchasePermanentUpgrade(shopItemIndex_);
				}
				break;
			}
		}
		for (int32_t index = 0; index < kCharacterCount; ++index) {
			if (IsPointInRect(
				mousePosition,
				CharacterIconPosition(index),
				kCharacterRowHitboxSize)) {
				selectionChanged =
					!shopCharacterSelectionActive_ || characterItemIndex_ != index;
				shopCharacterSelectionActive_ = true;
				characterItemIndex_ = index;
				if (mouseConfirm) {
					handledMouseConfirm = true;
					TryActivateCharacter(characterItemIndex_);
				}
				break;
			}
		}
	}

	if (menuInput.device != GameInputBindings::NavigationInputDevice::Mouse) {
		if (GameInputBindings::IsMenuUpTriggered(input) ||
			GameInputBindings::IsMenuDownTriggered(input)) {
			shopCharacterSelectionActive_ = !shopCharacterSelectionActive_;
			if (shopCharacterSelectionActive_) {
				characterItemIndex_ = std::clamp(
					characterItemIndex_,
					0,
					kCharacterCount - 1);
			} else {
				shopItemIndex_ = std::clamp(
					shopItemIndex_,
					0,
					kPermanentUpgradeCount - 1);
			}
			selectionChanged = true;
		}
		if (GameInputBindings::IsMenuLeftTriggered(input)) {
			if (shopCharacterSelectionActive_) {
				characterItemIndex_ =
					(characterItemIndex_ + kCharacterCount - 1) % kCharacterCount;
			} else {
				shopItemIndex_ =
					(shopItemIndex_ + kPermanentUpgradeCount - 1) %
					kPermanentUpgradeCount;
			}
			selectionChanged = true;
		}
		if (GameInputBindings::IsMenuRightTriggered(input)) {
			if (shopCharacterSelectionActive_) {
				characterItemIndex_ = (characterItemIndex_ + 1) % kCharacterCount;
			} else {
				shopItemIndex_ = (shopItemIndex_ + 1) % kPermanentUpgradeCount;
			}
			selectionChanged = true;
		}
		if (menuInput.confirm) {
			if (shopCharacterSelectionActive_) {
				TryActivateCharacter(characterItemIndex_);
			} else {
				TryPurchasePermanentUpgrade(shopItemIndex_);
			}
		}
	}

	if (selectionChanged && selectSeHandle_) {
		GameAudioCache::PlayTuned(selectSeHandle_, kAudioUiSelect, 0.55f, 0.04f);
	}
	(void)handledMouseConfirm;
}

void TitleScene::UpdateStartCharacterSelectionInput()
{
	if (!sessionContext_ || curtainStarted_) {
		return;
	}

	Engine::InputSystem::Input* input = Engine::InputSystem::Input::GetInstance();
	if (!input) {
		return;
	}
	const GameMenuInputState menuInput =
		GameMenuController::Update(input, navigationInputDevice_);
	navigationInputDevice_ = menuInput.device;
	if (startCharacterInputDelayFrames_ > 0) {
		--startCharacterInputDelayFrames_;
		return;
	}

	auto selectStartCharacter = [this](int32_t index) {
		const int32_t clampedIndex = std::clamp(index, 0, 2);
		if (characterItemIndex_ == clampedIndex) {
			return;
		}
		characterItemIndex_ = clampedIndex;
		startCharacterActionArmed_ = startCharacterSelectedIndex_ == characterItemIndex_;
		if (selectSeHandle_) {
			GameAudioCache::PlayTuned(selectSeHandle_, kAudioUiSelect, 0.55f, 0.04f);
		}
	};
	auto armFocusedCharacter = [this]() {
		if ((startCharacterSelectedIndex_ != characterItemIndex_ || !startCharacterActionArmed_) &&
			selectSeHandle_) {
			GameAudioCache::PlayTuned(selectSeHandle_, kAudioUiSelect, 0.55f, 0.04f);
		}
		startCharacterSelectedIndex_ = characterItemIndex_;
		startCharacterActionArmed_ = true;
	};
	auto activateSelectedCharacter = [this]() {
		startCharacterSelectedIndex_ = std::clamp(startCharacterSelectedIndex_, 0, 2);
		const CharacterId id = StartCharacterIdFromIndex(startCharacterSelectedIndex_);
		if (!sessionContext_) {
			return;
		}
		const bool wasUnlocked = sessionContext_->IsCharacterUnlocked(id);
		const bool activated = wasUnlocked
			? sessionContext_->TrySelectCharacter(id)
			: sessionContext_->TryUnlockCharacter(id);
		if (!activated) {
			return;
		}
		if (decideSeHandle_) {
			GameAudioCache::PlayTuned(decideSeHandle_, kAudioUiDecide, 0.72f);
		}
		if (wasUnlocked && sessionContext_->GetSelectedCharacterId() == id) {
			StartGameTransition();
		}
	};

	const bool gameInputActive =
		!GameInputBindings::IsGameInputSuppressedByImGui();
	if (gameInputActive && input->TriggerKey(DIK_1)) {
		selectStartCharacter(0);
		armFocusedCharacter();
	}
	if (gameInputActive && input->TriggerKey(DIK_2)) {
		selectStartCharacter(1);
		armFocusedCharacter();
	}
	if (gameInputActive && input->TriggerKey(DIK_3)) {
		selectStartCharacter(2);
		armFocusedCharacter();
	}
	if (GameInputBindings::IsMenuLeftTriggered(input)) {
		selectStartCharacter((characterItemIndex_ + 2) % 3);
	}
	if (GameInputBindings::IsMenuRightTriggered(input)) {
		selectStartCharacter((characterItemIndex_ + 1) % 3);
	}
	if (GameInputBindings::IsMenuUpTriggered(input)) {
		selectStartCharacter((characterItemIndex_ + 2) % 3);
	}
	if (GameInputBindings::IsMenuDownTriggered(input)) {
		selectStartCharacter((characterItemIndex_ + 1) % 3);
	}

	if (!GameInputBindings::IsGameInputSuppressedByImGui() &&
		ScreenUtil::IsInsideDebugSceneViewport(input->GetMousePos())) {
		const Vector2 mousePosition = ScreenUtil::ToGamePosition(input->GetMousePos());
		const bool mouseConfirm =
			GameInputBindings::IsMouseConfirmTriggered(input);
		for (int32_t index = 0; index < 3; ++index) {
			const int32_t row = index / 4;
			const int32_t column = index % 4;
			const Vector2 iconPosition{
				layoutSettings_.selectIconBasePosition.x +
					layoutSettings_.selectIconStepX * static_cast<float>(column),
				layoutSettings_.selectIconBasePosition.y +
					layoutSettings_.selectIconStepY * static_cast<float>(row),
			};
			if (IsPointInRect(mousePosition, iconPosition, layoutSettings_.selectIconHitboxSize)) {
				const bool sameFocusedCharacter = characterItemIndex_ == index;
				selectStartCharacter(index);
				if (mouseConfirm) {
					if (sameFocusedCharacter &&
						startCharacterActionArmed_ &&
						startCharacterSelectedIndex_ == characterItemIndex_) {
						activateSelectedCharacter();
					} else {
						armFocusedCharacter();
					}
				}
				break;
			}
		}
		if (IsPointInRect(
				mousePosition,
				layoutSettings_.selectActionButtonPosition,
				layoutSettings_.selectActionButtonSize) &&
			mouseConfirm) {
			activateSelectedCharacter();
		}
	}

	if (menuInput.cancel) {
		awaitingCharacterSelect_ = false;
		startCharacterActionArmed_ = false;
		cursorSprite_.SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
		if (backSeHandle_) {
			GameAudioCache::PlayTuned(backSeHandle_, kAudioUiBack, 0.52f);
		}
		return;
	}

	if (menuInput.device != GameInputBindings::NavigationInputDevice::Mouse &&
		menuInput.confirm) {
		if (startCharacterActionArmed_ &&
			startCharacterSelectedIndex_ == characterItemIndex_) {
			activateSelectedCharacter();
		} else {
			armFocusedCharacter();
		}
	}
}

bool TitleScene::TryPurchasePermanentUpgrade(int32_t index)
{
	if (!sessionContext_ || index < 0 || index >= kPermanentUpgradeCount) {
		return false;
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
		if (decideSeHandle_) {
			GameAudioCache::PlayTuned(decideSeHandle_, kAudioUiDecide, 0.72f);
		}
	}
	return purchased;
}

void TitleScene::UpdateCharacterSelectionInput()
{
	if (!sessionContext_ || curtainStarted_) {
		return;
	}

	Engine::InputSystem::Input* input = Engine::InputSystem::Input::GetInstance();
	if (!input || !ScreenUtil::IsInsideDebugSceneViewport(input->GetMousePos())) {
		return;
	}
	if (navigationInputDevice_ != GameInputBindings::NavigationInputDevice::Mouse) {
		return;
	}

	const Vector2 mousePosition = ScreenUtil::ToGamePosition(input->GetMousePos());
	const bool mouseConfirm =
		GameInputBindings::IsMouseConfirmTriggered(input);
	for (int32_t index = 0; index < kCharacterCount; ++index) {
		if (IsPointInRect(
			mousePosition,
			CharacterIconPosition(index),
			kCharacterRowHitboxSize)) {
			if (!shopCharacterSelectionActive_ || characterItemIndex_ != index) {
				shopCharacterSelectionActive_ = true;
				characterItemIndex_ = index;
				if (selectSeHandle_) {
					GameAudioCache::PlayTuned(selectSeHandle_, kAudioUiSelect, 0.55f, 0.04f);
				}
			}
			if (mouseConfirm) {
				TryActivateCharacter(characterItemIndex_);
			}
			break;
		}
	}
}

bool TitleScene::TryActivateCharacter(int32_t index)
{
	if (!sessionContext_ || index < 0 || index >= kCharacterCount) {
		return false;
	}

	const CharacterId id =
		kCharacterUiDefinitions[static_cast<size_t>(index)].id;
	const bool changed = sessionContext_->IsCharacterUnlocked(id)
		? sessionContext_->TrySelectCharacter(id)
		: sessionContext_->TryUnlockCharacter(id);
	if (changed && decideSeHandle_) {
		GameAudioCache::PlayTuned(decideSeHandle_, kAudioUiDecide, 0.72f);
	}
	return changed;
}

void TitleScene::UpdateAudio()
{
	if (!titleBgmHandle_) {
		return;
	}

	if (!GameAudioCache::IsPlaying(titleBgmHandle_)) {
		GameAudioCache::StopBus(AudioBus::Bgm);
		GameAudioCache::SetVolumeFromTuning(titleBgmHandle_, kAudioTitleBgm, 0.42f);
		GameAudioCache::PlayLoop(titleBgmHandle_);
	}
}

void TitleScene::UpdateModelAnimation()
{
	animationTime_ += kFixedDeltaTime;

	if (!titleObject_) {
		return;
	}

	titleObject_->SetRotate({ kTitlePlayerModelBasePitch, -2.618f, 0.0f });
	titleObject_->SetTranslate(layoutSettings_.modelBasePosition);
	ApplyIdleAnimation(*titleObject_);
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
		: CharacterId::Default;
	const int32_t ownedCoins = sessionContext_ ? sessionContext_->GetOwnedCoins() : 0;

	for (int32_t index = 0; index < kCharacterCount; ++index) {
		const CharacterId id =
			kCharacterUiDefinitions[static_cast<size_t>(index)].id;
		const bool unlocked = sessionContext_ ? sessionContext_->IsCharacterUnlocked(id) : id == CharacterId::Default;
		const bool selected = unlocked && id == selectedId;
		const bool focused =
			shopCharacterSelectionActive_ && index == characterItemIndex_;
		const int32_t unlockCost = sessionContext_ ? sessionContext_->GetCharacterUnlockCost(id) : 0;
		const bool affordable = !unlocked && ownedCoins >= unlockCost;
		const Vector4 iconColor = selected
			? Vector4{ 1.0f, 1.0f, 1.0f, 1.0f }
			: (focused
				? Vector4{ 1.0f, 0.88f, 0.12f, 0.95f }
				: (unlocked
				? Vector4{ 0.45f, 0.78f, 1.0f, 0.9f }
				: (affordable
					? Vector4{ 1.0f, 0.86f, 0.32f, 0.78f }
					: Vector4{ 0.36f, 0.36f, 0.36f, 0.62f })));
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

void TitleScene::UpdateStartCharacterSelectionDisplay()
{
	if (awaitingCharacterSelect_) {
		characterItemIndex_ = std::clamp(characterItemIndex_, 0, 2);
		startCharacterSelectedIndex_ = std::clamp(startCharacterSelectedIndex_, 0, 2);
	}
	for (int32_t index = 0; index < 3; ++index) {
		const CharacterId id = StartCharacterIdFromIndex(index);
		const bool unlocked = sessionContext_ ? sessionContext_->IsCharacterUnlocked(id) : true;
		const bool selected = index == startCharacterSelectedIndex_;
		const bool focused = awaitingCharacterSelect_ && index == characterItemIndex_;
		const bool armed = focused && selected && startCharacterActionArmed_;
		startCharacterHighlights_[index].SetColor(selected
			? Vector4{ 0.08f, 0.38f, 0.95f, 0.46f }
			: (focused
				? Vector4{ 1.0f, 0.88f, 0.12f, 0.32f }
				: Vector4{ 0.0f, 0.0f, 0.0f, 0.0f }));
		Vector4 iconColor = unlocked
			? (selected
				? Vector4{ 1.0f, 1.0f, 1.0f, 1.0f }
				: Vector4{ 0.52f, 0.72f, 0.86f, 0.88f })
			: Vector4{ 0.02f, 0.02f, 0.02f, 0.82f };
		if (focused && unlocked && !armed) {
			iconColor = { 1.0f, 0.92f, 0.42f, 1.0f };
		}
		startCharacterIcons_[index].SetColor(iconColor);
	}

	const CharacterId selectedId = StartCharacterIdFromIndex(startCharacterSelectedIndex_);
	const CharacterUiDefinition& selectedCharacter = CharacterUiForId(selectedId);
	const bool selectedUnlocked = sessionContext_
		? sessionContext_->IsCharacterUnlocked(selectedId)
		: true;
	selectedCharacterFaceIcon_.SetTexture(selectedCharacter.iconPath);
	selectedCharacterFaceIcon_.SetColor(selectedUnlocked
		? Vector4{ 1.0f, 1.0f, 1.0f, 1.0f }
		: Vector4{ 0.02f, 0.02f, 0.02f, 0.82f });
	selectedWeaponIcon_.SetTexture(selectedCharacter.weaponIconPath);
	selectedWeaponIcon_.SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
	selectedWeaponDescriptionText_.SetText(selectedCharacter.weaponDescription);
	selectActionButton_.SetColor(selectedUnlocked
		? Vector4{ 0.08f, 0.38f, 0.95f, 0.88f }
		: Vector4{ 0.86f, 0.08f, 0.08f, 0.88f });
	selectActionText_.SetText(selectedUnlocked ? "選択" : "購入");
	ApplyStartCharacterSelectionLayout();
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

void TitleScene::DrawStartCharacterSelectionDisplay()
{
	selectSprite_.Draw();
	selectTitleText_.Draw();
	for (UIPanel& highlight : startCharacterHighlights_) {
		highlight.Draw();
	}
	selectedWeaponIcon_.Draw();
	selectedWeaponDescriptionText_.Draw();
	selectActionButton_.Draw();
	selectActionText_.Draw();
}

void TitleScene::ApplyStartCharacterSelectionLayout()
{
	selectSprite_.SetPosition(layoutSettings_.selectBackgroundPosition);
	selectSprite_.SetSize(layoutSettings_.selectBackgroundSize);
	selectTitleText_.SetPosition(layoutSettings_.selectTitleTextPosition);
	selectTitleText_.SetScale(layoutSettings_.selectTitleTextScale);
	for (int32_t index = 0; index < 3; ++index) {
		const int32_t row = index / 4;
		const int32_t column = index % 4;
		const Vector2 iconPosition{
			layoutSettings_.selectIconBasePosition.x +
				layoutSettings_.selectIconStepX * static_cast<float>(column),
			layoutSettings_.selectIconBasePosition.y +
				layoutSettings_.selectIconStepY * static_cast<float>(row),
		};
		startCharacterHighlights_[index].SetPosition(iconPosition);
		startCharacterHighlights_[index].SetSize(layoutSettings_.selectIconHitboxSize);
		startCharacterIcons_[index].SetPosition(iconPosition);
		startCharacterIcons_[index].SetSize(layoutSettings_.selectIconSize);
	}
	selectedCharacterFaceIcon_.SetPosition(layoutSettings_.selectDetailFacePosition);
	selectedCharacterFaceIcon_.SetSize(layoutSettings_.selectDetailFaceSize);
	selectedWeaponIcon_.SetPosition(layoutSettings_.selectWeaponIconPosition);
	selectedWeaponIcon_.SetSize(layoutSettings_.selectWeaponIconSize);
	selectedWeaponDescriptionText_.SetPosition(layoutSettings_.selectWeaponDescriptionPosition);
	selectedWeaponDescriptionText_.SetScaleToFit(
		layoutSettings_.selectWeaponDescriptionScale,
		layoutSettings_.selectWeaponDescriptionMaxWidth);
	selectActionButton_.SetPosition(layoutSettings_.selectActionButtonPosition);
	selectActionButton_.SetSize(layoutSettings_.selectActionButtonSize);
	selectActionText_.SetPosition({
		layoutSettings_.selectActionButtonPosition.x + layoutSettings_.selectActionTextOffset.x,
		layoutSettings_.selectActionButtonPosition.y + layoutSettings_.selectActionTextOffset.y,
	});
	selectActionText_.SetScale(layoutSettings_.selectActionTextScale);
}

void TitleScene::UpdateStartCharacterModel()
{
	if (!startCharacterModel_) {
		return;
	}
	startCharacterSelectedIndex_ = std::clamp(startCharacterSelectedIndex_, 0, 2);
	const CharacterId selectedId = StartCharacterIdFromIndex(startCharacterSelectedIndex_);
	const CharacterUiDefinition& selectedCharacter = CharacterUiForId(selectedId);
	if (startCharacterAppliedSelectedIndex_ != startCharacterSelectedIndex_) {
		GameModelCache::ApplyToObject(
			*startCharacterModel_,
			GameModelCache::Load(selectedCharacter.modelPath));
		startCharacterModel_->SetCastsShadow(true);
		ApplyIdleAnimation(*startCharacterModel_);
		if (startCharacterWeaponModel_) {
			GameModelCache::ApplyToObject(
				*startCharacterWeaponModel_,
				GameModelCache::Load(selectedCharacter.weaponModelPath));
			startCharacterWeaponModel_->SetCastsShadow(true);
		}
		if (startCharacterDetailModel_) {
			GameModelCache::ApplyToObject(
				*startCharacterDetailModel_,
				GameModelCache::Load(selectedCharacter.modelPath));
			startCharacterDetailModel_->SetCastsShadow(true);
			ApplyIdleAnimation(*startCharacterDetailModel_);
		}
		if (startCharacterDetailWeaponModel_) {
			GameModelCache::ApplyToObject(
				*startCharacterDetailWeaponModel_,
				GameModelCache::Load(selectedCharacter.weaponModelPath));
			startCharacterDetailWeaponModel_->SetCastsShadow(true);
		}
		startCharacterAppliedSelectedIndex_ = startCharacterSelectedIndex_;
	}
	ApplyIdleAnimation(*startCharacterModel_);
	ApplyPreviewTransform(
		*startCharacterModel_,
		layoutSettings_.selectModelPosition,
		layoutSettings_.selectModelScale,
		layoutSettings_.selectModelRotation,
		selectedCharacter.modelColor);
	if (startCharacterWeaponModel_) {
		ApplyPreviewTransform(
			*startCharacterWeaponModel_,
			{
				layoutSettings_.selectModelPosition.x + layoutSettings_.selectModelWeaponOffset.x,
				layoutSettings_.selectModelPosition.y + layoutSettings_.selectModelWeaponOffset.y,
				layoutSettings_.selectModelPosition.z + layoutSettings_.selectModelWeaponOffset.z,
			},
			layoutSettings_.selectModelWeaponScale,
			layoutSettings_.selectModelWeaponRotation,
			{ 1.0f, 1.0f, 1.0f, 1.0f });
	}
	if (startCharacterDetailModel_) {
		ApplyIdleAnimation(*startCharacterDetailModel_);
		ApplyPreviewTransform(
			*startCharacterDetailModel_,
			layoutSettings_.selectDetailModelPosition,
			layoutSettings_.selectDetailModelScale,
			layoutSettings_.selectDetailModelRotation,
			selectedCharacter.modelColor);
	}
	if (startCharacterDetailWeaponModel_) {
		ApplyPreviewTransform(
			*startCharacterDetailWeaponModel_,
			{
				layoutSettings_.selectDetailModelPosition.x + layoutSettings_.selectDetailWeaponOffset.x,
				layoutSettings_.selectDetailModelPosition.y + layoutSettings_.selectDetailWeaponOffset.y,
				layoutSettings_.selectDetailModelPosition.z + layoutSettings_.selectDetailWeaponOffset.z,
			},
			layoutSettings_.selectDetailWeaponScale,
			layoutSettings_.selectDetailWeaponRotation,
			{ 1.0f, 1.0f, 1.0f, 1.0f });
	}
}

void TitleScene::UpdateStartCharacterPreviewModels()
{
	for (int32_t index = 0; index < 3; ++index) {
		const CharacterId id = StartCharacterIdFromIndex(index);
		const CharacterUiDefinition& definition = CharacterUiForId(id);
		const Vector3 basePosition{
			layoutSettings_.selectIconModelBasePosition.x +
				layoutSettings_.selectIconModelStep.x * static_cast<float>(index),
			layoutSettings_.selectIconModelBasePosition.y +
				layoutSettings_.selectIconModelStep.y * static_cast<float>(index),
			layoutSettings_.selectIconModelBasePosition.z +
				layoutSettings_.selectIconModelStep.z * static_cast<float>(index),
		};
		const bool unlocked = sessionContext_ ? sessionContext_->IsCharacterUnlocked(id) : true;
		const Vector4 modelColor = unlocked
			? definition.modelColor
			: Vector4{ 0.02f, 0.02f, 0.02f, 0.82f };
		if (startCharacterPreviewModels_[static_cast<size_t>(index)]) {
			ApplyIdleAnimation(
				*startCharacterPreviewModels_[static_cast<size_t>(index)]);
			ApplyPreviewTransform(
				*startCharacterPreviewModels_[static_cast<size_t>(index)],
				basePosition,
				layoutSettings_.selectIconModelScale,
				layoutSettings_.selectIconModelRotation,
				modelColor);
		}
		if (startCharacterPreviewWeaponModels_[static_cast<size_t>(index)]) {
			ApplyPreviewTransform(
				*startCharacterPreviewWeaponModels_[static_cast<size_t>(index)],
				{
					basePosition.x + layoutSettings_.selectIconWeaponOffset.x,
					basePosition.y + layoutSettings_.selectIconWeaponOffset.y,
					basePosition.z + layoutSettings_.selectIconWeaponOffset.z,
				},
				layoutSettings_.selectIconWeaponScale,
				layoutSettings_.selectIconWeaponRotation,
				unlocked
					? Vector4{ 1.0f, 1.0f, 1.0f, 1.0f }
					: Vector4{ 0.02f, 0.02f, 0.02f, 0.82f });
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
		highlight.SetColor(!shopCharacterSelectionActive_ && itemIndex == shopItemIndex_
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
			{ "select.backgroundPosition", { layoutSettings_.selectBackgroundPosition.x, layoutSettings_.selectBackgroundPosition.y } },
			{ "select.backgroundSize", { layoutSettings_.selectBackgroundSize.x, layoutSettings_.selectBackgroundSize.y } },
			{ "select.titleTextPosition", { layoutSettings_.selectTitleTextPosition.x, layoutSettings_.selectTitleTextPosition.y } },
			{ "select.titleTextScale", { layoutSettings_.selectTitleTextScale } },
			{ "select.iconBasePosition", { layoutSettings_.selectIconBasePosition.x, layoutSettings_.selectIconBasePosition.y } },
			{ "select.iconSize", { layoutSettings_.selectIconSize.x, layoutSettings_.selectIconSize.y } },
			{ "select.iconHitboxSize", { layoutSettings_.selectIconHitboxSize.x, layoutSettings_.selectIconHitboxSize.y } },
			{ "select.iconStepX", { layoutSettings_.selectIconStepX } },
			{ "select.iconStepY", { layoutSettings_.selectIconStepY } },
			{ "select.iconModelBasePosition", { layoutSettings_.selectIconModelBasePosition.x, layoutSettings_.selectIconModelBasePosition.y, layoutSettings_.selectIconModelBasePosition.z } },
			{ "select.iconModelStep", { layoutSettings_.selectIconModelStep.x, layoutSettings_.selectIconModelStep.y, layoutSettings_.selectIconModelStep.z } },
			{ "select.iconModelScale", { layoutSettings_.selectIconModelScale.x, layoutSettings_.selectIconModelScale.y, layoutSettings_.selectIconModelScale.z } },
			{ "select.iconModelRotation", { layoutSettings_.selectIconModelRotation.x, layoutSettings_.selectIconModelRotation.y, layoutSettings_.selectIconModelRotation.z } },
			{ "select.iconWeaponOffset", { layoutSettings_.selectIconWeaponOffset.x, layoutSettings_.selectIconWeaponOffset.y, layoutSettings_.selectIconWeaponOffset.z } },
			{ "select.iconWeaponScale", { layoutSettings_.selectIconWeaponScale.x, layoutSettings_.selectIconWeaponScale.y, layoutSettings_.selectIconWeaponScale.z } },
			{ "select.iconWeaponRotation", { layoutSettings_.selectIconWeaponRotation.x, layoutSettings_.selectIconWeaponRotation.y, layoutSettings_.selectIconWeaponRotation.z } },
			{ "select.detailFacePosition", { layoutSettings_.selectDetailFacePosition.x, layoutSettings_.selectDetailFacePosition.y } },
			{ "select.detailFaceSize", { layoutSettings_.selectDetailFaceSize.x, layoutSettings_.selectDetailFaceSize.y } },
			{ "select.weaponIconPosition", { layoutSettings_.selectWeaponIconPosition.x, layoutSettings_.selectWeaponIconPosition.y } },
			{ "select.weaponIconSize", { layoutSettings_.selectWeaponIconSize.x, layoutSettings_.selectWeaponIconSize.y } },
			{ "select.weaponDescriptionPosition", { layoutSettings_.selectWeaponDescriptionPosition.x, layoutSettings_.selectWeaponDescriptionPosition.y } },
			{ "select.weaponDescriptionScale", { layoutSettings_.selectWeaponDescriptionScale } },
			{ "select.weaponDescriptionMaxWidth", { layoutSettings_.selectWeaponDescriptionMaxWidth } },
			{ "select.actionButtonPosition", { layoutSettings_.selectActionButtonPosition.x, layoutSettings_.selectActionButtonPosition.y } },
			{ "select.actionButtonSize", { layoutSettings_.selectActionButtonSize.x, layoutSettings_.selectActionButtonSize.y } },
			{ "select.actionTextOffset", { layoutSettings_.selectActionTextOffset.x, layoutSettings_.selectActionTextOffset.y } },
			{ "select.actionTextScale", { layoutSettings_.selectActionTextScale } },
			{ "select.modelPosition", { layoutSettings_.selectModelPosition.x, layoutSettings_.selectModelPosition.y, layoutSettings_.selectModelPosition.z } },
			{ "select.modelScale", { layoutSettings_.selectModelScale.x, layoutSettings_.selectModelScale.y, layoutSettings_.selectModelScale.z } },
			{ "select.modelRotation", { layoutSettings_.selectModelRotation.x, layoutSettings_.selectModelRotation.y, layoutSettings_.selectModelRotation.z } },
			{ "select.modelWeaponOffset", { layoutSettings_.selectModelWeaponOffset.x, layoutSettings_.selectModelWeaponOffset.y, layoutSettings_.selectModelWeaponOffset.z } },
			{ "select.modelWeaponScale", { layoutSettings_.selectModelWeaponScale.x, layoutSettings_.selectModelWeaponScale.y, layoutSettings_.selectModelWeaponScale.z } },
			{ "select.modelWeaponRotation", { layoutSettings_.selectModelWeaponRotation.x, layoutSettings_.selectModelWeaponRotation.y, layoutSettings_.selectModelWeaponRotation.z } },
			{ "select.detailModelPosition", { layoutSettings_.selectDetailModelPosition.x, layoutSettings_.selectDetailModelPosition.y, layoutSettings_.selectDetailModelPosition.z } },
			{ "select.detailModelScale", { layoutSettings_.selectDetailModelScale.x, layoutSettings_.selectDetailModelScale.y, layoutSettings_.selectDetailModelScale.z } },
			{ "select.detailModelRotation", { layoutSettings_.selectDetailModelRotation.x, layoutSettings_.selectDetailModelRotation.y, layoutSettings_.selectDetailModelRotation.z } },
			{ "select.detailWeaponOffset", { layoutSettings_.selectDetailWeaponOffset.x, layoutSettings_.selectDetailWeaponOffset.y, layoutSettings_.selectDetailWeaponOffset.z } },
			{ "select.detailWeaponScale", { layoutSettings_.selectDetailWeaponScale.x, layoutSettings_.selectDetailWeaponScale.y, layoutSettings_.selectDetailWeaponScale.z } },
			{ "select.detailWeaponRotation", { layoutSettings_.selectDetailWeaponRotation.x, layoutSettings_.selectDetailWeaponRotation.y, layoutSettings_.selectDetailWeaponRotation.z } },
#ifdef _DEBUG
			{ "debug.windowSwitcher", { debugWindows_.windowSwitcher ? 1.0f : 0.0f } },
			{ "debug.titleView", { debugWindows_.titleView ? 1.0f : 0.0f } },
			{ "debug.statisticsView", { debugWindows_.statisticsView ? 1.0f : 0.0f } },
			{ "debug.titleSettings", { debugWindows_.titleSettings ? 1.0f : 0.0f } },
			{ "debug.titleModelSettings", { debugWindows_.titleModelSettings ? 1.0f : 0.0f } },
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
	layoutSettings_.selectBackgroundPosition = UILayoutIO::GetVector2(titleLayout, "select.backgroundPosition", layoutSettings_.selectBackgroundPosition);
	layoutSettings_.selectBackgroundSize = UILayoutIO::GetVector2(titleLayout, "select.backgroundSize", layoutSettings_.selectBackgroundSize);
	layoutSettings_.selectTitleTextPosition = UILayoutIO::GetVector2(titleLayout, "select.titleTextPosition", layoutSettings_.selectTitleTextPosition);
	layoutSettings_.selectTitleTextScale = UILayoutIO::GetFloat(titleLayout, "select.titleTextScale", layoutSettings_.selectTitleTextScale);
	layoutSettings_.selectIconBasePosition = UILayoutIO::GetVector2(titleLayout, "select.iconBasePosition", layoutSettings_.selectIconBasePosition);
	layoutSettings_.selectIconSize = UILayoutIO::GetVector2(titleLayout, "select.iconSize", layoutSettings_.selectIconSize);
	layoutSettings_.selectIconHitboxSize = UILayoutIO::GetVector2(titleLayout, "select.iconHitboxSize", layoutSettings_.selectIconHitboxSize);
	layoutSettings_.selectIconStepX = UILayoutIO::GetFloat(titleLayout, "select.iconStepX", layoutSettings_.selectIconStepX);
	layoutSettings_.selectIconStepY = UILayoutIO::GetFloat(titleLayout, "select.iconStepY", layoutSettings_.selectIconStepY);
	layoutSettings_.selectIconModelBasePosition = UILayoutIO::GetVector3(titleLayout, "select.iconModelBasePosition", layoutSettings_.selectIconModelBasePosition);
	layoutSettings_.selectIconModelStep = UILayoutIO::GetVector3(titleLayout, "select.iconModelStep", layoutSettings_.selectIconModelStep);
	layoutSettings_.selectIconModelScale = UILayoutIO::GetVector3(titleLayout, "select.iconModelScale", layoutSettings_.selectIconModelScale);
	layoutSettings_.selectIconModelRotation = UILayoutIO::GetVector3(titleLayout, "select.iconModelRotation", layoutSettings_.selectIconModelRotation);
	layoutSettings_.selectIconWeaponOffset = UILayoutIO::GetVector3(titleLayout, "select.iconWeaponOffset", layoutSettings_.selectIconWeaponOffset);
	layoutSettings_.selectIconWeaponScale = UILayoutIO::GetVector3(titleLayout, "select.iconWeaponScale", layoutSettings_.selectIconWeaponScale);
	layoutSettings_.selectIconWeaponRotation = UILayoutIO::GetVector3(titleLayout, "select.iconWeaponRotation", layoutSettings_.selectIconWeaponRotation);
	layoutSettings_.selectDetailFacePosition = UILayoutIO::GetVector2(titleLayout, "select.detailFacePosition", layoutSettings_.selectDetailFacePosition);
	layoutSettings_.selectDetailFaceSize = UILayoutIO::GetVector2(titleLayout, "select.detailFaceSize", layoutSettings_.selectDetailFaceSize);
	layoutSettings_.selectWeaponIconPosition = UILayoutIO::GetVector2(titleLayout, "select.weaponIconPosition", layoutSettings_.selectWeaponIconPosition);
	layoutSettings_.selectWeaponIconSize = UILayoutIO::GetVector2(titleLayout, "select.weaponIconSize", layoutSettings_.selectWeaponIconSize);
	layoutSettings_.selectWeaponDescriptionPosition = UILayoutIO::GetVector2(titleLayout, "select.weaponDescriptionPosition", layoutSettings_.selectWeaponDescriptionPosition);
	layoutSettings_.selectWeaponDescriptionScale = UILayoutIO::GetFloat(titleLayout, "select.weaponDescriptionScale", layoutSettings_.selectWeaponDescriptionScale);
	layoutSettings_.selectWeaponDescriptionMaxWidth = UILayoutIO::GetFloat(titleLayout, "select.weaponDescriptionMaxWidth", layoutSettings_.selectWeaponDescriptionMaxWidth);
	layoutSettings_.selectActionButtonPosition = UILayoutIO::GetVector2(titleLayout, "select.actionButtonPosition", layoutSettings_.selectActionButtonPosition);
	layoutSettings_.selectActionButtonSize = UILayoutIO::GetVector2(titleLayout, "select.actionButtonSize", layoutSettings_.selectActionButtonSize);
	layoutSettings_.selectActionTextOffset = UILayoutIO::GetVector2(titleLayout, "select.actionTextOffset", layoutSettings_.selectActionTextOffset);
	layoutSettings_.selectActionTextScale = UILayoutIO::GetFloat(titleLayout, "select.actionTextScale", layoutSettings_.selectActionTextScale);
	layoutSettings_.selectModelPosition = UILayoutIO::GetVector3(titleLayout, "select.modelPosition", layoutSettings_.selectModelPosition);
	layoutSettings_.selectModelScale = UILayoutIO::GetVector3(titleLayout, "select.modelScale", layoutSettings_.selectModelScale);
	layoutSettings_.selectModelRotation = UILayoutIO::GetVector3(titleLayout, "select.modelRotation", layoutSettings_.selectModelRotation);
	layoutSettings_.selectModelWeaponOffset = UILayoutIO::GetVector3(titleLayout, "select.modelWeaponOffset", layoutSettings_.selectModelWeaponOffset);
	layoutSettings_.selectModelWeaponScale = UILayoutIO::GetVector3(titleLayout, "select.modelWeaponScale", layoutSettings_.selectModelWeaponScale);
	layoutSettings_.selectModelWeaponRotation = UILayoutIO::GetVector3(titleLayout, "select.modelWeaponRotation", layoutSettings_.selectModelWeaponRotation);
	layoutSettings_.selectDetailModelPosition = UILayoutIO::GetVector3(titleLayout, "select.detailModelPosition", layoutSettings_.selectDetailModelPosition);
	layoutSettings_.selectDetailModelScale = UILayoutIO::GetVector3(titleLayout, "select.detailModelScale", layoutSettings_.selectDetailModelScale);
	layoutSettings_.selectDetailModelRotation = UILayoutIO::GetVector3(titleLayout, "select.detailModelRotation", layoutSettings_.selectDetailModelRotation);
	layoutSettings_.selectDetailWeaponOffset = UILayoutIO::GetVector3(titleLayout, "select.detailWeaponOffset", layoutSettings_.selectDetailWeaponOffset);
	layoutSettings_.selectDetailWeaponScale = UILayoutIO::GetVector3(titleLayout, "select.detailWeaponScale", layoutSettings_.selectDetailWeaponScale);
	layoutSettings_.selectDetailWeaponRotation = UILayoutIO::GetVector3(titleLayout, "select.detailWeaponRotation", layoutSettings_.selectDetailWeaponRotation);
#ifdef _DEBUG
	auto loadWindowVisible = [&titleLayout](std::string_view key, bool fallback) {
		return UILayoutIO::GetFloat(titleLayout, key, fallback ? 1.0f : 0.0f) != 0.0f;
	};
	debugWindows_.windowSwitcher = loadWindowVisible("debug.windowSwitcher", debugWindows_.windowSwitcher);
	debugWindows_.titleView = loadWindowVisible("debug.titleView", debugWindows_.titleView);
	debugWindows_.statisticsView = loadWindowVisible("debug.statisticsView", debugWindows_.statisticsView);
	debugWindows_.titleSettings = loadWindowVisible("debug.titleSettings", debugWindows_.titleSettings);
	debugWindows_.titleModelSettings = loadWindowVisible("debug.titleModelSettings", debugWindows_.titleModelSettings);
	debugWindows_.offscreenSettings = loadWindowVisible("debug.offscreenSettings", debugWindows_.offscreenSettings);
	debugWindows_.lightSettings = loadWindowVisible("debug.lightSettings", debugWindows_.lightSettings);
	debugWindows_.audio = loadWindowVisible("debug.audio", debugWindows_.audio);
	debugWindows_.keyInputDebug = loadWindowVisible("debug.keyInputDebug", debugWindows_.keyInputDebug);
#endif
	InitializeLighting();
	ApplyLayout();
	ApplyStartCharacterSelectionLayout();
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
