#include "game/directxgame/ui/common/UIBar.h"
#include <algorithm>

namespace DirectXGame {

void UIBar::Initialize()
{
	background_.Initialize();
	fill_.Initialize();
	background_.SetParent(this);
	fill_.SetParent(this);
	RefreshLayout();
}

void UIBar::SetColors(const Vector4& backgroundColor, const Vector4& fillColor)
{
	background_.SetColor(backgroundColor);
	fill_.SetColor(fillColor);
}

void UIBar::SetRate(float rate)
{
	rate_ = std::clamp(rate, 0.0f, 1.0f);
	RefreshLayout();
}

void UIBar::Draw()
{
	if (!visible_) {
		return;
	}
	DrawChildren();
}

void UIBar::OnTransformChanged()
{
	RefreshLayout();
}

void UIBar::RefreshLayout()
{
	const Vector2 offset = GetLocalAnchorOffset();

	background_.SetPosition(offset);
	background_.SetSize(size_);

	fill_.SetPosition(offset);
	fill_.SetSize({ size_.x * rate_, size_.y });
}

}
