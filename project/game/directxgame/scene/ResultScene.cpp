#include "ResultScene.h"
#include "DirectXCommon.h"
#include "DataPaths.h"
#include "GameAudioTuning.h"
#include "GameMenuController.h"
#include "SceneId.h"
#include "GameSession.h"
#include "GameSpriteFactory.h"
#include "ResultSceneDebugUIController.h"
#include "UILayoutIO.h"
#include "CameraManager.h"
#include "Input.h"
#include "Object3DCommon.h"
#include "OffscreenRenderManager.h"
#include "SceneManager.h"
#include "SpriteCommon.h"
#include "SrvManager.h"
#include "SceneLighting.h"
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <string_view>

namespace {

constexpr char kResultFinishSePath[] = "se/result_finish.wav";
constexpr char kDecideSePath[] = "se/ui_decide.wav";
constexpr char kAudioResultFinish[] = "result.finish";
constexpr char kAudioUiDecide[] = "ui.decide";
constexpr char kResultBackgroundTexture[] = "ui/result/Result.png";
constexpr char kResultFinishTexture[] = "ui/result/finish_ui.png";
constexpr char kResultNumberTexture[] = "ui/font/noto_sans_jp_black.png";
constexpr char kResultNumberMetadata[] = "ui/font/noto_sans_jp_black.json";
constexpr char kResultCameraName[] = "directxgame_result";
constexpr int32_t kScorePerExp = 1;
constexpr int32_t kScorePerKill = 100;
constexpr int32_t kScorePerLevel = 1000;
constexpr int32_t kScorePerCoin = 25;
constexpr Vector4 kPanelColor{ 0.0f, 0.0f, 0.0f, 0.72f };
constexpr Vector4 kDimColor{ 0.0f, 0.0f, 0.0f, 0.18f };
constexpr Vector4 kFrameColor{ 1.0f, 0.96f, 0.0f, 1.0f };
constexpr Vector4 kRowColor{ 0.0f, 0.0f, 0.0f, 0.54f };

int32_t CalculateTotalScore(int32_t exp, int32_t level, int32_t kill, int32_t coins)
{
	return exp * kScorePerExp +
		kill * kScorePerKill +
		level * kScorePerLevel +
		coins * kScorePerCoin;
}

void SetPanelLayout(DirectXGame::UIPanel& panel, const Vector2& position, const Vector2& size, const Vector4& color)
{
	panel.SetPosition(position);
	panel.SetSize(size);
	panel.SetColor(color);
}

template <size_t N>
void SetBorderLayout(
	std::array<DirectXGame::UIPanel, N>& borders,
	size_t offset,
	const Vector2& position,
	const Vector2& size,
	float thickness,
	const Vector4& color)
{
	SetPanelLayout(borders[offset + 0], position, { size.x, thickness }, color);
	SetPanelLayout(borders[offset + 1], { position.x, position.y + size.y - thickness }, { size.x, thickness }, color);
	SetPanelLayout(borders[offset + 2], position, { thickness, size.y }, color);
	SetPanelLayout(borders[offset + 3], { position.x + size.x - thickness, position.y }, { thickness, size.y }, color);
}

void ConfigureText(DirectXGame::BitmapText& text, float scale, const Vector4& color)
{
	text.SetScale(scale);
	text.SetAdvanceMultiplier(0.92f);
	text.SetColor(color);
}

const char* ResultPromptText(DirectXGame::GameInputBindings::NavigationInputDevice device)
{
	return device == DirectXGame::GameInputBindings::NavigationInputDevice::Gamepad
		? "--Press Any Button--"
		: "--Press Any Click--";
}

}

