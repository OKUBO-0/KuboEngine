#include "ExpGauge.h"
#include "DataPaths.h"
#include "UILayoutIO.h"
#include <algorithm>
#include <cmath>
#ifdef _DEBUG
#include <imgui.h>
#endif

namespace {

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
	if (maxValue <= 0) {
		return 0.0f;
	}

	const float ratio =
		static_cast<float>(displayedValue) / static_cast<float>(maxValue);
	return std::clamp(ratio, 0.0f, 1.0f);
}

}

namespace DirectXGame {

void ExpGauge::Initialize()
{
	const UILayoutIO::LayoutMap layout = UILayoutIO::LoadOrDefault(DataPaths::kHudLayout, {});
	layoutSettings_.framePosition = UILayoutIO::GetVector2(layout, "expFramePosition", layoutSettings_.framePosition);
	layoutSettings_.frameSize = UILayoutIO::GetVector2(layout, "expFrameSize", layoutSettings_.frameSize);
	layoutSettings_.gaugePosition = UILayoutIO::GetVector2(layout, "expGaugePosition", layoutSettings_.gaugePosition);
	layoutSettings_.gaugeSize = UILayoutIO::GetVector2(layout, "expGaugeSize", layoutSettings_.gaugeSize);
	layoutSettings_.lvLabelPosition = UILayoutIO::GetVector2(layout, "lvLabelPosition", layoutSettings_.lvLabelPosition);
	layoutSettings_.lvLabelSize = UILayoutIO::GetVector2(layout, "lvLabelSize", layoutSettings_.lvLabelSize);
	layoutSettings_.lvDigitsPosition = UILayoutIO::GetVector2(layout, "lvDigitsPosition", layoutSettings_.lvDigitsPosition);
	layoutSettings_.lvDigitSize = UILayoutIO::GetVector2(layout, "lvDigitSize", layoutSettings_.lvDigitSize);
	layoutSettings_.lvScale = UILayoutIO::GetFloat(layout, "lvScale", layoutSettings_.lvScale);

	glowBar_.Initialize();
	glowBar_.SetColors({ 1.0f, 0.9f, 0.05f, 1.0f }, { 1.0f, 0.9f, 0.05f, 1.0f });
	glowBar_.SetRate(1.0f);
	glowBar_.SetAlpha(0.0f);

	frameBar_.Initialize();
	frameBar_.SetColors({ 1.0f, 1.0f, 0.0f, 1.0f }, { 1.0f, 1.0f, 0.0f, 1.0f });
	frameBar_.SetRate(1.0f);

	gaugeBar_.Initialize();
	gaugeBar_.SetColors({ 0.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f, 1.0f });
	gaugeBar_.SetRate(0.0f);

	for (UIPanel& sweep : lightSweeps_) {
		sweep.Initialize();
		sweep.SetColor({ 0.75f, 0.92f, 1.0f, 1.0f });
		sweep.SetSkewX(0.28f);
		sweep.SetAlpha(0.0f);
	}

	lvLabelText_.Initialize(
		"ui/font/noto_sans_jp_black.png",
		"ui/font/noto_sans_jp_black.json");
	lvLabelText_.SetText("LV");
	lvDigitsText_.Initialize(
		"ui/font/noto_sans_jp_black.png",
		"ui/font/noto_sans_jp_black.json");

	ApplyLayout();
	SetLevel(1);
}

void ExpGauge::Update()
{
	displayedExp_ = StepDisplayValue(displayedExp_, targetExp_);
	gaugeBar_.SetRate(CalculateGaugeRate(displayedExp_, maxExp_));
	feedbackPulseTimer_ = (std::max)(0.0f, feedbackPulseTimer_ - 1.0f / 60.0f);
	const float feedbackPulse = std::clamp(feedbackPulseTimer_ / 0.24f, 0.0f, 1.0f);
	selectionPulseTime_ += 1.0f / 60.0f;
	const float selectionPulse = levelUpSelectionActive_
		? 0.5f + 0.5f * std::sin(selectionPulseTime_ * 7.0f)
		: 1.0f;
	glowBar_.SetAlpha(0.0f);
	glowBar_.SetScale(1.0f);
	frameBar_.SetAlpha(1.0f);
	gaugeBar_.SetAlpha(1.0f);
	lvLabelText_.SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
	lvDigitsText_.SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
	const float levelUpBrightness = levelUpSelectionActive_ ? selectionPulse * 0.75f : 0.0f;
	frameBar_.SetColors(
		{ 1.0f + levelUpBrightness, 1.0f + levelUpBrightness, levelUpBrightness * 0.35f, 1.0f },
		{ 1.0f + levelUpBrightness, 1.0f + levelUpBrightness, levelUpBrightness * 0.35f, 1.0f });
	gaugeBar_.SetColors(
		{ 0.0f, 0.0f, 0.0f, 1.0f },
		{
			feedbackPulse * 0.25f + levelUpBrightness * 0.18f,
			feedbackPulse * 0.45f + levelUpBrightness * 0.38f,
			1.0f + levelUpBrightness,
			1.0f,
		});
	if (levelUpSelectionActive_) {
		const float filledWidth = layoutSettings_.gaugeSize.x * CalculateGaugeRate(displayedExp_, maxExp_);
		const float sweepWidth = (std::min)(layoutSettings_.gaugeSize.x * 0.055f, filledWidth);
		for (size_t index = 0; index < lightSweeps_.size(); ++index) {
			const float phaseOffset =
				static_cast<float>(index) / static_cast<float>(lightSweeps_.size());
			const float sweepProgress = std::fmod(selectionPulseTime_ * 0.72f + phaseOffset, 1.0f);
			const float sweepEdgeFade = std::sin(sweepProgress * 3.14159265f);
			UIPanel& sweep = lightSweeps_[index];
			sweep.SetPosition({
				layoutSettings_.gaugePosition.x - sweepWidth +
					(filledWidth + sweepWidth) * sweepProgress,
				layoutSettings_.gaugePosition.y,
				});
			sweep.SetSize({ sweepWidth, layoutSettings_.gaugeSize.y });
			sweep.SetAlpha(filledWidth > 1.0f ? sweepEdgeFade * 0.62f : 0.0f);
		}
	} else {
		for (UIPanel& sweep : lightSweeps_) {
			sweep.SetAlpha(0.0f);
		}
	}
}

void ExpGauge::Draw()
{
	glowBar_.Draw();
	frameBar_.Draw();
	gaugeBar_.Draw();
	for (UIPanel& sweep : lightSweeps_) {
		sweep.Draw();
	}
	lvLabelText_.Draw();
	lvDigitsText_.Draw();
}

void ExpGauge::SetEXP(int32_t current, int32_t max)
{
	if (current > targetExp_) {
		feedbackPulseTimer_ = 0.24f;
	}
	targetExp_ = current;
	maxExp_ = std::max<int32_t>(1, max);
}

void ExpGauge::SetLevel(int32_t level)
{
	if (level > level_) {
		feedbackPulseTimer_ = 0.34f;
		displayedExp_ = targetExp_;
	}
	level_ = level;
	lvDigitsText_.SetText(level < 10
		? "0" + std::to_string((std::max)(0, level))
		: std::to_string((std::min)(99, level)));
}

void ExpGauge::SetLevelUpSelectionActive(bool active)
{
	if (active && !levelUpSelectionActive_) {
		selectionPulseTime_ = 0.0f;
	}
	levelUpSelectionActive_ = active;
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

	if (ImGui::SliderFloat("LV Scale", &layoutSettings_.lvScale, 0.25f, 3.0f)) {
		ApplyLayout();
	}

	float labelSize[2]{ layoutSettings_.lvLabelSize.x, layoutSettings_.lvLabelSize.y };
	if (ImGui::DragFloat2("LV Label Size", labelSize, 1.0f, 8.0f, 256.0f)) {
		layoutSettings_.lvLabelSize = { labelSize[0], labelSize[1] };
		ApplyLayout();
	}

	float digitsPosition[2]{ layoutSettings_.lvDigitsPosition.x, layoutSettings_.lvDigitsPosition.y };
	if (ImGui::DragFloat2("LV Digits Pos", digitsPosition, 1.0f, -400.0f, 1280.0f)) {
		layoutSettings_.lvDigitsPosition = { digitsPosition[0], digitsPosition[1] };
		ApplyLayout();
	}

	float digitSize[2]{ layoutSettings_.lvDigitSize.x, layoutSettings_.lvDigitSize.y };
	if (ImGui::DragFloat2("LV Digit Size", digitSize, 1.0f, 8.0f, 128.0f)) {
		layoutSettings_.lvDigitSize = { digitSize[0], digitSize[1] };
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
			{ "lvDigitSize", { layoutSettings_.lvDigitSize.x, layoutSettings_.lvDigitSize.y } },
			{ "lvScale", { layoutSettings_.lvScale } },
		});
}

void ExpGauge::ApplyLayout()
{
	glowBar_.SetPosition(layoutSettings_.framePosition);
	glowBar_.SetSize(layoutSettings_.frameSize);
	frameBar_.SetPosition(layoutSettings_.framePosition);
	frameBar_.SetSize(layoutSettings_.frameSize);
	gaugeBar_.SetPosition(layoutSettings_.gaugePosition);
	gaugeBar_.SetSize(layoutSettings_.gaugeSize);
	const Vector2 scaledLabelSize{
		layoutSettings_.lvLabelSize.x * layoutSettings_.lvScale,
		layoutSettings_.lvLabelSize.y * layoutSettings_.lvScale,
	};
	const Vector2 scaledDigitSize{
		layoutSettings_.lvDigitSize.x * layoutSettings_.lvScale,
		layoutSettings_.lvDigitSize.y * layoutSettings_.lvScale,
	};
	lvLabelText_.SetPosition(layoutSettings_.lvLabelPosition);
	lvLabelText_.SetScale(scaledLabelSize.y / 72.0f);
	lvDigitsText_.SetPosition(layoutSettings_.lvDigitsPosition);
	lvDigitsText_.SetScale(scaledDigitSize.y / 72.0f);
}

}
