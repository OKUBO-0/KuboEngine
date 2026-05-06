#include "game/directxgame/ui/common/UILabel.h"
#include "Sprite.h"
#include "game/directxgame/core/GameSpriteFactory.h"

namespace DirectXGame {

void UILabel::Initialize(const std::string& relativePath, const Vector2& position)
{
	sprite_ = GameSpriteFactory::Create(relativePath, position);
	size_ = sprite_->GetSize();
	SetPosition(position);
	OnTransformChanged();
	OnVisualChanged();
}

void UILabel::Initialize(TextureHandle textureHandle, const Vector2& position)
{
	sprite_ = GameSpriteFactory::Create(textureHandle, position);
	size_ = sprite_->GetSize();
	SetPosition(position);
	OnTransformChanged();
	OnVisualChanged();
}

void UILabel::SetColor(const Vector4& color)
{
	color_ = color;
	OnVisualChanged();
}

void UILabel::Draw()
{
	if (visible_ && sprite_) {
		sprite_->Update();
		sprite_->Draw();
	}
	DrawChildren();
}

void UILabel::OnTransformChanged()
{
	if (!sprite_) {
		return;
	}

	const Vector2 world = GetWorldPosition();
	const Vector2 offset = GetAnchorOffset();
	sprite_->SetPosition({ world.x + offset.x, world.y + offset.y });
	sprite_->SetSize(GetScaledSize());
}

void UILabel::OnVisualChanged()
{
	if (!sprite_) {
		return;
	}

	Vector4 appliedColor = color_;
	appliedColor.w *= GetWorldAlpha();
	sprite_->SetColor(appliedColor);
}

}