namespace DirectXGame {

ResultScene::ResultScene(std::shared_ptr<GameSession> sessionContext)
	: sessionContext_(std::move(sessionContext))
{
}

void ResultScene::Initialize()
{
	LoadGameAudioTuning();
	GameAudioCache::StopBus(AudioBus::Bgm);
	Engine::CameraSystem::CameraManager::GetInstance()->Initialize();
	if (Engine::Base::OffscreenRenderManager* offscreen = Engine::Base::OffscreenRenderManager::GetInstance()) {
		offscreen->SetScenePostEffectType(PostEffectType::Fullscreen);
	}

	if (sessionContext_) {
		sessionContext_->OnEnterResultScene();
	}

	InitializeWorldBackground();
	InitializeUi();
}

void ResultScene::Finalize()
{
	if (finishSeHandle_) {
		GameAudioCache::Stop(finishSeHandle_);
	}
	if (decideSeHandle_) {
		GameAudioCache::Stop(decideSeHandle_);
	}
}

void ResultScene::Update()
{
	if (sessionContext_ && sessionContext_->IsSceneStressEnabled()) {
		if (sessionContext_->IsSceneStressReportWritten()) {
			PostQuitMessage(0);
			return;
		}
		sessionContext_->AdvanceSceneStressFrame();
		if (Engine::Base::SrvManager* srvManager =
			Engine::Graphics3D::Object3DCommon::GetInstance()->GetSrvManager()) {
			const Engine::Base::SrvManager::UsageSummary usage =
				srvManager->GetUsageSummary();
			sessionContext_->RecordSrvUsage(
				GameSession::SceneStressStage::Result,
				srvManager->GetUsedCount(),
				srvManager->GetHighWatermark(),
				usage.texture2D,
				usage.textureCube,
				usage.structuredBuffer,
				usage.shadowMap,
				usage.other);
		}
		if (sessionContext_->GetSceneStressFrameCount() >= 30) {
			if (sessionContext_->IsSceneStressComplete()) {
				auto dxCommon =
					Engine::Graphics3D::Object3DCommon::GetInstance()->GetDxCommon();
				sessionContext_->RecordGpuTimingSummary(
					dxCommon->GetAverageFrameGpuMilliseconds(),
					dxCommon->GetAverageShadowGpuMilliseconds(),
					dxCommon->GetFrameGpuP95Milliseconds(),
					dxCommon->GetShadowGpuP95Milliseconds(),
					dxCommon->GetFrameGpuMaxMilliseconds(),
					dxCommon->GetShadowGpuMaxMilliseconds(),
					dxCommon->GetFrameGpuSampleCount(),
					dxCommon->GetShadowGpuSampleCount());
				Engine::Graphics3D::Object3DCommon* objectCommon =
					Engine::Graphics3D::Object3DCommon::GetInstance();
				sessionContext_->RecordShadowPassSummary(
					objectCommon->GetMeasuredShadowPassCount(),
					objectCommon->GetTotalShadowCandidateCount(),
					objectCommon->GetTotalShadowSubmittedCount(),
					objectCommon->GetTotalShadowCulledCount(),
					objectCommon->GetShadowMapSize(),
					objectCommon->GetShadowMapMemoryBytes(),
					objectCommon->GetShadowArea());
				sessionContext_->WriteSceneStressReport();
				PostQuitMessage(0);
			} else {
				Engine::Scene::SceneManager::GetInstance()->ChangeScene(
					SceneId::kTitle);
			}
			return;
		}
	}

	const GameMenuInputState menuInput = GameMenuController::Update(
		Engine::InputSystem::Input::GetInstance(),
		navigationInputDevice_);
	navigationInputDevice_ = menuInput.device;
	promptText_.SetText(ResultPromptText(navigationInputDevice_));
	promptText_.SetScaleToFit(0.34f, panelSize_.x - 56.0f);
	promptText_.SetPosition({
		panelPosition_.x + (panelSize_.x - promptText_.MeasureWidth()) * 0.5f,
		panelPosition_.y + panelSize_.y - 70.0f,
		});

	if (Engine::CameraSystem::CameraManager::GetInstance()->GetActiveCamera()) {
		Engine::CameraSystem::CameraManager::GetInstance()->GetActiveCamera()->Update();
	}

	constexpr float kFixedDeltaTime = 1.0f / 60.0f;
	resultAnimationTime_ += kFixedDeltaTime;
	UpdateCountUp(kFixedDeltaTime);
	UpdateFinishUiPulse();
	UpdateCurtain(kFixedDeltaTime);

	if (pendingSceneId_.empty() && (menuInput.confirm || menuInput.cancel)) {
		if (!IsCountUpFinished()) {
			FinishCountUp();
		} else {
			if (decideSeHandle_) {
				GameAudioCache::PlayTuned(decideSeHandle_, kAudioUiDecide, 0.72f);
			}
			RequestSceneChange(SceneId::kTitle);
		}
	}

	DrawDebugUI();
}

void ResultScene::DrawDebugUI()
{
	ResultSceneDebugUIController::Draw(*this);
}

void ResultScene::Draw()
{
	DrawWorldBackground();
	Engine::Graphics2D::SpriteCommon::GetInstance()->CommonDraw();
	DrawResultPanels();
	expText_.Draw();
	levelText_.Draw();
	killText_.Draw();
	coinText_.Draw();
	totalScoreText_.Draw();
	promptText_.Draw();
	if (curtain_) {
		curtain_->Draw();
	}
}

void ResultScene::InitializeUi()
{
	const UILayoutIO::LayoutMap layout = UILayoutIO::LoadOrDefault(DataPaths::kResultLayout, {});
	backgroundPosition_ = UILayoutIO::GetVector2(layout, "backgroundPosition", backgroundPosition_);
	backgroundSize_ = UILayoutIO::GetVector2(layout, "backgroundSize", backgroundSize_);
	resultPosition_ = UILayoutIO::GetVector2(layout, "resultUIPosition", UILayoutIO::GetVector2(layout, "resultPosition", resultPosition_));
	resultSize_ = UILayoutIO::GetVector2(layout, "resultUISize", UILayoutIO::GetVector2(layout, "resultSize", resultSize_));
	finishPosition_ = UILayoutIO::GetVector2(layout, "finishUIPosition", finishPosition_);
	finishSize_ = UILayoutIO::GetVector2(layout, "finishUISize", finishSize_);
	expPosition_ = UILayoutIO::GetVector2(layout, "expPosition", expPosition_);
	levelPosition_ = UILayoutIO::GetVector2(layout, "levelPosition", levelPosition_);
	killPosition_ = UILayoutIO::GetVector2(layout, "killPosition", killPosition_);
	coinPosition_ = UILayoutIO::GetVector2(layout, "coinPosition", coinPosition_);
	totalScorePosition_ = UILayoutIO::GetVector2(layout, "totalScorePosition", totalScorePosition_);
	digitSize_ = UILayoutIO::GetVector2(layout, "digitSize", digitSize_);
	panelPosition_ = UILayoutIO::GetVector2(layout, "panelPosition", panelPosition_);
	panelSize_ = UILayoutIO::GetVector2(layout, "panelSize", panelSize_);
	scoreScale_ = UILayoutIO::GetFloat(layout, "scoreScale", scoreScale_);
#ifdef _DEBUG
	auto loadWindowVisible = [&layout](std::string_view key, bool fallback) {
		return UILayoutIO::GetFloat(layout, key, fallback ? 1.0f : 0.0f) != 0.0f;
	};
	debugWindows_.windowSwitcher = loadWindowVisible("debug.windowSwitcher", debugWindows_.windowSwitcher);
	debugWindows_.sceneView = loadWindowVisible("debug.sceneView", debugWindows_.sceneView);
	debugWindows_.statisticsView = loadWindowVisible("debug.statisticsView", debugWindows_.statisticsView);
	debugWindows_.sceneSettings = loadWindowVisible("debug.sceneSettings", debugWindows_.sceneSettings);
	debugWindows_.audio = loadWindowVisible("debug.audio", debugWindows_.audio);
	debugWindows_.keyInputDebug = loadWindowVisible("debug.keyInputDebug", debugWindows_.keyInputDebug);
#endif

	GameTextureCache::LoadBatch({
		"white1x1.png",
		kResultNumberTexture,
		});
	InitializeResultPanels();
	for (BitmapText* text : {
		&titleText_, &expLabelText_, &levelLabelText_, &killLabelText_,
		&coinLabelText_, &totalScoreLabelText_, &promptText_,
		&expText_, &levelText_, &killText_, &coinText_, &totalScoreText_ }) {
		text->Initialize(kResultNumberTexture, kResultNumberMetadata);
	}
	titleText_.SetText("RESULT");
	expLabelText_.SetText("EXP");
	levelLabelText_.SetText("LEVEL");
	killLabelText_.SetText("KILLS");
	coinLabelText_.SetText("COINS");
	totalScoreLabelText_.SetText("TOTAL");
	promptText_.SetText(ResultPromptText(navigationInputDevice_));
	ConfigureText(titleText_, 0.56f, { 1.0f, 0.96f, 0.0f, 1.0f });
	ConfigureText(expLabelText_, 0.30f, { 1.0f, 1.0f, 1.0f, 0.96f });
	ConfigureText(levelLabelText_, 0.30f, { 1.0f, 1.0f, 1.0f, 0.96f });
	ConfigureText(killLabelText_, 0.30f, { 1.0f, 1.0f, 1.0f, 0.96f });
	ConfigureText(coinLabelText_, 0.30f, { 1.0f, 1.0f, 1.0f, 0.96f });
	ConfigureText(totalScoreLabelText_, 0.34f, { 1.0f, 0.96f, 0.0f, 1.0f });
	ConfigureText(promptText_, 0.34f, { 1.0f, 1.0f, 1.0f, 0.92f });
	ApplyLayout();
	curtain_ = std::make_unique<CurtainTransition>();
	curtain_->Initialize();
	curtain_->StartOpen(20.0f);

	finishSeHandle_ = GameAudioCache::LoadWave(kResultFinishSePath);
	decideSeHandle_ = GameAudioCache::LoadWave(kDecideSePath);

	FinishCountUp();
	displayedExp_ = 0.0f;
	displayedLevel_ = 0.0f;
	displayedKills_ = 0.0f;
	displayedCoins_ = 0.0f;
	displayedTotalScore_ = 0.0f;
	countUpFinished_ = false;
	finishSePlayed_ = false;
	SetNumberText(expText_, expPosition_, 0);
	SetNumberText(levelText_, levelPosition_, 0);
	SetNumberText(killText_, killPosition_, 0);
	SetNumberText(coinText_, coinPosition_, 0);
	SetNumberText(totalScoreText_, totalScorePosition_, 0);
}

void ResultScene::ApplyLayout()
{
	ApplyResultPanelLayout();
	SetNumberText(expText_, expPosition_, static_cast<int32_t>(displayedExp_));
	SetNumberText(levelText_, levelPosition_, static_cast<int32_t>(displayedLevel_));
	SetNumberText(killText_, killPosition_, static_cast<int32_t>(displayedKills_));
	SetNumberText(coinText_, coinPosition_, static_cast<int32_t>(displayedCoins_));
	SetNumberText(totalScoreText_, totalScorePosition_, static_cast<int32_t>(displayedTotalScore_));
}

void ResultScene::SaveLayout() const
{
	UILayoutIO::Save(DataPaths::kResultLayout,
		{
			{ "backgroundPosition", { backgroundPosition_.x, backgroundPosition_.y } },
			{ "backgroundSize", { backgroundSize_.x, backgroundSize_.y } },
			{ "resultUIPosition", { resultPosition_.x, resultPosition_.y } },
			{ "resultUISize", { resultSize_.x, resultSize_.y } },
			{ "finishUIPosition", { finishPosition_.x, finishPosition_.y } },
			{ "finishUISize", { finishSize_.x, finishSize_.y } },
			{ "expPosition", { expPosition_.x, expPosition_.y } },
			{ "levelPosition", { levelPosition_.x, levelPosition_.y } },
			{ "killPosition", { killPosition_.x, killPosition_.y } },
			{ "coinPosition", { coinPosition_.x, coinPosition_.y } },
			{ "totalScorePosition", { totalScorePosition_.x, totalScorePosition_.y } },
			{ "digitSize", { digitSize_.x, digitSize_.y } },
			{ "panelPosition", { panelPosition_.x, panelPosition_.y } },
			{ "panelSize", { panelSize_.x, panelSize_.y } },
			{ "scoreScale", { scoreScale_ } },
#ifdef _DEBUG
			{ "debug.windowSwitcher", { debugWindows_.windowSwitcher ? 1.0f : 0.0f } },
			{ "debug.sceneView", { debugWindows_.sceneView ? 1.0f : 0.0f } },
			{ "debug.statisticsView", { debugWindows_.statisticsView ? 1.0f : 0.0f } },
			{ "debug.sceneSettings", { debugWindows_.sceneSettings ? 1.0f : 0.0f } },
			{ "debug.audio", { debugWindows_.audio ? 1.0f : 0.0f } },
			{ "debug.keyInputDebug", { debugWindows_.keyInputDebug ? 1.0f : 0.0f } },
#endif
		});
}

void ResultScene::ReloadDebugData()
{
	const UILayoutIO::LayoutMap layout = UILayoutIO::LoadOrDefault(DataPaths::kResultLayout, {});
	backgroundPosition_ = UILayoutIO::GetVector2(layout, "backgroundPosition", backgroundPosition_);
	backgroundSize_ = UILayoutIO::GetVector2(layout, "backgroundSize", backgroundSize_);
	resultPosition_ = UILayoutIO::GetVector2(layout, "resultUIPosition", UILayoutIO::GetVector2(layout, "resultPosition", resultPosition_));
	resultSize_ = UILayoutIO::GetVector2(layout, "resultUISize", UILayoutIO::GetVector2(layout, "resultSize", resultSize_));
	finishPosition_ = UILayoutIO::GetVector2(layout, "finishUIPosition", finishPosition_);
	finishSize_ = UILayoutIO::GetVector2(layout, "finishUISize", finishSize_);
	expPosition_ = UILayoutIO::GetVector2(layout, "expPosition", expPosition_);
	levelPosition_ = UILayoutIO::GetVector2(layout, "levelPosition", levelPosition_);
	killPosition_ = UILayoutIO::GetVector2(layout, "killPosition", killPosition_);
	coinPosition_ = UILayoutIO::GetVector2(layout, "coinPosition", coinPosition_);
	totalScorePosition_ = UILayoutIO::GetVector2(layout, "totalScorePosition", totalScorePosition_);
	digitSize_ = UILayoutIO::GetVector2(layout, "digitSize", digitSize_);
	panelPosition_ = UILayoutIO::GetVector2(layout, "panelPosition", panelPosition_);
	panelSize_ = UILayoutIO::GetVector2(layout, "panelSize", panelSize_);
	scoreScale_ = UILayoutIO::GetFloat(layout, "scoreScale", scoreScale_);
#ifdef _DEBUG
	auto loadWindowVisible = [&layout](std::string_view key, bool fallback) {
		return UILayoutIO::GetFloat(layout, key, fallback ? 1.0f : 0.0f) != 0.0f;
	};
	debugWindows_.windowSwitcher = loadWindowVisible("debug.windowSwitcher", debugWindows_.windowSwitcher);
	debugWindows_.sceneView = loadWindowVisible("debug.sceneView", debugWindows_.sceneView);
	debugWindows_.statisticsView = loadWindowVisible("debug.statisticsView", debugWindows_.statisticsView);
	debugWindows_.sceneSettings = loadWindowVisible("debug.sceneSettings", debugWindows_.sceneSettings);
	debugWindows_.audio = loadWindowVisible("debug.audio", debugWindows_.audio);
	debugWindows_.keyInputDebug = loadWindowVisible("debug.keyInputDebug", debugWindows_.keyInputDebug);
#endif
	ApplyLayout();
}

void ResultScene::UpdateCountUp(float deltaTime)
{
	if (!sessionContext_ || countUpFinished_) {
		return;
	}

	const RunResult& resultData = sessionContext_->GetResultData();
	const float stepScale = deltaTime * 2.4f;
	displayedExp_ = (std::min)(static_cast<float>(resultData.totalExp), displayedExp_ + (std::max)(1.0f, static_cast<float>(resultData.totalExp) * stepScale));
	displayedLevel_ = (std::min)(static_cast<float>(resultData.finalLevel), displayedLevel_ + (std::max)(1.0f, static_cast<float>(resultData.finalLevel) * stepScale));
	displayedKills_ = (std::min)(static_cast<float>(resultData.totalKillCount), displayedKills_ + (std::max)(1.0f, static_cast<float>(resultData.totalKillCount) * stepScale));
	displayedCoins_ = (std::min)(static_cast<float>(resultData.coins), displayedCoins_ + (std::max)(1.0f, static_cast<float>(resultData.coins) * stepScale));
	const int32_t totalScore = CalculateTotalScore(resultData.totalExp, resultData.finalLevel, resultData.totalKillCount, resultData.coins);
	displayedTotalScore_ = (std::min)(static_cast<float>(totalScore), displayedTotalScore_ + (std::max)(1.0f, static_cast<float>(totalScore) * stepScale));

	const float countPulse = 1.0f + (0.5f + 0.5f * std::sin(resultAnimationTime_ * 12.0f)) * 0.055f;
	SetNumberText(expText_, expPosition_, static_cast<int32_t>(displayedExp_), countPulse, 0.92f);
	SetNumberText(levelText_, levelPosition_, static_cast<int32_t>(displayedLevel_), countPulse, 0.92f);
	SetNumberText(killText_, killPosition_, static_cast<int32_t>(displayedKills_), countPulse, 0.92f);
	SetNumberText(coinText_, coinPosition_, static_cast<int32_t>(displayedCoins_), countPulse, 0.92f);
	SetNumberText(totalScoreText_, totalScorePosition_, static_cast<int32_t>(displayedTotalScore_), countPulse, 1.0f);

	countUpFinished_ =
		static_cast<int32_t>(displayedExp_) >= resultData.totalExp &&
		static_cast<int32_t>(displayedLevel_) >= resultData.finalLevel &&
		static_cast<int32_t>(displayedKills_) >= resultData.totalKillCount &&
		static_cast<int32_t>(displayedCoins_) >= resultData.coins &&
		static_cast<int32_t>(displayedTotalScore_) >= totalScore;
	if (countUpFinished_ && !finishSePlayed_ && finishSeHandle_) {
		GameAudioCache::PlayTuned(finishSeHandle_, kAudioResultFinish, 0.72f);
		finishSePlayed_ = true;
	}
}

void ResultScene::UpdateFinishUiPulse()
{
	if (!countUpFinished_) {
		finishUi_.SetAlpha(0.0f);
		return;
	}

	const float pulse = 0.5f + 0.5f * std::sin(resultAnimationTime_ * 4.8f);
	finishUi_.SetAlpha(0.62f + pulse * 0.38f);
	finishUi_.SetScale(1.0f + pulse * 0.035f);
	SetNumberText(expText_, expPosition_, static_cast<int32_t>(displayedExp_));
	SetNumberText(levelText_, levelPosition_, static_cast<int32_t>(displayedLevel_));
	SetNumberText(killText_, killPosition_, static_cast<int32_t>(displayedKills_));
	SetNumberText(coinText_, coinPosition_, static_cast<int32_t>(displayedCoins_));
	SetNumberText(totalScoreText_, totalScorePosition_, static_cast<int32_t>(displayedTotalScore_));
}

void ResultScene::SetNumberText(
	BitmapText& text,
	const Vector2& basePosition,
	int32_t value,
	float scaleMultiplier,
	float alpha)
{
	const float scale = (digitSize_.y / 72.0f) * scoreScale_ * scaleMultiplier;
	const float yOffset = digitSize_.y * scoreScale_ * (1.0f - scaleMultiplier) * 0.5f;
	value = std::clamp(value, 0, 999999);
	std::string digits = std::to_string(value);
	constexpr float kValueColumnWidth = 112.0f;
	text.SetText(digits);
	text.SetScaleToFit(scale, kValueColumnWidth);
	text.SetPosition({
		basePosition.x + kValueColumnWidth - text.MeasureWidth(),
		basePosition.y + yOffset,
		});
	text.SetColor({ 1.0f, 1.0f, 1.0f, alpha });
}

bool ResultScene::IsCountUpFinished() const
{
	return countUpFinished_;
}

void ResultScene::FinishCountUp()
{
	if (!sessionContext_) {
		countUpFinished_ = true;
		return;
	}

	const RunResult& resultData = sessionContext_->GetResultData();
	displayedExp_ = static_cast<float>(resultData.totalExp);
	displayedLevel_ = static_cast<float>(resultData.finalLevel);
	displayedKills_ = static_cast<float>(resultData.totalKillCount);
	displayedCoins_ = static_cast<float>(resultData.coins);
	displayedTotalScore_ = static_cast<float>(CalculateTotalScore(resultData.totalExp, resultData.finalLevel, resultData.totalKillCount, resultData.coins));
	SetNumberText(expText_, expPosition_, resultData.totalExp);
	SetNumberText(levelText_, levelPosition_, resultData.finalLevel);
	SetNumberText(killText_, killPosition_, resultData.totalKillCount);
	SetNumberText(coinText_, coinPosition_, resultData.coins);
	SetNumberText(totalScoreText_, totalScorePosition_, static_cast<int32_t>(displayedTotalScore_));
	countUpFinished_ = true;
	finishSePlayed_ = true;
}

void ResultScene::RequestSceneChange(const char* sceneId)
{
	if (!pendingSceneId_.empty()) {
		return;
	}
	pendingSceneId_ = sceneId;
	if (curtain_) {
		curtain_->StartClose(24.0f);
	}
}

void ResultScene::UpdateCurtain(float deltaTime)
{
	if (!curtain_) {
		return;
	}
	curtain_->Update(deltaTime);
	if (!pendingSceneId_.empty() && curtain_->IsFinished()) {
		Engine::Scene::SceneManager::GetInstance()->ChangeScene(pendingSceneId_);
	}
}

void ResultScene::InitializeWorldBackground()
{
	resultCamera_ = std::make_unique<Engine::CameraSystem::Camera>();
	resultCamera_->SetTranslate({ 0.0f, 30.0f, -44.0f });
	resultCamera_->SetRotate({ 0.64f, 0.0f, 0.0f });
	resultCamera_->SetFarClip(500.0f);
	resultCamera_->Update();
	Engine::CameraSystem::CameraManager::GetInstance()->AddCamera(
		kResultCameraName,
		resultCamera_.get());
	Engine::CameraSystem::CameraManager::GetInstance()->SetActiveCamera(kResultCameraName);

	SceneLighting::Defaults defaults{};
	defaults.light.color = { 1.0f, 0.96f, 0.86f, 1.0f };
	defaults.light.direction = { -0.55f, -1.0f, -0.45f };
	defaults.light.intensity = 0.9f;
	defaults.light.ambientColor = { 0.48f, 0.52f, 0.62f, 1.0f };
	defaults.light.ambientIntensity = 0.34f;
	defaults.light.specularStrength = 0.16f;
	defaults.light.enable = 1;
	SceneLighting::ApplyDefaults(defaults);

	gridPlane_ = std::make_unique<GridPlane>();
	gridPlane_->Initialize();
	gridPlane_->Update({ 0.0f, 0.0f, 0.0f });

	skyDome_ = std::make_unique<SkyDome>();
	skyDome_->Initialize();
	skyDome_->Update();
}

void ResultScene::DrawWorldBackground()
{
	Engine::Graphics3D::Object3DCommon* objectCommon =
		Engine::Graphics3D::Object3DCommon::GetInstance();
	Engine::Graphics3D::Object3D::ClearSubmittedDraws();
	Engine::Graphics3D::Object3D::ClearSubmittedShadows();
	if (objectCommon->BeginShadowPass({ 0.0f, 0.0f, 0.0f })) {
		if (gridPlane_) {
			gridPlane_->DrawShadow();
		}
		Engine::Graphics3D::Object3D::FlushSubmittedShadows();
		objectCommon->EndShadowPass();
	}

	objectCommon->CommonDraw();
	if (skyDome_) {
		skyDome_->Draw();
	}
	if (gridPlane_) {
		gridPlane_->Draw();
	}
	Engine::Graphics3D::Object3D::FlushSubmittedDraws();
}

void ResultScene::InitializeResultPanels()
{
	dimPanel_.Initialize();
	resultPanel_.Initialize();
	for (UIPanel& border : resultPanelBorders_) {
		border.Initialize();
	}
	for (UIPanel& row : resultRowPanels_) {
		row.Initialize();
	}
	for (UIPanel& border : resultRowBorders_) {
		border.Initialize();
	}
}

void ResultScene::ApplyResultPanelLayout()
{
	SetPanelLayout(dimPanel_, { 0.0f, 0.0f }, { 1280.0f, 720.0f }, kDimColor);
	SetPanelLayout(resultPanel_, panelPosition_, panelSize_, kPanelColor);
	SetBorderLayout(resultPanelBorders_, 0, panelPosition_, panelSize_, 4.0f, kFrameColor);

	const float rowX = panelPosition_.x + 42.0f;
	const float rowWidth = panelSize_.x - 84.0f;
	const float rowHeight = 58.0f;
	const std::array<float, 5> rowY{
		panelPosition_.y + 132.0f,
		panelPosition_.y + 202.0f,
		panelPosition_.y + 272.0f,
		panelPosition_.y + 342.0f,
		panelPosition_.y + 430.0f,
	};
	const std::array<BitmapText*, 5> labels{
		&expLabelText_,
		&levelLabelText_,
		&killLabelText_,
		&coinLabelText_,
		&totalScoreLabelText_,
	};
	const std::array<Vector2*, 5> valuePositions{
		&expPosition_,
		&levelPosition_,
		&killPosition_,
		&coinPosition_,
		&totalScorePosition_,
	};
	for (size_t index = 0; index < resultRowPanels_.size(); ++index) {
		const Vector2 rowPosition{ rowX, rowY[index] };
		SetPanelLayout(resultRowPanels_[index], rowPosition, { rowWidth, rowHeight }, kRowColor);
		SetBorderLayout(resultRowBorders_, index * 4, rowPosition, { rowWidth, rowHeight }, 2.0f, kFrameColor);
		labels[index]->SetPosition({ rowPosition.x + 24.0f, rowPosition.y + 13.0f });
		*valuePositions[index] = {
			rowPosition.x + rowWidth - 172.0f,
			rowPosition.y + 2.0f,
		};
	}
	titleText_.SetPosition({
		panelPosition_.x + (panelSize_.x - titleText_.MeasureWidth()) * 0.5f,
		panelPosition_.y + 42.0f,
		});
	promptText_.SetPosition({
		panelPosition_.x + (panelSize_.x - promptText_.MeasureWidth()) * 0.5f,
		panelPosition_.y + panelSize_.y - 70.0f,
		});
}

void ResultScene::DrawResultPanels()
{
	dimPanel_.Draw();
	resultPanel_.Draw();
	for (UIPanel& border : resultPanelBorders_) {
		border.Draw();
	}
	for (UIPanel& row : resultRowPanels_) {
		row.Draw();
	}
	for (UIPanel& border : resultRowBorders_) {
		border.Draw();
	}
	titleText_.Draw();
	expLabelText_.Draw();
	levelLabelText_.Draw();
	killLabelText_.Draw();
	coinLabelText_.Draw();
	totalScoreLabelText_.Draw();
}

}
