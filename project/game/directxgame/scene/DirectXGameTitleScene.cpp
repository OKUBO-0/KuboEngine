#include "game/directxgame/scene/DirectXGameTitleScene.h"
#include "game/directxgame/core/DirectXGameDataPaths.h"
#include "game/directxgame/core/DirectXGameResourceProbe.h"
#include "game/directxgame/core/GameMenuController.h"
#include "game/directxgame/core/DirectXGameSceneId.h"
#include "game/directxgame/core/DirectXGameSessionContext.h"
#include "game/directxgame/core/GameInputBindings.h"
#include "game/directxgame/core/GameModelCache.h"
#include "game/directxgame/core/ScreenUtil.h"
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
#include <iterator>
#include <string>
#include <string_view>
#ifdef _DEBUG
#include "DebugEditorManager.h"
#include "IconsFontAwesome5.h"
#include <imgui.h>
#endif

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
constexpr float kCubeModelLocalHeight = 2.0f;
constexpr float kTitleLightTopOffset = 25.0f;
const Vector4 kTitlePointColor{ 1.0f, 0.960784f, 0.819608f, 1.0f };
constexpr float kTitlePointIntensity = 2.02f;
constexpr float kTitlePointRadius = 260.0f;
constexpr float kTitlePointDecay = 2.05f;

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
	UpdatePlayerLight();
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

	titleSprite_.Initialize(kTitleTexturePath, layoutSettings_.titlePosition);
	cursorSprite_.Initialize(kCursorTexturePath, layoutSettings_.cursorBasePosition);
	guideSprite_.Initialize(kGuideTexturePath, layoutSettings_.guidePosition);
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

	Engine::Base::TextureManager::GetInstance()->LoadTexture(kEnvironmentTexturePath);

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
	lightSettings_.ApplyTo(*titleObject_);

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
	gridPlane_->SetLightSettings(lightSettings_);
	gridPlane_->Update(layoutSettings_.modelBasePosition);

	UpdatePlayerLight();
}

