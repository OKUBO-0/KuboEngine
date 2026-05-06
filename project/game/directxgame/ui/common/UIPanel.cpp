#include "game/directxgame/ui/common/UIPanel.h"
#include "Sprite.h"
#include "game/directxgame/core/GameSpriteFactory.h"

namespace DirectXGame {

void UIPanel::Initialize()
{
	sprite_ = GameSpriteFactory::Create("white1x1.png", { 0.0f, 0.0f });
	sprite_->SetAnchorPoint({ 0.0f, 0.0f });
	sprite_->SetColor(color_);
	OnTransformChanged();
	OnVisualChanged();
}

void UIPanel::SetColor(const Vector4& color)
{
	color_ = color;
	OnVisualChanged();
}

void UIPanel::Draw()
{
	if (visible_ && sprite_) {
		sprite_->Update();
		sprite_->Draw();
	}
	DrawChildren();
}

void UIPanel::OnTransformChanged()
{
	if (!sprite_) {
		return;
	}

	const Vector2 world = GetWorldPosition();
	const Vector2 offset = GetAnchorOffset();
	sprite_->SetPosition({ world.x + offset.x, world.y + offset.y });
	sprite_->SetSize(GetScaledSize());
}

void UIPanel::OnVisualChanged()
{
	if (!sprite_) {
		return;
	}

	Vector4 appliedColor = color_;
	appliedColor.w *= GetWorldAlpha();
	sprite_->SetColor(appliedColor);
}

}
