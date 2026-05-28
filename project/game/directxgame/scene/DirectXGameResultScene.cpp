#include "game/directxgame/scene/DirectXGameResultScene.h"
#include "game/directxgame/core/DirectXGameDataPaths.h"
#include "game/directxgame/core/GameMenuController.h"
#include "game/directxgame/core/DirectXGameSceneId.h"
#include "game/directxgame/core/DirectXGameSessionContext.h"
#include "game/directxgame/core/GameSpriteFactory.h"
#include "game/directxgame/core/ScreenUtil.h"
#include "game/directxgame/core/UILayoutIO.h"
#include "game/directxgame/ui/common/DigitSpriteUtil.h"
#include "CameraManager.h"
#include "Input.h"
#include "Object3DCommon.h"
#include "OffscreenRenderManager.h"
#include "SceneManager.h"
#include "SpriteCommon.h"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <string_view>
#ifdef _DEBUG
#include "DebugEditorManager.h"
#include "IconsFontAwesome5.h"
#include <imgui.h>
#endif

namespace {

constexpr char kAudioResultFinish[] = "result.finish";
constexpr int32_t kScorePerExp = 1;
constexpr int32_t kScorePerKill = 100;
constexpr int32_t kScorePerLevel = 1000;

int32_t CalculateTotalScore(int32_t exp, int32_t level, int32_t kill)
{
	return exp * kScorePerExp + kill * kScorePerKill + level * kScorePerLevel;
}

}