void DirectXGameTitleScene::InitializeLighting()
{
	const UILayoutIO::LayoutMap tuning = UILayoutIO::LoadOrDefault(DataPaths::kDebugTuning, {});

	DirectionalLight directional{};
	directional.color = GameLightDefaults::kDirectionalColor;
	directional.direction = GameLightDefaults::kDirectionalDirection;
	directional.intensity = 0.0f;
	directional.enable = false;
	lightSettings_.SetDirectionalLight(directional);

	titleLightOffset_ = { 0.0f, UILayoutIO::GetFloat(tuning, "title.light.height", kTitleLightTopOffset), 0.0f };
	titleLightOffset_.x = 0.0f;
	titleLightOffset_.z = 0.0f;

	PointLight point{};
	const Vector3 pointColor = UILayoutIO::GetVector3(
		tuning,
		"title.light.pointColor",
		{ kTitlePointColor.x, kTitlePointColor.y, kTitlePointColor.z });
	point.color = { pointColor.x, pointColor.y, pointColor.z, kTitlePointColor.w };
	point.position = {
		layoutSettings_.modelBasePosition.x,
		layoutSettings_.modelBasePosition.y + layoutSettings_.modelScale.y * kCubeModelLocalHeight + titleLightOffset_.y,
		layoutSettings_.modelBasePosition.z,
	};
	point.intensity = UILayoutIO::GetFloat(tuning, "title.light.pointIntensity", kTitlePointIntensity);
	point.radius = UILayoutIO::GetFloat(tuning, "title.light.pointRadius", kTitlePointRadius);
	point.decay = UILayoutIO::GetFloat(tuning, "title.light.pointDecay", kTitlePointDecay);
	point.enable = UILayoutIO::GetFloat(tuning, "title.light.pointEnabled", 1.0f) > 0.5f;
	lightSettings_.SetPointLight(point);
	lightSettings_.SetLightingEnabled(true);
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

void DirectXGameTitleScene::UpdatePlayerLight()
{
	PointLight point = lightSettings_.GetPointLight();
	Vector3 position = layoutSettings_.modelBasePosition;
	if (titleObject_) {
		position = titleObject_->GetTransform().translate;
	}
	const Vector3 scale = titleObject_ ? titleObject_->GetTransform().scale : layoutSettings_.modelScale;
	point.position = {
		position.x,
		position.y + scale.y * kCubeModelLocalHeight + titleLightOffset_.y,
		position.z,
	};
	lightSettings_.SetPointLight(point);

	if (titleObject_) {
		lightSettings_.ApplyTo(*titleObject_);
	}
	if (gridPlane_) {
		gridPlane_->SetLightSettings(lightSettings_);
	}
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
#ifdef _DEBUG
	Engine::InputSystem::Input* input = Engine::InputSystem::Input::GetInstance();
	if (input && input->TriggerKey(DIK_F5)) {
		ReloadDebugData();
	}

	const Engine::Editor::DebugEditorMenuItem windowItems[] = {
		{ "Scene", &debugWindows_.titleView },
		{ "統計", &debugWindows_.statisticsView },
		{ "シーン設定", &debugWindows_.titleSettings },
		{ "オフスクリーン設定", &debugWindows_.offscreenSettings },
		{ "ライト設定", &debugWindows_.lightSettings },
		{ "オーディオ", &debugWindows_.audio },
		{ "キー操作デバッグ", &debugWindows_.keyInputDebug },
	};
	const Engine::Editor::DebugEditorMenuItem editItems[] = {
		{ "シーン設定", &debugWindows_.titleSettings },
		{ "ライト設定", &debugWindows_.lightSettings },
	};
	const Engine::Editor::DebugEditorMenuItem objectItems[] = {
		{ "Scene", &debugWindows_.titleView },
		{ "シーン設定", &debugWindows_.titleSettings },
	};
	Engine::Editor::DebugEditorManager::DrawMainMenu({
		windowItems,
		std::size(windowItems),
		editItems,
		std::size(editItems),
		objectItems,
		std::size(objectItems),
		"タイトルレイアウトを保存",
		[this]() { SaveLayout(); },
		"タイトル設定を再読み込み",
		[this]() { ReloadDebugData(); },
		"Title Debug UI",
		{},
		[this]() { StartGameTransition(); },
		{},
		&debugWindows_.windowSwitcher,
	});

	if (debugWindows_.windowSwitcher) {
		Engine::Editor::DebugEditorManager::DrawWindowSwitcher(
			"ウィンドウ表示切り替え",
			&debugWindows_.windowSwitcher,
			windowItems,
			std::size(windowItems),
			{ 260.0f, 220.0f });
	}
	if (debugWindows_.offscreenSettings) {
		if (Engine::Base::OffscreenRenderManager* offscreen = Engine::Base::OffscreenRenderManager::GetInstance()) {
			offscreen->DrawImGui();
		}
	}
	if (debugWindows_.titleView) {
	const Engine::Editor::DebugSceneViewportState sceneViewport =
		Engine::Editor::DebugEditorManager::DrawSceneViewport(&debugWindows_.titleView);
	ScreenUtil::SetDebugSceneInputActive(sceneViewport.inputActive);
	if (sceneViewport.drawn) {
		ScreenUtil::SetDebugSceneViewport(sceneViewport.min, sceneViewport.size);
	} else {
		ScreenUtil::ClearDebugSceneViewport();
	}
	} else {
		ScreenUtil::ClearDebugSceneViewport();
	}

	if (debugWindows_.audio) {
	ImGui::Begin("オーディオ", &debugWindows_.audio);
	if (debugWindows_.audio) {
		float masterVolume = GameAudioCache::GetMasterVolume();
		if (ImGui::SliderFloat("Master Volume", &masterVolume, 0.0f, 1.0f)) {
			GameAudioCache::SetMasterVolume(masterVolume);
		}
		if (ImGui::CollapsingHeader("Audio Balance")) {
			float titleBgm = GameAudioCache::GetTunedVolume(kAudioTitleBgm, 0.1f);
			if (ImGui::SliderFloat("Title BGM", &titleBgm, 0.0f, 1.0f)) {
				GameAudioCache::SetTunedVolume(kAudioTitleBgm, titleBgm);
				if (titleBgmHandle_ != 0) {
					GameAudioCache::SetVolumeFromTuning(titleBgmHandle_, kAudioTitleBgm, 0.1f);
				}
			}
			float selectSe = GameAudioCache::GetTunedVolume(kAudioTitleSelect, 1.0f);
			if (ImGui::SliderFloat("Title Select", &selectSe, 0.0f, 1.0f)) {
				GameAudioCache::SetTunedVolume(kAudioTitleSelect, selectSe);
			}
			float decideSe = GameAudioCache::GetTunedVolume(kAudioTitleDecide, 1.0f);
			if (ImGui::SliderFloat("Title Decide", &decideSe, 0.0f, 1.0f)) {
				GameAudioCache::SetTunedVolume(kAudioTitleDecide, decideSe);
			}
		}
	}
	ImGui::End();
	}

	if (debugWindows_.statisticsView) {
	ImGui::Begin("統計", &debugWindows_.statisticsView);
	const DirectXGameResourceProbeStatus& probeStatus = DirectXGameResourceProbe::Verify();
	ImGui::Text("Texture Probe: %s", probeStatus.textureLoaded ? "OK" : "NG");
	ImGui::Text("Model Probe: %s", probeStatus.modelLoaded ? "OK" : "NG");
	ImGui::Text("CSV Probe: %s", probeStatus.csvOpened ? "OK" : "NG");
	ImGui::Text("Required Assets: %s (%zu checked, %zu missing)",
		probeStatus.requiredAssetsReady ? "OK" : "NG",
		probeStatus.requiredAssetCount,
		probeStatus.missingRequiredAssets.size());
	if (!probeStatus.missingRequiredAssets.empty() && ImGui::TreeNode("Missing DirectXGame Assets")) {
		for (const std::string& path : probeStatus.missingRequiredAssets) {
			ImGui::TextUnformatted(path.c_str());
		}
		ImGui::TreePop();
	}
	ImGui::End();
	}

	if (debugWindows_.titleSettings) {
	ImGui::Begin("シーン設定", &debugWindows_.titleSettings);
	ImGui::Checkbox("Enable Title Debug", &layoutSettings_.debugEnabled);
	if (layoutSettings_.debugEnabled) {
		float titlePosition[2]{ layoutSettings_.titlePosition.x, layoutSettings_.titlePosition.y };
		if (ImGui::DragFloat2("Title Position", titlePosition, 1.0f, -400.0f, 1280.0f)) {
			layoutSettings_.titlePosition = { titlePosition[0], titlePosition[1] };
			ApplyLayout();
		}

		float titleSize[2]{ layoutSettings_.titleSize.x, layoutSettings_.titleSize.y };
		if (ImGui::DragFloat2("Title Size", titleSize, 1.0f, 64.0f, 1280.0f)) {
			layoutSettings_.titleSize = { titleSize[0], titleSize[1] };
			ApplyLayout();
		}

		float cursorPosition[2]{ layoutSettings_.cursorBasePosition.x, layoutSettings_.cursorBasePosition.y };
		if (ImGui::DragFloat2("Cursor Base", cursorPosition, 1.0f, -400.0f, 1280.0f)) {
			layoutSettings_.cursorBasePosition = { cursorPosition[0], cursorPosition[1] };
			ApplyLayout();
		}

		float cursorSize[2]{ layoutSettings_.cursorSize.x, layoutSettings_.cursorSize.y };
		if (ImGui::DragFloat2("Cursor Size", cursorSize, 1.0f, 64.0f, 1280.0f)) {
			layoutSettings_.cursorSize = { cursorSize[0], cursorSize[1] };
			ApplyLayout();
		}

		if (ImGui::DragFloat("Cursor Step", &layoutSettings_.cursorStepY, 1.0f, 16.0f, 320.0f)) {
			ApplyLayout();
		}

		float hitboxPosition[2]{ layoutSettings_.menuHitboxPosition.x, layoutSettings_.menuHitboxPosition.y };
		if (ImGui::DragFloat2("Menu Hitbox Pos", hitboxPosition, 1.0f, -400.0f, 1280.0f)) {
			layoutSettings_.menuHitboxPosition = { hitboxPosition[0], hitboxPosition[1] };
		}

		float hitboxSize[2]{ layoutSettings_.menuHitboxSize.x, layoutSettings_.menuHitboxSize.y };
		if (ImGui::DragFloat2("Menu Hitbox Size", hitboxSize, 1.0f, 16.0f, 640.0f)) {
			layoutSettings_.menuHitboxSize = { hitboxSize[0], hitboxSize[1] };
		}

		ImGui::DragFloat("Menu Hitbox Step", &layoutSettings_.menuHitboxStepY, 1.0f, 16.0f, 320.0f);

		float guidePosition[2]{ layoutSettings_.guidePosition.x, layoutSettings_.guidePosition.y };
		if (ImGui::DragFloat2("Guide Position", guidePosition, 1.0f, -400.0f, 1280.0f)) {
			layoutSettings_.guidePosition = { guidePosition[0], guidePosition[1] };
			ApplyLayout();
		}

		float guideSize[2]{ layoutSettings_.guideSize.x, layoutSettings_.guideSize.y };
		if (ImGui::DragFloat2("Guide Size", guideSize, 1.0f, 64.0f, 1280.0f)) {
			layoutSettings_.guideSize = { guideSize[0], guideSize[1] };
			ApplyLayout();
		}

		float modelPosition[3]{
			layoutSettings_.modelBasePosition.x,
			layoutSettings_.modelBasePosition.y,
			layoutSettings_.modelBasePosition.z,
		};
		if (ImGui::DragFloat3("Model Position", modelPosition, 0.1f, -40.0f, 40.0f)) {
			layoutSettings_.modelBasePosition = { modelPosition[0], modelPosition[1], modelPosition[2] };
			ApplyLayout();
		}

		float modelScale[3]{
			layoutSettings_.modelScale.x,
			layoutSettings_.modelScale.y,
			layoutSettings_.modelScale.z,
		};
		if (ImGui::DragFloat3("Model Scale", modelScale, 0.1f, 0.5f, 10.0f)) {
			layoutSettings_.modelScale = { modelScale[0], modelScale[1], modelScale[2] };
			ApplyLayout();
		}

		if (ImGui::CollapsingHeader("Title Light", ImGuiTreeNodeFlags_DefaultOpen)) {
			PointLight point = lightSettings_.GetPointLight();
			bool lightChanged = false;

			bool pointEnabled = point.enable != 0;
			if (ImGui::Checkbox("Title Light Enabled", &pointEnabled)) {
				point.enable = pointEnabled;
				lightChanged = true;
			}
			if (ImGui::DragFloat("Title Light Height", &titleLightOffset_.y, 0.25f, -8.0f, 80.0f)) {
				lightChanged = true;
			}
			float pointColor[3]{ point.color.x, point.color.y, point.color.z };
			if (ImGui::ColorEdit3("Title Light Color", pointColor)) {
				point.color = { pointColor[0], pointColor[1], pointColor[2], point.color.w };
				lightChanged = true;
			}
			if (ImGui::SliderFloat("Title Light Intensity", &point.intensity, 0.0f, 12.0f)) {
				lightChanged = true;
			}
			if (ImGui::DragFloat("Title Light Radius", &point.radius, 0.5f, 1.0f, 260.0f)) {
				lightChanged = true;
			}
			if (ImGui::DragFloat("Title Light Decay", &point.decay, 0.05f, 0.1f, 8.0f)) {
				lightChanged = true;
			}
			if (ImGui::Button("Apply Title Reference Light")) {
				titleLightOffset_ = { 0.0f, kTitleLightTopOffset, 0.0f };
				point.color = kTitlePointColor;
				point.intensity = kTitlePointIntensity;
				point.radius = kTitlePointRadius;
				point.decay = kTitlePointDecay;
				point.enable = true;
				lightChanged = true;
			}
			if (lightChanged) {
				lightSettings_.SetPointLight(point);
				UpdatePlayerLight();
			}
		}

		float cameraTarget[3]{
			layoutSettings_.cameraTarget.x,
			layoutSettings_.cameraTarget.y,
			layoutSettings_.cameraTarget.z,
		};
		if (ImGui::DragFloat3("Camera Target Offset", cameraTarget, 0.1f, -60.0f, 60.0f)) {
			layoutSettings_.cameraTarget = { cameraTarget[0], cameraTarget[1], cameraTarget[2] };
			ApplyLayout();
		}

		if (ImGui::DragFloat("Camera Distance", &layoutSettings_.cameraDistance, 0.5f, 8.0f, 120.0f)) {
			ApplyLayout();
		}
		if (ImGui::DragFloat("Camera Height", &layoutSettings_.cameraHeight, 0.5f, -20.0f, 80.0f)) {
			ApplyLayout();
		}
		if (ImGui::DragFloat("Camera Pitch", &layoutSettings_.cameraPitch, 0.01f, -1.2f, 1.2f)) {
			ApplyLayout();
		}
		if (ImGui::DragFloat("Camera Yaw", &layoutSettings_.cameraYaw, 0.01f, -6.28f, 6.28f)) {
			ApplyLayout();
		}
		ImGui::DragFloat("Camera Orbit Speed", &layoutSettings_.cameraOrbitSpeed, 0.01f, -1.0f, 1.0f);

		if (ImGui::Button("Save Title Layout")) {
			SaveLayout();
		}
	}
	ImGui::End();
	}

	if (debugWindows_.keyInputDebug) {
	ImGui::Begin("キー操作デバッグ", &debugWindows_.keyInputDebug);
	ImGui::TextUnformatted("タイトルシーン入力");
	ImGui::Text("Input Device: %s", GameInputBindings::ToDisplayName(navigationInputDevice_));
	ImGui::Text("Confirm: %s", GameInputBindings::GetConfirmLabel(navigationInputDevice_));
	ImGui::Text("Cancel: %s", GameInputBindings::GetCancelLabel(navigationInputDevice_));
	ImGui::Text("Menu Index: %d", menuIndex_);
	ImGui::Text("Guide Active: %s", guideActive_ ? "true" : "false");
	ImGui::End();
	}
#endif
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
	UpdatePlayerLight();
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
