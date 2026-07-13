#include "ResultScene.h"
#include "DirectXCommon.h"
#include "DataPaths.h"
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
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <string_view>

namespace {

constexpr char kAudioResultFinish[] = "result.finish";
constexpr char kResultBackgroundTexture[] = "ui/result/Result.png";
constexpr char kResultFinishTexture[] = "ui/result/finish_ui.png";
constexpr char kResultNumberTexture[] = "ui/font/noto_sans_jp_black.png";
constexpr char kResultNumberMetadata[] = "ui/font/noto_sans_jp_black.json";
constexpr int32_t kScorePerExp = 1;
constexpr int32_t kScorePerKill = 100;
constexpr int32_t kScorePerLevel = 1000;

int32_t CalculateTotalScore(int32_t exp, int32_t level, int32_t kill)
{
	return exp * kScorePerExp + kill * kScorePerKill + level * kScorePerLevel;
}

}

namespace DirectXGame {

ResultScene::ResultScene(std::shared_ptr<GameSession> sessionContext)
	: sessionContext_(std::move(sessionContext))
{
}

void ResultScene::Initialize()
{
	Engine::CameraSystem::CameraManager::GetInstance()->Initialize();
	if (Engine::Base::OffscreenRenderManager* offscreen = Engine::Base::OffscreenRenderManager::GetInstance()) {
		offscreen->SetScenePostEffectType(PostEffectType::Fullscreen);
	}

	if (sessionContext_) {
		sessionContext_->OnEnterResultScene();
	}

	InitializeUi();
}

void ResultScene::Finalize()
{
	if (finishSeHandle_) {
		GameAudioCache::Stop(finishSeHandle_);
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
	Engine::Graphics3D::Object3DCommon::GetInstance()->CommonDraw();
	Engine::Graphics2D::SpriteCommon::GetInstance()->CommonDraw();
	background_.Draw();
	resultUi_.Draw();
	expText_.Draw();
	levelText_.Draw();
	killText_.Draw();
	totalScoreText_.Draw();
	if (IsCountUpFinished()) {
		finishUi_.Draw();
	}
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
	totalScorePosition_ = UILayoutIO::GetVector2(layout, "totalScorePosition", totalScorePosition_);
	digitSize_ = UILayoutIO::GetVector2(layout, "digitSize", digitSize_);
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
		kResultBackgroundTexture,
		kResultFinishTexture,
		kResultNumberTexture,
		});
	background_.Initialize(kResultBackgroundTexture, backgroundPosition_);
	resultUi_.Initialize(kResultBackgroundTexture, resultPosition_);
	finishUi_.Initialize(kResultFinishTexture, finishPosition_);
	finishUi_.SetAnchor(UIElement::Anchor::Center);
	ApplyLayout();
	curtain_ = std::make_unique<CurtainTransition>();
	curtain_->Initialize();
	curtain_->StartOpen(20.0f);

	finishSeHandle_ = GameAudioCache::LoadWave("audio/se/se_pause.wav");
	for (BitmapText* text : { &expText_, &levelText_, &killText_, &totalScoreText_ }) {
		text->Initialize(kResultNumberTexture, kResultNumberMetadata);
	}

	FinishCountUp();
	displayedExp_ = 0.0f;
	displayedLevel_ = 0.0f;
	displayedKills_ = 0.0f;
	displayedTotalScore_ = 0.0f;
	countUpFinished_ = false;
	finishSePlayed_ = false;
	SetNumberText(expText_, expPosition_, 0);
	SetNumberText(levelText_, levelPosition_, 0);
	SetNumberText(killText_, killPosition_, 0);
	SetNumberText(totalScoreText_, totalScorePosition_, 0);
}

void ResultScene::ApplyLayout()
{
	background_.SetPosition(backgroundPosition_);
	background_.SetSize(backgroundSize_);
	resultUi_.SetPosition(resultPosition_);
	resultUi_.SetSize(resultSize_);
	finishUi_.SetPosition(finishPosition_);
	finishUi_.SetSize(finishSize_);
	SetNumberText(expText_, expPosition_, static_cast<int32_t>(displayedExp_));
	SetNumberText(levelText_, levelPosition_, static_cast<int32_t>(displayedLevel_));
	SetNumberText(killText_, killPosition_, static_cast<int32_t>(displayedKills_));
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
			{ "totalScorePosition", { totalScorePosition_.x, totalScorePosition_.y } },
			{ "digitSize", { digitSize_.x, digitSize_.y } },
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
	totalScorePosition_ = UILayoutIO::GetVector2(layout, "totalScorePosition", totalScorePosition_);
	digitSize_ = UILayoutIO::GetVector2(layout, "digitSize", digitSize_);
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
	const int32_t totalScore = CalculateTotalScore(resultData.totalExp, resultData.finalLevel, resultData.totalKillCount);
	displayedTotalScore_ = (std::min)(static_cast<float>(totalScore), displayedTotalScore_ + (std::max)(1.0f, static_cast<float>(totalScore) * stepScale));

	const float countPulse = 1.0f + (0.5f + 0.5f * std::sin(resultAnimationTime_ * 12.0f)) * 0.055f;
	SetNumberText(expText_, expPosition_, static_cast<int32_t>(displayedExp_), countPulse, 0.92f);
	SetNumberText(levelText_, levelPosition_, static_cast<int32_t>(displayedLevel_), countPulse, 0.92f);
	SetNumberText(killText_, killPosition_, static_cast<int32_t>(displayedKills_), countPulse, 0.92f);
	SetNumberText(totalScoreText_, totalScorePosition_, static_cast<int32_t>(displayedTotalScore_), countPulse, 1.0f);

	countUpFinished_ =
		static_cast<int32_t>(displayedExp_) >= resultData.totalExp &&
		static_cast<int32_t>(displayedLevel_) >= resultData.finalLevel &&
		static_cast<int32_t>(displayedKills_) >= resultData.totalKillCount &&
		static_cast<int32_t>(displayedTotalScore_) >= totalScore;
	if (countUpFinished_ && !finishSePlayed_ && finishSeHandle_) {
		GameAudioCache::Play(finishSeHandle_);
		GameAudioCache::SetVolumeFromTuning(finishSeHandle_, kAudioResultFinish, 1.0f);
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
	digits.insert(digits.begin(), 6 - digits.size(), '0');
	text.SetText(digits);
	text.SetPosition({ basePosition.x, basePosition.y + yOffset });
	text.SetScale(scale);
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
	displayedTotalScore_ = static_cast<float>(CalculateTotalScore(resultData.totalExp, resultData.finalLevel, resultData.totalKillCount));
	SetNumberText(expText_, expPosition_, resultData.totalExp);
	SetNumberText(levelText_, levelPosition_, resultData.finalLevel);
	SetNumberText(killText_, killPosition_, resultData.totalKillCount);
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

}
