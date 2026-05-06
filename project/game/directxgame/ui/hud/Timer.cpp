#include "game/directxgame/ui/hud/Timer.h"
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

constexpr char kHudNumberPath[] = "ui/number/numbers.png";
constexpr char kHudColonPath[] = "ui/number/colon.png";

}

namespace DirectXGame {

void Timer::Initialize()
{
	numberTexture_ = GameTextureCache::Load(kHudNumberPath);
	colonTexture_ = GameTextureCache::Load(kHudColonPath);

	const UILayoutIO::LayoutMap layout = UILayoutIO::LoadOrDefault(DataPaths::kHudLayout, {});
	layoutSettings_.position = UILayoutIO::GetVector2(layout, "timerPosition", layoutSettings_.position);
	layoutSettings_.scale = UILayoutIO::GetFloat(layout, "timerScale", layoutSettings_.scale);

	for (int32_t index = 0; index < kDigitCount; ++index) {
		sprite_[index] = GameSpriteFactory::Create(numberTexture_, { 0.0f, 0.0f });
		sprite_[index]->SetSize(digitSize_);
		sprite_[index]->SetTextureSize(digitSize_);
	}

	colonSprite_ = GameSpriteFactory::Create(colonTexture_, { 0.0f, 0.0f });
	colonSprite_->SetSize(digitSize_);

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

	for (int32_t index = 0; index < kDigitCount; ++index) {
		if (index == 2 || !sprite_[index]) {
			continue;
		}
		sprite_[index]->Update();
		sprite_[index]->Draw();
	}

	if (colonSprite_) {
		colonSprite_->Update();
		colonSprite_->Draw();
	}
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

	for (int32_t index = 0; index < kDigitCount; ++index) {
		if (!sprite_[index]) {
			continue;
		}
		DigitSpriteUtil::UpdateDigitLayout(*sprite_[index], basePosition, digitSize_, worldScale, index);
	}

	if (colonSprite_) {
		DigitSpriteUtil::UpdateDigitLayout(*colonSprite_, basePosition, digitSize_, worldScale, 2);
	}
}

void Timer::UpdateBounds()
{
	size_ = { digitSize_.x * static_cast<float>(kDigitCount), digitSize_.y };
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

	DigitSpriteUtil::SetDigitSprite(*sprite_[0], digitSize_.x, digitSize_, minutes / 10);
	DigitSpriteUtil::SetDigitSprite(*sprite_[1], digitSize_.x, digitSize_, minutes % 10);
	DigitSpriteUtil::SetDigitSprite(*sprite_[3], digitSize_.x, digitSize_, seconds / 10);
	DigitSpriteUtil::SetDigitSprite(*sprite_[4], digitSize_.x, digitSize_, seconds % 10);
}

}