namespace DirectXGame {

DirectXGameResultScene::DirectXGameResultScene(std::shared_ptr<DirectXGameSessionContext> sessionContext)
	: sessionContext_(std::move(sessionContext))
{
}

void DirectXGameResultScene::Initialize()
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

void DirectXGameResultScene::Finalize()
{
	if (finishSeHandle_ != 0) {
		GameAudioCache::Stop(finishSeHandle_);
	}
}

void DirectXGameResultScene::Update()
{
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

#ifdef _DEBUG
	Engine::InputSystem::Input* input = Engine::InputSystem::Input::GetInstance();
	if (input && input->TriggerKey(DIK_F5)) {
		ReloadDebugData();
	}
	const DebugWindowVisibility previousDebugWindows = debugWindows_;
	const auto saveWindowVisibilityIfChanged = [this](const DebugWindowVisibility& previous) {
		if (previous.windowSwitcher != debugWindows_.windowSwitcher ||
			previous.sceneView != debugWindows_.sceneView ||
			previous.statisticsView != debugWindows_.statisticsView ||
			previous.sceneSettings != debugWindows_.sceneSettings ||
			previous.audio != debugWindows_.audio ||
			previous.keyInputDebug != debugWindows_.keyInputDebug) {
			SaveLayout();
		}
	};

	const Engine::Editor::DebugEditorMenuItem windowItems[] = {
		{ "Scene", &debugWindows_.sceneView },
		{ "統計", &debugWindows_.statisticsView },
		{ "シーン設定", &debugWindows_.sceneSettings },
		{ "オーディオ", &debugWindows_.audio },
		{ "キー操作デバッグ", &debugWindows_.keyInputDebug },
	};
	const Engine::Editor::DebugEditorMenuItem editItems[] = {
		{ "シーン設定", &debugWindows_.sceneSettings },
	};
	const Engine::Editor::DebugEditorMenuItem objectItems[] = {
		{ "Scene", &debugWindows_.sceneView },
		{ "シーン設定", &debugWindows_.sceneSettings },
	};
	Engine::Editor::DebugEditorManager::DrawMainMenu({
		windowItems,
		std::size(windowItems),
		editItems,
		std::size(editItems),
		objectItems,
		std::size(objectItems),
		"リザルトレイアウトを保存",
		[this]() { SaveLayout(); },
		"リザルト設定を再読み込み",
		[this]() { ReloadDebugData(); },
		"Result Debug UI",
		[this]() { RequestSceneChange(SceneId::kTitle); },
		{},
		{},
		&debugWindows_.windowSwitcher,
	});

	if (debugWindows_.windowSwitcher) {
		Engine::Editor::DebugEditorManager::DrawWindowSwitcher(
			"ウィンドウ表示切り替え",
			&debugWindows_.windowSwitcher,
			windowItems,
			std::size(windowItems),
			{ 260.0f, 180.0f },
			[this]() {
				Engine::Editor::DebugEditorManager::DrawHotReloadButton();
			});
	}

	if (debugWindows_.sceneView) {
		const Engine::Editor::DebugSceneViewportState sceneViewport =
			Engine::Editor::DebugEditorManager::DrawSceneViewport(&debugWindows_.sceneView);
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
		float masterVolume = GameAudioCache::GetMasterVolume();
		if (ImGui::SliderFloat("Master Volume", &masterVolume, 0.0f, 1.0f)) {
			GameAudioCache::SetMasterVolume(masterVolume);
		}
		float resultFinishVolume = GameAudioCache::GetTunedVolume(kAudioResultFinish, 1.0f);
		if (ImGui::SliderFloat("Result Finish Volume", &resultFinishVolume, 0.0f, 1.0f)) {
			GameAudioCache::SetTunedVolume(kAudioResultFinish, resultFinishVolume);
		}
		ImGui::End();
	}

	if (debugWindows_.sceneSettings) {
		ImGui::Begin("シーン設定", &debugWindows_.sceneSettings);
		ImGui::Checkbox("Enable Result Layout Debug", &layoutDebugEnabled_);
		if (layoutDebugEnabled_) {
		float backgroundPosition[2]{ backgroundPosition_.x, backgroundPosition_.y };
		if (ImGui::DragFloat2("Background Position", backgroundPosition, 1.0f, -400.0f, 1280.0f)) {
			backgroundPosition_ = { backgroundPosition[0], backgroundPosition[1] };
			ApplyLayout();
		}
		float backgroundSize[2]{ backgroundSize_.x, backgroundSize_.y };
		if (ImGui::DragFloat2("Background Size", backgroundSize, 1.0f, 64.0f, 1600.0f)) {
			backgroundSize_ = { backgroundSize[0], backgroundSize[1] };
			ApplyLayout();
		}
		float resultPosition[2]{ resultPosition_.x, resultPosition_.y };
		if (ImGui::DragFloat2("Result UI Position", resultPosition, 1.0f, -400.0f, 1280.0f)) {
			resultPosition_ = { resultPosition[0], resultPosition[1] };
			ApplyLayout();
		}
		float resultSize[2]{ resultSize_.x, resultSize_.y };
		if (ImGui::DragFloat2("Result UI Size", resultSize, 1.0f, 64.0f, 1600.0f)) {
			resultSize_ = { resultSize[0], resultSize[1] };
			ApplyLayout();
		}
		float finishPosition[2]{ finishPosition_.x, finishPosition_.y };
		if (ImGui::DragFloat2("Finish UI Position", finishPosition, 1.0f, -400.0f, 1280.0f)) {
			finishPosition_ = { finishPosition[0], finishPosition[1] };
			ApplyLayout();
		}
		float finishSize[2]{ finishSize_.x, finishSize_.y };
		if (ImGui::DragFloat2("Finish UI Size", finishSize, 1.0f, 64.0f, 1600.0f)) {
			finishSize_ = { finishSize[0], finishSize[1] };
			ApplyLayout();
		}
		float expPosition[2]{ expPosition_.x, expPosition_.y };
		if (ImGui::DragFloat2("EXP Position", expPosition, 1.0f, -400.0f, 1280.0f)) {
			expPosition_ = { expPosition[0], expPosition[1] };
			ApplyLayout();
		}
		float levelPosition[2]{ levelPosition_.x, levelPosition_.y };
		if (ImGui::DragFloat2("Level Position", levelPosition, 1.0f, -400.0f, 1280.0f)) {
			levelPosition_ = { levelPosition[0], levelPosition[1] };
			ApplyLayout();
		}
		float killPosition[2]{ killPosition_.x, killPosition_.y };
		if (ImGui::DragFloat2("Kill Position", killPosition, 1.0f, -400.0f, 1280.0f)) {
			killPosition_ = { killPosition[0], killPosition[1] };
			ApplyLayout();
		}
		float totalScorePosition[2]{ totalScorePosition_.x, totalScorePosition_.y };
		if (ImGui::DragFloat2("Total Score Position", totalScorePosition, 1.0f, -400.0f, 1280.0f)) {
			totalScorePosition_ = { totalScorePosition[0], totalScorePosition[1] };
			ApplyLayout();
		}
		float digitSize[2]{ digitSize_.x, digitSize_.y };
		if (ImGui::DragFloat2("Digit Size", digitSize, 1.0f, 4.0f, 128.0f)) {
			digitSize_ = { digitSize[0], digitSize[1] };
			ApplyLayout();
		}
		if (ImGui::DragFloat("Score Scale", &scoreScale_, 0.05f, 0.25f, 6.0f)) {
			ApplyLayout();
		}
		if (ImGui::Button("Save Result Layout")) {
			SaveLayout();
		}
	}
		if (ImGui::Button("Back To Title")) {
			RequestSceneChange(SceneId::kTitle);
			ImGui::End();
			return;
		}
		ImGui::End();
	}

	if (debugWindows_.statisticsView) {
		ImGui::Begin("統計", &debugWindows_.statisticsView);
		ImGui::Text("Stage 12 result scene");
		ImGui::Text("Count Up Finished: %s", countUpFinished_ ? "true" : "false");
		ImGui::Text("Displayed EXP: %.0f", displayedExp_);
		ImGui::Text("Displayed Level: %.0f", displayedLevel_);
		ImGui::Text("Displayed Kills: %.0f", displayedKills_);
		ImGui::Text("Displayed Total Score: %.0f", displayedTotalScore_);
		if (sessionContext_) {
		const DirectXGameResultData& resultData = sessionContext_->GetResultData();
		ImGui::Separator();
		ImGui::Text("Run Count: %u", sessionContext_->GetRunCount());
		ImGui::Text("Elapsed Frames: %u", resultData.elapsedFrames);
		ImGui::Text("Level: %u", resultData.level);
		ImGui::Text("Kill Count: %u", resultData.killCount);
	}
		ImGui::End();
	}

	if (debugWindows_.keyInputDebug) {
		ImGui::Begin("キー操作デバッグ", &debugWindows_.keyInputDebug);
		ImGui::TextUnformatted("リザルトシーン入力");
		ImGui::Text("Input Device: %s", GameInputBindings::ToDisplayName(navigationInputDevice_));
		ImGui::Text("Confirm: %s", GameInputBindings::GetConfirmLabel(navigationInputDevice_));
		ImGui::Text("Cancel: %s", GameInputBindings::GetCancelLabel(navigationInputDevice_));
		ImGui::Text("Pending Scene: %s", pendingSceneId_.empty() ? "none" : pendingSceneId_.c_str());
		ImGui::End();
	}
	Engine::Editor::DebugEditorManager::SaveWindowItems(windowItems, std::size(windowItems));
	saveWindowVisibilityIfChanged(previousDebugWindows);
#endif
}

void DirectXGameResultScene::Draw()
{
	Engine::Graphics3D::Object3DCommon::GetInstance()->CommonDraw();
	Engine::Graphics2D::SpriteCommon::GetInstance()->CommonDraw();
	background_.Draw();
	resultUi_.Draw();
	DrawNumber(expDigits_);
	DrawNumber(levelDigits_);
	DrawNumber(killDigits_);
	DrawNumber(totalScoreDigits_);
	if (IsCountUpFinished()) {
		finishUi_.Draw();
	}
	if (curtain_) {
		curtain_->Draw();
	}
}

void DirectXGameResultScene::InitializeUi()
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

