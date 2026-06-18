#include "game/directxgame/scene/DirectXGameTitleScene.h"
#include "game/directxgame/core/DirectXGameDataPaths.h"
#include "game/directxgame/core/DirectXGameResourceProbe.h"
#include "game/directxgame/core/GameAudioTuning.h"
#include "game/directxgame/core/GameMenuController.h"
#include "game/directxgame/core/DirectXGameSceneId.h"
#include "game/directxgame/core/DirectXGameSessionContext.h"
#include "game/directxgame/core/GameInputBindings.h"
#include "game/directxgame/core/GameModelCache.h"
#include "game/directxgame/core/ScreenUtil.h"
#include "game/directxgame/core/SceneLighting.h"
#include "game/directxgame/core/TitleSceneDebugUiController.h"
#include "game/directxgame/core/UILayoutIO.h"
#include "game/directxgame/effects/CurtainTransition.h"
#include "Camera.h"
#include "CameraManager.h"
#include "Input.h"
#include "Object3D.h"
#include "Object3DCommon.h"
#include "OffscreenRenderManager.h"
#include "SceneManager.h"
#include "SpriteCommon.h"
#include "TextureManager.h"
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>

namespace {

constexpr float kFixedDeltaTime = 1.0f / 60.0f;
constexpr float kGuideFadeSpeed = 4.5f;
constexpr char kTitleTexturePath[] = "ui/title/title.png";
constexpr char kCursorTexturePath[] = "ui/title/cursor.png";
constexpr char kGuideTexturePath[] = "ui/title/guideUI.png";
constexpr char kTitleBgmPath[] = "audio/bgm/title.wav";
constexpr char kSelectSePath[] = "audio/se/se_pause.wav";
constexpr char kDecideSePath[] = "audio/se/se_exp.wav";
constexpr char kAudioTitleBgm[] = "title.bgm";
constexpr char kAudioTitleSelect[] = "title.select";
constexpr char kAudioTitleDecide[] = "title.decide";
constexpr char kEnvironmentTexturePath[] = "Resources/textures/skybox/test.dds";
constexpr char kTitleCameraName[] = "directxgame_title";

bool IsPointInRect(const Vector2& point, const Vector2& rectPosition, const Vector2& rectSize)
{
	return point.x >= rectPosition.x && point.x <= rectPosition.x + rectSize.x &&
		point.y >= rectPosition.y && point.y <= rectPosition.y + rectSize.y;
}

}

