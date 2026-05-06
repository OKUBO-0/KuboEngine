#include "game/directxgame/ui/gauge/ExpGauge.h"
#include "Sprite.h"
#include "game/directxgame/core/DirectXGameDataPaths.h"
#include "game/directxgame/core/GameSpriteFactory.h"
#include "game/directxgame/core/GameTextureCache.h"
#include "game/directxgame/core/UILayoutIO.h"
#include "game/directxgame/ui/common/DigitSpriteUtil.h"
#include <algorithm>
#ifdef _DEBUG
#include <imgui.h>
#endif

namespace {

constexpr char kLvLabelPath[] = "ui/game/lv_label.png";
constexpr char kLvDigitsPath[] = "ui/number/numbers.png";

int32_t StepDisplayValue(int32_t displayedValue, int32_t targetValue)
{
	if (displayedValue < targetValue) {
		displayedValue += std::max<int32_t>(1, (targetValue - displayedValue) / 10);
	} else if (displayedValue > targetValue) {
		displayedValue -= std::max<int32_t>(1, (displayedValue - targetValue) / 10);
	}

	return displayedValue;
}

float CalculateGaugeRate(int32_t displayedValue, int32_t maxValue)
{
	float ratio = static_cast<float>(displayedValue) / static_cast<float>(maxValue);
	return std::clamp(ratio, 0.0f, 1.0f);
}

}

