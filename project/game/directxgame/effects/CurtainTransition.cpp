#include "game/directxgame/effects/CurtainTransition.h"
#include "Vector2.h"
#include "Vector4.h"
#include "game/directxgame/core/ScreenUtil.h"
#include "game/directxgame/ui/common/UILabel.h"
#include <algorithm>

namespace DirectXGame {

namespace {

constexpr float kFrameDeltaBaseline = 0.016f;
constexpr float kMaxTransitionDelta = 0.033f;
constexpr float kCurtainOverlap = 24.0f;

float GetCurtainHalfHeight()
{
	return ScreenUtil::GetClientSize().y * 0.5f;
}

float GetTopOpenY()
{
	return -(GetCurtainHalfHeight() + kCurtainOverlap);
}

float GetBottomClosedY()
{
	return GetCurtainHalfHeight() - kCurtainOverlap;
}

float GetBottomOpenY()
{
	return ScreenUtil::GetClientSize().y;
}

Vector2 GetCurtainSize()
{
	const Vector2 clientSize = ScreenUtil::GetClientSize();
	return { clientSize.x, GetCurtainHalfHeight() + kCurtainOverlap };
}

}

void CurtainTransition::Initialize()
{
	topCurtain_ = std::make_unique<UILabel>();
	bottomCurtain_ = std::make_unique<UILabel>();

	topCurtain_->Initialize("white1x1.png", { 0.0f, GetTopOpenY() });
	bottomCurtain_->Initialize("white1x1.png", { 0.0f, GetBottomOpenY() });
	topCurtain_->SetColor({ 0.0f, 0.0f, 0.0f, 1.0f });
	bottomCurtain_->SetColor({ 0.0f, 0.0f, 0.0f, 1.0f });
	topCurtain_->SetSize(GetCurtainSize());
	bottomCurtain_->SetSize(GetCurtainSize());
	state_ = State::None;
}

void CurtainTransition::StartClose(float speed)
{
	speed_ = speed;
	state_ = State::Close;
	topCurtain_->SetPosition({ 0.0f, GetTopOpenY() });
	bottomCurtain_->SetPosition({ 0.0f, GetBottomOpenY() });
}

void CurtainTransition::StartOpen(float speed)
{
	speed_ = speed;
	state_ = State::Open;
	topCurtain_->SetPosition({ 0.0f, 0.0f });
	bottomCurtain_->SetPosition({ 0.0f, GetBottomClosedY() });
}

void CurtainTransition::Update(float deltaTime)
{
	const float clampedDeltaTime = (std::min)(deltaTime, kMaxTransitionDelta);
	const float deltaScale = clampedDeltaTime / kFrameDeltaBaseline;

	if (state_ == State::Close) {
		Vector2 top = topCurtain_->GetPosition();
		Vector2 bottom = bottomCurtain_->GetPosition();
		top.y += speed_ * deltaScale;
		bottom.y -= speed_ * deltaScale;
		topCurtain_->SetPosition(top);
		bottomCurtain_->SetPosition(bottom);

		if (top.y >= 0.0f) {
			topCurtain_->SetPosition({ 0.0f, 0.0f });
			bottomCurtain_->SetPosition({ 0.0f, GetBottomClosedY() });
			state_ = State::Finished;
		}
	} else if (state_ == State::Open) {
		Vector2 top = topCurtain_->GetPosition();
		Vector2 bottom = bottomCurtain_->GetPosition();
		top.y -= speed_ * deltaScale;
		bottom.y += speed_ * deltaScale;
		topCurtain_->SetPosition(top);
		bottomCurtain_->SetPosition(bottom);

		if (top.y <= GetTopOpenY()) {
			topCurtain_->SetPosition({ 0.0f, GetTopOpenY() });
			bottomCurtain_->SetPosition({ 0.0f, GetBottomOpenY() });
			state_ = State::None;
		}
	}
}

void CurtainTransition::Draw()
{
	if (state_ == State::None) {
		return;
	}

	topCurtain_->Draw();
	bottomCurtain_->Draw();
}

}
