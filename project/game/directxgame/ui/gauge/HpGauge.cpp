#include "game/directxgame/ui/gauge/HpGauge.h"
#include "game/directxgame/core/DirectXGameDataPaths.h"
#include "game/directxgame/core/UILayoutIO.h"
#include <algorithm>
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
	float ratio = static_cast<float>(displayedValue) / static_cast<float>(maxValue);
	return std::clamp(ratio, 0.0f, 1.0f);
}

}

namespace DirectXGame {

void HpGauge::Initialize()
{
	const UILayoutIO::LayoutMap layout = UILayoutIO::LoadOrDefault(DataPaths::kHudLayout, {});
	layoutSettings_.position = UILayoutIO::GetVector2(layout, "hpPosition", layoutSettings_.position);
	layoutSettings_.size = UILayoutIO::GetVector2(layout, "hpSize", layoutSettings_.size);

	gauge_.Initialize();
	gauge_.SetColors({ 0.0f, 0.0f, 0.0f, 0.85f }, { 1.0f, 0.0f, 0.0f, 0.95f });
	gauge_.SetRate(0.0f);
	ApplyLayout();
}

void HpGauge::Update()
{
	displayedHP_ = StepDisplayValue(displayedHP_, targetHP_);
	gauge_.SetRate(CalculateGaugeRate(displayedHP_, maxHP_));
}

void HpGauge::Draw()
{
	gauge_.Draw();
}

void HpGauge::SetHP(int32_t current, int32_t max)
{
	targetHP_ = current;
	maxHP_ = std::max<int32_t>(1, max);
}

bool HpGauge::IsDepleted() const
{
	return displayedHP_ <= 0;
}

void HpGauge::DebugDrawImGui()
{
#ifdef _DEBUG
	if (!ImGui::CollapsingHeader("HUD HP", ImGuiTreeNodeFlags_DefaultOpen)) {
		return;
	}

	ImGui::Checkbox("Enable HUD Debug##HP", &layoutSettings_.debugEnabled);
	if (!layoutSettings_.debugEnabled) {
		return;
	}

	float position[2]{ layoutSettings_.position.x, layoutSettings_.position.y };
	if (ImGui::DragFloat2("HP Position", position, 1.0f, -400.0f, 1280.0f)) {
		layoutSettings_.position = { position[0], position[1] };
		ApplyLayout();
	}

	float size[2]{ layoutSettings_.size.x, layoutSettings_.size.y };
	if (ImGui::DragFloat2("HP Size", size, 1.0f, 4.0f, 512.0f)) {
		layoutSettings_.size = { size[0], size[1] };
		ApplyLayout();
	}

	if (ImGui::Button("Save HP Layout")) {
		SaveLayout();
	}
#endif
}

void HpGauge::SaveLayout() const
{
	UILayoutIO::Save(DataPaths::kHudLayout,
		{
			{ "hpPosition", { layoutSettings_.position.x, layoutSettings_.position.y } },
			{ "hpSize", { layoutSettings_.size.x, layoutSettings_.size.y } },
		});
}

void HpGauge::ApplyLayout()
{
	gauge_.SetPosition(layoutSettings_.position);
	gauge_.SetSize(layoutSettings_.size);
}

}