namespace DirectXGame {

void ExpGauge::Initialize()
{
	lvDigitsHandle_ = GameTextureCache::Load(kLvDigitsPath);

	const UILayoutIO::LayoutMap layout = UILayoutIO::LoadOrDefault(DataPaths::kHudLayout, {});
	layoutSettings_.framePosition = UILayoutIO::GetVector2(layout, "expFramePosition", layoutSettings_.framePosition);
	layoutSettings_.frameSize = UILayoutIO::GetVector2(layout, "expFrameSize", layoutSettings_.frameSize);
	layoutSettings_.gaugePosition = UILayoutIO::GetVector2(layout, "expGaugePosition", layoutSettings_.gaugePosition);
	layoutSettings_.gaugeSize = UILayoutIO::GetVector2(layout, "expGaugeSize", layoutSettings_.gaugeSize);
	layoutSettings_.lvLabelPosition = UILayoutIO::GetVector2(layout, "lvLabelPosition", layoutSettings_.lvLabelPosition);
	layoutSettings_.lvLabelSize = UILayoutIO::GetVector2(layout, "lvLabelSize", layoutSettings_.lvLabelSize);
	layoutSettings_.lvDigitsPosition = UILayoutIO::GetVector2(layout, "lvDigitsPosition", layoutSettings_.lvDigitsPosition);

	frameBar_.Initialize();
	frameBar_.SetColors({ 1.0f, 1.0f, 0.0f, 1.0f }, { 1.0f, 1.0f, 0.0f, 1.0f });
	frameBar_.SetRate(1.0f);

	gaugeBar_.Initialize();
	gaugeBar_.SetColors({ 0.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f, 1.0f });
	gaugeBar_.SetRate(0.0f);

	lvLabel_.Initialize(kLvLabelPath, layoutSettings_.lvLabelPosition);
	lvLabel_.SetSize(layoutSettings_.lvLabelSize);

	for (int32_t index = 0; index < kLvDigits; ++index) {
		sprite_[index] = GameSpriteFactory::Create(lvDigitsHandle_, { 0.0f, 0.0f });
		sprite_[index]->SetSize(digitSize_);
		sprite_[index]->SetTextureSize(digitSize_);
	}

	ApplyLayout();
	SetLevel(1);
}

void ExpGauge::Update()
{
	displayedExp_ = StepDisplayValue(displayedExp_, targetExp_);
	gaugeBar_.SetRate(CalculateGaugeRate(displayedExp_, maxExp_));
}

void ExpGauge::Draw()
{
	frameBar_.Draw();
	gaugeBar_.Draw();
	lvLabel_.Draw();
	for (int32_t index = 0; index < kLvDigits; ++index) {
		if (!sprite_[index]) {
			continue;
		}
		sprite_[index]->Update();
		sprite_[index]->Draw();
	}
}

void ExpGauge::SetEXP(int32_t current, int32_t max)
{
	targetExp_ = current;
	maxExp_ = std::max<int32_t>(1, max);
}

void ExpGauge::SetLevel(int32_t level)
{
	DigitSpriteUtil::SetNumberSprites(sprite_, digitSize_.x, digitSize_, level, 10);
}

bool ExpGauge::IsFilled() const
{
	return displayedExp_ >= maxExp_;
}

void ExpGauge::DebugDrawImGui()
{
#ifdef _DEBUG
	if (!ImGui::CollapsingHeader("HUD EXP", ImGuiTreeNodeFlags_DefaultOpen)) {
		return;
	}

	ImGui::Checkbox("Enable HUD Debug##EXP", &layoutSettings_.debugEnabled);
	if (!layoutSettings_.debugEnabled) {
		return;
	}

	float framePosition[2]{ layoutSettings_.framePosition.x, layoutSettings_.framePosition.y };
	if (ImGui::DragFloat2("EXP Frame Pos", framePosition, 1.0f, -400.0f, 1280.0f)) {
		layoutSettings_.framePosition = { framePosition[0], framePosition[1] };
		ApplyLayout();
	}

	float frameSize[2]{ layoutSettings_.frameSize.x, layoutSettings_.frameSize.y };
	if (ImGui::DragFloat2("EXP Frame Size", frameSize, 1.0f, 16.0f, 1600.0f)) {
		layoutSettings_.frameSize = { frameSize[0], frameSize[1] };
		ApplyLayout();
	}

	float gaugePosition[2]{ layoutSettings_.gaugePosition.x, layoutSettings_.gaugePosition.y };
	if (ImGui::DragFloat2("EXP Gauge Pos", gaugePosition, 1.0f, -400.0f, 1280.0f)) {
		layoutSettings_.gaugePosition = { gaugePosition[0], gaugePosition[1] };
		ApplyLayout();
	}

	float gaugeSize[2]{ layoutSettings_.gaugeSize.x, layoutSettings_.gaugeSize.y };
	if (ImGui::DragFloat2("EXP Gauge Size", gaugeSize, 1.0f, 16.0f, 1600.0f)) {
		layoutSettings_.gaugeSize = { gaugeSize[0], gaugeSize[1] };
		ApplyLayout();
	}

	float labelPosition[2]{ layoutSettings_.lvLabelPosition.x, layoutSettings_.lvLabelPosition.y };
	if (ImGui::DragFloat2("LV Label Pos", labelPosition, 1.0f, -400.0f, 1280.0f)) {
		layoutSettings_.lvLabelPosition = { labelPosition[0], labelPosition[1] };
		ApplyLayout();
	}

	float digitsPosition[2]{ layoutSettings_.lvDigitsPosition.x, layoutSettings_.lvDigitsPosition.y };
	if (ImGui::DragFloat2("LV Digits Pos", digitsPosition, 1.0f, -400.0f, 1280.0f)) {
		layoutSettings_.lvDigitsPosition = { digitsPosition[0], digitsPosition[1] };
		ApplyLayout();
	}

	if (ImGui::Button("Save EXP Layout")) {
		SaveLayout();
	}
#endif
}

void ExpGauge::SaveLayout() const
{
	UILayoutIO::Save(DataPaths::kHudLayout,
		{
			{ "expFramePosition", { layoutSettings_.framePosition.x, layoutSettings_.framePosition.y } },
			{ "expFrameSize", { layoutSettings_.frameSize.x, layoutSettings_.frameSize.y } },
			{ "expGaugePosition", { layoutSettings_.gaugePosition.x, layoutSettings_.gaugePosition.y } },
			{ "expGaugeSize", { layoutSettings_.gaugeSize.x, layoutSettings_.gaugeSize.y } },
			{ "lvLabelPosition", { layoutSettings_.lvLabelPosition.x, layoutSettings_.lvLabelPosition.y } },
			{ "lvLabelSize", { layoutSettings_.lvLabelSize.x, layoutSettings_.lvLabelSize.y } },
			{ "lvDigitsPosition", { layoutSettings_.lvDigitsPosition.x, layoutSettings_.lvDigitsPosition.y } },
		});
}

void ExpGauge::ApplyLayout()
{
	frameBar_.SetPosition(layoutSettings_.framePosition);
	frameBar_.SetSize(layoutSettings_.frameSize);
	gaugeBar_.SetPosition(layoutSettings_.gaugePosition);
	gaugeBar_.SetSize(layoutSettings_.gaugeSize);
	lvLabel_.SetPosition(layoutSettings_.lvLabelPosition);
	lvLabel_.SetSize(layoutSettings_.lvLabelSize);

	for (int32_t index = 0; index < kLvDigits; ++index) {
		if (!sprite_[index]) {
			continue;
		}
		sprite_[index]->SetPosition(
			{ layoutSettings_.lvDigitsPosition.x + digitSize_.x * static_cast<float>(index), layoutSettings_.lvDigitsPosition.y });
		sprite_[index]->SetSize(digitSize_);
	}
}

}