	background_.Initialize("ui/result/Result.png", backgroundPosition_);
	resultUi_.Initialize("ui/result/Result.png", resultPosition_);
	finishUi_.Initialize("ui/result/finish_ui.png", finishPosition_);
	finishUi_.SetAnchor(UIElement::Anchor::Center);
	ApplyLayout();
	curtain_ = std::make_unique<CurtainTransition>();
	curtain_->Initialize();
	curtain_->StartOpen(20.0f);

	numberTexture_ = GameTextureCache::Load("ui/number/numbers.png");
	finishSeHandle_ = GameAudioCache::LoadWave("audio/se/se_pause.wav");
	for (size_t index = 0; index < expDigits_.size(); ++index) {
		expDigits_[index] = GameSpriteFactory::Create(numberTexture_, { 0.0f, 0.0f });
		levelDigits_[index] = GameSpriteFactory::Create(numberTexture_, { 0.0f, 0.0f });
		killDigits_[index] = GameSpriteFactory::Create(numberTexture_, { 0.0f, 0.0f });
		totalScoreDigits_[index] = GameSpriteFactory::Create(numberTexture_, { 0.0f, 0.0f });
	}

	FinishCountUp();
	displayedExp_ = 0.0f;
	displayedLevel_ = 0.0f;
	displayedKills_ = 0.0f;
	displayedTotalScore_ = 0.0f;
	countUpFinished_ = false;
	finishSePlayed_ = false;
	SetNumberSprites(expDigits_, expPosition_, 0);
	SetNumberSprites(levelDigits_, levelPosition_, 0);
	SetNumberSprites(killDigits_, killPosition_, 0);
	SetNumberSprites(totalScoreDigits_, totalScorePosition_, 0);
}