namespace DirectXGame {

DirectXGameTitleScene::DirectXGameTitleScene(std::shared_ptr<DirectXGameSessionContext> sessionContext)
	: sessionContext_(std::move(sessionContext))
{
}

void DirectXGameTitleScene::Initialize()
{
	LoadGameAudioTuning();
	Engine::CameraSystem::CameraManager::GetInstance()->Initialize();
	if (Engine::Base::OffscreenRenderManager* offscreen = Engine::Base::OffscreenRenderManager::GetInstance()) {
		offscreen->SetScenePostEffectType(PostEffectType::Fullscreen);
	}

	if (sessionContext_) {
		sessionContext_->OnEnterTitleScene();
	}

	DirectXGameResourceProbe::Verify();
	InitializeLighting();
	InitializeResources();
	InitializeCameraAndObjects();
	ApplyLayout();
}

void DirectXGameTitleScene::Finalize()
{
	if (titleBgmHandle_ != 0) {
		GameAudioCache::Stop(titleBgmHandle_);
	}

	Engine::CameraSystem::CameraManager::GetInstance()->RemoveCamera(kTitleCameraName);
}

void DirectXGameTitleScene::Update()
{
	UpdateCurtain();

	if (curtainStarted_) {
		if (curtain_ && curtain_->IsFinished()) {
			if (titleBgmHandle_ != 0) {
				GameAudioCache::Stop(titleBgmHandle_);
			}
			Engine::Scene::SceneManager::GetInstance()->ChangeScene(SceneId::kGame);
			DrawDebugUi();
			return;
		}
		DrawDebugUi();
		return;
	}

	if (curtainOpening_) {
		if (curtain_ && curtain_->GetState() == CurtainTransition::State::None) {
			curtainOpening_ = false;
		}
		DrawDebugUi();
		return;
	}

	UpdateAudio();
	UpdateGuide();
	if (!guideActive_ && guideTransitionState_ == GuideTransitionState::None) {
		UpdateNavigation();
	}
	UpdateModelAnimation();
	UpdateCameraAnimation();

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

	DrawDebugUi();
}

void DirectXGameTitleScene::Draw()
{
	Engine::Graphics3D::Object3DCommon::GetInstance()->CommonDraw();
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
	if (guideActive_ || guideTransitionState_ != GuideTransitionState::None) {
		guideSprite_.Draw();
	} else {
		cursorSprite_.Draw();
	}
	if (curtain_) {
		curtain_->Draw();
	}
}

void DirectXGameTitleScene::InitializeResources()
{
	const UILayoutIO::LayoutMap titleLayout = UILayoutIO::LoadOrDefault(DataPaths::kTitleLayout, {});
	layoutSettings_.titlePosition = UILayoutIO::GetVector2(titleLayout, "titlePosition", layoutSettings_.titlePosition);
	layoutSettings_.titleSize = UILayoutIO::GetVector2(titleLayout, "titleSize", layoutSettings_.titleSize);
	layoutSettings_.cursorBasePosition = UILayoutIO::GetVector2(titleLayout, "cursorBasePosition", layoutSettings_.cursorBasePosition);
	layoutSettings_.cursorSize = UILayoutIO::GetVector2(titleLayout, "cursorSize", layoutSettings_.cursorSize);
	layoutSettings_.cursorStepY = UILayoutIO::GetFloat(titleLayout, "cursorStepY", layoutSettings_.cursorStepY);
	layoutSettings_.menuHitboxPosition = UILayoutIO::GetVector2(titleLayout, "menuHitboxPosition", layoutSettings_.menuHitboxPosition);
	layoutSettings_.menuHitboxSize = UILayoutIO::GetVector2(titleLayout, "menuHitboxSize", layoutSettings_.menuHitboxSize);
	layoutSettings_.menuHitboxStepY = UILayoutIO::GetFloat(titleLayout, "menuHitboxStepY", layoutSettings_.menuHitboxStepY);
	layoutSettings_.guidePosition = UILayoutIO::GetVector2(titleLayout, "guidePosition", layoutSettings_.guidePosition);
	layoutSettings_.guideSize = UILayoutIO::GetVector2(titleLayout, "guideSize", layoutSettings_.guideSize);
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
	const std::string guideTexturePath =
		ResourcePaths::MakeTexturePath(kGuideTexturePath);
	Engine::Base::TextureManager::GetInstance()->LoadTextures({
		titleTexturePath,
		cursorTexturePath,
		guideTexturePath,
		kEnvironmentTexturePath,
		});
	titleSprite_.Initialize(titleTexturePath, layoutSettings_.titlePosition);
	cursorSprite_.Initialize(cursorTexturePath, layoutSettings_.cursorBasePosition);
	guideSprite_.Initialize(guideTexturePath, layoutSettings_.guidePosition);
	guideSprite_.SetColor({ 1.0f, 1.0f, 1.0f, 0.0f });

	curtain_ = std::make_unique<CurtainTransition>();
	curtain_->Initialize();
	curtain_->StartOpen(20.0f);

	titleBgmHandle_ = GameAudioCache::LoadWave(kTitleBgmPath);
	selectSeHandle_ = GameAudioCache::LoadWave(kSelectSePath);
	decideSeHandle_ = GameAudioCache::LoadWave(kDecideSePath);
}

void DirectXGameTitleScene::InitializeCameraAndObjects()
{
	titleCamera_ = std::make_unique<Engine::CameraSystem::Camera>();
	UpdateCameraAnimation();
	Engine::CameraSystem::CameraManager::GetInstance()->AddCamera(kTitleCameraName, titleCamera_.get());
	Engine::CameraSystem::CameraManager::GetInstance()->SetActiveCamera(kTitleCameraName);

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
	skyDomeObject_->SetScale({ 28.0f, 28.0f, 28.0f });
	skyDomeObject_->SetTranslate({ 0.0f, 0.0f, 0.0f });

	gridPlane_ = std::make_unique<GridPlane>();
	gridPlane_->Initialize();
	gridPlane_->Update(layoutSettings_.modelBasePosition);
}

void DirectXGameTitleScene::InitializeLighting()
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

void DirectXGameTitleScene::ApplyLayout()
{
	titleSprite_.SetPosition(layoutSettings_.titlePosition);
	titleSprite_.SetSize(layoutSettings_.titleSize);
	cursorSprite_.SetPosition({
		layoutSettings_.cursorBasePosition.x,
		layoutSettings_.cursorBasePosition.y + layoutSettings_.cursorStepY * static_cast<float>(menuIndex_)
		});
	cursorSprite_.SetSize(layoutSettings_.cursorSize);
	guideSprite_.SetPosition(layoutSettings_.guidePosition);
	guideSprite_.SetSize(layoutSettings_.guideSize);

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

void DirectXGameTitleScene::UpdateCurtain()
{
	if (curtain_) {
		curtain_->Update(kFixedDeltaTime);
	}
}

void DirectXGameTitleScene::UpdateGuide()
{
	if (guideActive_ && guideTransitionState_ == GuideTransitionState::None) {
		Engine::InputSystem::Input* input = Engine::InputSystem::Input::GetInstance();
		const GameMenuInputState menuInput = GameMenuController::Update(input, navigationInputDevice_);
		navigationInputDevice_ = menuInput.device;
		if (menuInput.cancel || menuInput.confirm) {
			CloseGuide();
		}
		return;
	}

	if (guideTransitionState_ == GuideTransitionState::None) {
		return;
	}

	const float deltaAlpha = kFixedDeltaTime * kGuideFadeSpeed;
	if (guideTransitionState_ == GuideTransitionState::FadeIn) {
		guideAlpha_ += deltaAlpha;
		if (guideAlpha_ >= 1.0f) {
			guideAlpha_ = 1.0f;
			guideTransitionState_ = GuideTransitionState::None;
			guideActive_ = true;
		}
	} else {
		guideAlpha_ -= deltaAlpha;
		if (guideAlpha_ <= 0.0f) {
			guideAlpha_ = 0.0f;
			guideTransitionState_ = GuideTransitionState::None;
			guideActive_ = false;
		}
	}

	guideSprite_.SetColor({ 1.0f, 1.0f, 1.0f, guideAlpha_ });
}

void DirectXGameTitleScene::UpdateNavigation()
{
	Engine::InputSystem::Input* input = Engine::InputSystem::Input::GetInstance();
	const bool mouseInsideScene = ScreenUtil::IsInsideDebugSceneViewport(input->GetMousePos());
	const Vector2 mousePosition = ScreenUtil::ToGamePosition(input->GetMousePos());

	int32_t hoveredMenuIndex = -1;
	if (mouseInsideScene) {
		for (int32_t index = 0; index < 3; ++index) {
			const Vector2 rectPosition{
				layoutSettings_.menuHitboxPosition.x,
				layoutSettings_.menuHitboxPosition.y + layoutSettings_.menuHitboxStepY * static_cast<float>(index),
			};
			if (IsPointInRect(mousePosition, rectPosition, layoutSettings_.menuHitboxSize)) {
				hoveredMenuIndex = index;
				break;
			}
		}
	}

	const GameMenuInputState menuInput = GameMenuController::Update(input, navigationInputDevice_);
	navigationInputDevice_ = menuInput.device;

	const int32_t previousIndex = menuIndex_;
	switch (navigationInputDevice_) {
	case GameInputBindings::NavigationInputDevice::Mouse:
		if (hoveredMenuIndex >= 0) {
			menuIndex_ = hoveredMenuIndex;
		}
		break;
	case GameInputBindings::NavigationInputDevice::Keyboard:
	case GameInputBindings::NavigationInputDevice::Gamepad:
		if (menuInput.moveDelta != 0) {
			menuIndex_ = std::clamp(menuIndex_ + menuInput.moveDelta, 0, 2);
		}
		break;
	case GameInputBindings::NavigationInputDevice::None:
	default:
		break;
	}

	if (menuIndex_ != previousIndex && selectSeHandle_ != 0) {
		GameAudioCache::Play(selectSeHandle_);
		GameAudioCache::SetVolumeFromTuning(selectSeHandle_, kAudioTitleSelect, 1.0f);
	}

	cursorSprite_.SetPosition({
		layoutSettings_.cursorBasePosition.x,
		layoutSettings_.cursorBasePosition.y + layoutSettings_.cursorStepY * static_cast<float>(menuIndex_)
		});

	const bool confirmTriggered =
		navigationInputDevice_ == GameInputBindings::NavigationInputDevice::Mouse
		? IsMouseMenuConfirm(hoveredMenuIndex)
		: menuInput.confirm;

	if (!confirmTriggered) {
		return;
	}

	if (decideSeHandle_ != 0) {
		GameAudioCache::Play(decideSeHandle_);
		GameAudioCache::SetVolumeFromTuning(decideSeHandle_, kAudioTitleDecide, 1.0f);
	}

	switch (menuIndex_) {
	case 0:
		StartGameTransition();
		break;
	case 1:
		OpenGuide();
		break;
	case 2:
		PostQuitMessage(0);
		break;
	default:
		break;
	}
}

void DirectXGameTitleScene::UpdateAudio()
{
	if (titleBgmHandle_ == 0) {
		return;
	}

	if (!GameAudioCache::IsPlaying(titleBgmHandle_)) {
		GameAudioCache::PlayLoop(titleBgmHandle_);
		GameAudioCache::SetVolumeFromTuning(titleBgmHandle_, kAudioTitleBgm, 0.1f);
	}
}

void DirectXGameTitleScene::UpdateModelAnimation()
{
	animationTime_ += kFixedDeltaTime;

	if (!titleObject_) {
		return;
	}

	titleObject_->SetRotate({ 0.0f, -2.618f, 0.0f });
	titleObject_->SetTranslate(layoutSettings_.modelBasePosition);
}

void DirectXGameTitleScene::UpdateCameraAnimation()
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
	if (cameraManager->GetCamera(kTitleCameraName)) {
		cameraManager->SyncCamera(kTitleCameraName, titleCamera_.get());
		cameraManager->SetActiveCamera(kTitleCameraName);
	}
}

void DirectXGameTitleScene::DrawDebugUi()
{
	TitleSceneDebugUiController::Draw(*this);
}

void DirectXGameTitleScene::SaveLayout() const
{
	UILayoutIO::Save(DataPaths::kTitleLayout,
		{
			{ "titlePosition", { layoutSettings_.titlePosition.x, layoutSettings_.titlePosition.y } },
			{ "titleSize", { layoutSettings_.titleSize.x, layoutSettings_.titleSize.y } },
			{ "cursorBasePosition", { layoutSettings_.cursorBasePosition.x, layoutSettings_.cursorBasePosition.y } },
			{ "cursorSize", { layoutSettings_.cursorSize.x, layoutSettings_.cursorSize.y } },
			{ "cursorStepY", { layoutSettings_.cursorStepY } },
			{ "menuHitboxPosition", { layoutSettings_.menuHitboxPosition.x, layoutSettings_.menuHitboxPosition.y } },
			{ "menuHitboxSize", { layoutSettings_.menuHitboxSize.x, layoutSettings_.menuHitboxSize.y } },
			{ "menuHitboxStepY", { layoutSettings_.menuHitboxStepY } },
			{ "guidePosition", { layoutSettings_.guidePosition.x, layoutSettings_.guidePosition.y } },
			{ "guideSize", { layoutSettings_.guideSize.x, layoutSettings_.guideSize.y } },
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

void DirectXGameTitleScene::ReloadDebugData()
{
	const UILayoutIO::LayoutMap titleLayout = UILayoutIO::LoadOrDefault(DataPaths::kTitleLayout, {});
	layoutSettings_.titlePosition = UILayoutIO::GetVector2(titleLayout, "titlePosition", layoutSettings_.titlePosition);
	layoutSettings_.titleSize = UILayoutIO::GetVector2(titleLayout, "titleSize", layoutSettings_.titleSize);
	layoutSettings_.cursorBasePosition = UILayoutIO::GetVector2(titleLayout, "cursorBasePosition", layoutSettings_.cursorBasePosition);
	layoutSettings_.cursorSize = UILayoutIO::GetVector2(titleLayout, "cursorSize", layoutSettings_.cursorSize);
	layoutSettings_.cursorStepY = UILayoutIO::GetFloat(titleLayout, "cursorStepY", layoutSettings_.cursorStepY);
	layoutSettings_.menuHitboxPosition = UILayoutIO::GetVector2(titleLayout, "menuHitboxPosition", layoutSettings_.menuHitboxPosition);
	layoutSettings_.menuHitboxSize = UILayoutIO::GetVector2(titleLayout, "menuHitboxSize", layoutSettings_.menuHitboxSize);
	layoutSettings_.menuHitboxStepY = UILayoutIO::GetFloat(titleLayout, "menuHitboxStepY", layoutSettings_.menuHitboxStepY);
	layoutSettings_.guidePosition = UILayoutIO::GetVector2(titleLayout, "guidePosition", layoutSettings_.guidePosition);
	layoutSettings_.guideSize = UILayoutIO::GetVector2(titleLayout, "guideSize", layoutSettings_.guideSize);
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

bool DirectXGameTitleScene::IsMouseMenuConfirm(int32_t hoveredMenuIndex) const
{
	return hoveredMenuIndex >= 0 && GameInputBindings::IsMouseConfirmTriggered(Engine::InputSystem::Input::GetInstance());
}

void DirectXGameTitleScene::StartGameTransition()
{
	if (curtain_ && curtain_->GetState() == CurtainTransition::State::None) {
		curtain_->StartClose();
		curtainStarted_ = true;
		if (sessionContext_) {
			sessionContext_->BeginNewRun();
		}
	}
}

void DirectXGameTitleScene::OpenGuide()
{
	guideAlpha_ = 0.0f;
	guideSprite_.SetColor({ 1.0f, 1.0f, 1.0f, guideAlpha_ });
	guideTransitionState_ = GuideTransitionState::FadeIn;
}

void DirectXGameTitleScene::CloseGuide()
{
	guideTransitionState_ = GuideTransitionState::FadeOut;
}

}
