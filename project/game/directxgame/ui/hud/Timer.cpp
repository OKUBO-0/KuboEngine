#include "Timer.h"
#include "DataPaths.h"
#include "UILayoutIO.h"
#include <algorithm>
#include <iomanip>
#include <sstream>
#ifdef _DEBUG
#include <imgui.h>
#endif

namespace DirectXGame {

void Timer::Initialize()
{
	const UILayoutIO::LayoutMap layout = UILayoutIO::LoadOrDefault(DataPaths::kHudLayout, {});
	layoutSettings_.position = UILayoutIO::GetVector2(layout, "timerPosition", layoutSettings_.position);
	layoutSettings_.scale = UILayoutIO::GetFloat(layout, "timerScale", layoutSettings_.scale);

	timeText_.Initialize(
		"ui/font/noto_sans_jp_black.png",
		"ui/font/noto_sans_jp_black.json");
	timeText_.SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });

	UpdateBounds();
	ApplyLayout();
	UpdateDisplay();
}

void Timer::Update(float deltaTime)
{
	time_ += deltaTime;
	UpdateDisplay();
}

void Timer::Draw()
{
	if (!visible_) {
		return;
	}

	timeText_.Draw();
}

void Timer::SetPosition(const Vector2& position)
{
	UIElement::SetPosition(position);
}

void Timer::SetScale(float scale)
{
	UIElement::SetScale(scale);
}

void Timer::SetTime(float time)
{
	time_ = (std::max)(0.0f, time);
	UpdateDisplay();
}

void Timer::Reset()
{
	time_ = 0.0f;
	UpdateDisplay();
}

void Timer::DebugDrawImGui()
{
#ifdef _DEBUG
	if (!ImGui::CollapsingHeader("HUD Timer", ImGuiTreeNodeFlags_DefaultOpen)) {
		return;
	}

	ImGui::Checkbox("Enable HUD Debug##Timer", &layoutSettings_.debugEnabled);
	if (!layoutSettings_.debugEnabled) {
		return;
	}

	float position[2]{ layoutSettings_.position.x, layoutSettings_.position.y };
	if (ImGui::DragFloat2("Timer Position", position, 1.0f, -400.0f, 1280.0f)) {
		layoutSettings_.position = { position[0], position[1] };
		ApplyLayout();
	}

	if (ImGui::DragFloat("Timer Scale", &layoutSettings_.scale, 0.05f, 0.5f, 6.0f)) {
		ApplyLayout();
	}

	if (ImGui::Button("Save Timer Layout")) {
		SaveLayout();
	}
#endif
}

void Timer::SaveLayout() const
{
	UILayoutIO::Save(DataPaths::kHudLayout,
		{
			{ "timerPosition", { layoutSettings_.position.x, layoutSettings_.position.y } },
			{ "timerScale", { layoutSettings_.scale } },
		});
}

void Timer::OnTransformChanged()
{
	RefreshLayout();
}

void Timer::RefreshLayout()
{
	const Vector2 world = GetWorldPosition();
	const Vector2 offset = GetAnchorOffset();
	const Vector2 basePosition = { world.x + offset.x, world.y + offset.y };
	const float worldScale = GetWorldScale();

	timeText_.SetPosition(basePosition);
	timeText_.SetScale((digitSize_.y / 72.0f) * worldScale);
	timeText_.SetAdvanceMultiplier(1.0f);
}

void Timer::UpdateBounds()
{
	size_ = { digitSize_.x * 5.0f, digitSize_.y };
}

void Timer::ApplyLayout()
{
	UpdateBounds();
	UIElement::SetPosition(layoutSettings_.position);
	UIElement::SetScale(layoutSettings_.scale);
}

void Timer::UpdateDisplay()
{
	const int totalSeconds = static_cast<int>(time_);
	const int minutes = totalSeconds / 60;
	const int seconds = totalSeconds % 60;

	std::ostringstream stream;
	stream << std::setfill('0') << std::setw(2) << (minutes % 100)
		<< ':' << std::setw(2) << seconds;
	timeText_.SetText(stream.str());
}

}