void DirectXGameResultScene::ApplyLayout()
{
	background_.SetPosition(backgroundPosition_);
	background_.SetSize(backgroundSize_);
	resultUi_.SetPosition(resultPosition_);
	resultUi_.SetSize(resultSize_);
	finishUi_.SetPosition(finishPosition_);
	finishUi_.SetSize(finishSize_);
	SetNumberSprites(expDigits_, expPosition_, static_cast<int32_t>(displayedExp_));
	SetNumberSprites(levelDigits_, levelPosition_, static_cast<int32_t>(displayedLevel_));
	SetNumberSprites(killDigits_, killPosition_, static_cast<int32_t>(displayedKills_));
	SetNumberSprites(totalScoreDigits_, totalScorePosition_, static_cast<int32_t>(displayedTotalScore_));
}

void DirectXGameResultScene::SaveLayout() const
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

void DirectXGameResultScene::ReloadDebugData()
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

void DirectXGameResultScene::UpdateCountUp(float deltaTime)
{
	if (!sessionContext_ || countUpFinished_) {
		return;
	}

	const DirectXGameResultData& resultData = sessionContext_->GetResultData();
	const float stepScale = deltaTime * 2.4f;
	displayedExp_ = (std::min)(static_cast<float>(resultData.totalExp), displayedExp_ + (std::max)(1.0f, static_cast<float>(resultData.totalExp) * stepScale));
	displayedLevel_ = (std::min)(static_cast<float>(resultData.finalLevel), displayedLevel_ + (std::max)(1.0f, static_cast<float>(resultData.finalLevel) * stepScale));
	displayedKills_ = (std::min)(static_cast<float>(resultData.totalKillCount), displayedKills_ + (std::max)(1.0f, static_cast<float>(resultData.totalKillCount) * stepScale));
	const int32_t totalScore = CalculateTotalScore(resultData.totalExp, resultData.finalLevel, resultData.totalKillCount);
	displayedTotalScore_ = (std::min)(static_cast<float>(totalScore), displayedTotalScore_ + (std::max)(1.0f, static_cast<float>(totalScore) * stepScale));

	const float countPulse = 1.0f + (0.5f + 0.5f * std::sin(resultAnimationTime_ * 12.0f)) * 0.055f;
	SetNumberSprites(expDigits_, expPosition_, static_cast<int32_t>(displayedExp_), countPulse, 0.92f);
	SetNumberSprites(levelDigits_, levelPosition_, static_cast<int32_t>(displayedLevel_), countPulse, 0.92f);
	SetNumberSprites(killDigits_, killPosition_, static_cast<int32_t>(displayedKills_), countPulse, 0.92f);
	SetNumberSprites(totalScoreDigits_, totalScorePosition_, static_cast<int32_t>(displayedTotalScore_), countPulse, 1.0f);

	countUpFinished_ =
		static_cast<int32_t>(displayedExp_) >= resultData.totalExp &&
		static_cast<int32_t>(displayedLevel_) >= resultData.finalLevel &&
		static_cast<int32_t>(displayedKills_) >= resultData.totalKillCount &&
		static_cast<int32_t>(displayedTotalScore_) >= totalScore;
	if (countUpFinished_ && !finishSePlayed_ && finishSeHandle_ != 0) {
		GameAudioCache::Play(finishSeHandle_);
		GameAudioCache::SetVolumeFromTuning(finishSeHandle_, kAudioResultFinish, 1.0f);
		finishSePlayed_ = true;
	}
}

void DirectXGameResultScene::UpdateFinishUiPulse()
{
	if (!countUpFinished_) {
		finishUi_.SetAlpha(0.0f);
		return;
	}

	const float pulse = 0.5f + 0.5f * std::sin(resultAnimationTime_ * 4.8f);
	finishUi_.SetAlpha(0.62f + pulse * 0.38f);
	finishUi_.SetScale(1.0f + pulse * 0.035f);
	SetNumberSprites(expDigits_, expPosition_, static_cast<int32_t>(displayedExp_));
	SetNumberSprites(levelDigits_, levelPosition_, static_cast<int32_t>(displayedLevel_));
	SetNumberSprites(killDigits_, killPosition_, static_cast<int32_t>(displayedKills_));
	SetNumberSprites(totalScoreDigits_, totalScorePosition_, static_cast<int32_t>(displayedTotalScore_));
}

void DirectXGameResultScene::DrawNumber(const std::array<std::unique_ptr<Engine::Graphics2D::Sprite>, 6>& sprites)
{
	for (const std::unique_ptr<Engine::Graphics2D::Sprite>& sprite : sprites) {
		if (!sprite) {
			continue;
		}
		sprite->Update();
		sprite->Draw();
	}
}

void DirectXGameResultScene::SetNumberSprites(
	std::array<std::unique_ptr<Engine::Graphics2D::Sprite>, 6>& sprites,
	const Vector2& basePosition,
	int32_t value,
	float scaleMultiplier,
	float alpha)
{
	const Vector2 scaledSize{ digitSize_.x * scoreScale_ * scaleMultiplier, digitSize_.y * scoreScale_ * scaleMultiplier };
	const float yOffset = (digitSize_.y * scoreScale_ - scaledSize.y) * 0.5f;
	value = std::clamp(value, 0, 999999);
	for (size_t index = 0; index < sprites.size(); ++index) {
		if (!sprites[index]) {
			continue;
		}
		const int32_t divisor = static_cast<int32_t>(std::pow(10, static_cast<int32_t>(sprites.size() - index - 1)));
		const int32_t digit = divisor > 0 ? (value / divisor) % 10 : 0;
		sprites[index]->SetPosition({
			basePosition.x + scaledSize.x * static_cast<float>(index),
			basePosition.y + yOffset,
		});
		sprites[index]->SetSize(scaledSize);
		sprites[index]->SetColor({ 1.0f, 1.0f, 1.0f, alpha });
		DigitSpriteUtil::SetDigitSprite(*sprites[index], digitSize_.x, digitSize_, digit);
	}
}

bool DirectXGameResultScene::IsCountUpFinished() const
{
	return countUpFinished_;
}

void DirectXGameResultScene::FinishCountUp()
{
	if (!sessionContext_) {
		countUpFinished_ = true;
		return;
	}

	const DirectXGameResultData& resultData = sessionContext_->GetResultData();
	displayedExp_ = static_cast<float>(resultData.totalExp);
	displayedLevel_ = static_cast<float>(resultData.finalLevel);
	displayedKills_ = static_cast<float>(resultData.totalKillCount);
	displayedTotalScore_ = static_cast<float>(CalculateTotalScore(resultData.totalExp, resultData.finalLevel, resultData.totalKillCount));
	SetNumberSprites(expDigits_, expPosition_, resultData.totalExp);
	SetNumberSprites(levelDigits_, levelPosition_, resultData.finalLevel);
	SetNumberSprites(killDigits_, killPosition_, resultData.totalKillCount);
	SetNumberSprites(totalScoreDigits_, totalScorePosition_, static_cast<int32_t>(displayedTotalScore_));
	countUpFinished_ = true;
	finishSePlayed_ = true;
}

void DirectXGameResultScene::RequestSceneChange(const char* sceneId)
{
	if (!pendingSceneId_.empty()) {
		return;
	}
	pendingSceneId_ = sceneId;
	if (curtain_) {
		curtain_->StartClose(24.0f);
	}
}

void DirectXGameResultScene::UpdateCurtain(float deltaTime)
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
