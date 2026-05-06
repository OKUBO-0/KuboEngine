#include "game/directxgame/ui/common/UIElement.h"
#include <algorithm>

namespace DirectXGame {

void UIElement::SetPosition(const Vector2& position)
{
	position_ = position;
	OnTransformChanged();
	NotifyChildrenTransformChanged();
}

void UIElement::SetSize(const Vector2& size)
{
	size_ = size;
	OnTransformChanged();
	NotifyChildrenTransformChanged();
}

void UIElement::SetVisible(bool visible)
{
	visible_ = visible;
}

void UIElement::SetScale(float scale)
{
	scale_ = scale;
	OnTransformChanged();
	NotifyChildrenTransformChanged();
}

void UIElement::SetAlpha(float alpha)
{
	alpha_ = std::clamp(alpha, 0.0f, 1.0f);
	OnVisualChanged();
	NotifyChildrenVisualChanged();
}

void UIElement::SetAnchor(Anchor anchor)
{
	anchor_ = anchor;
	OnTransformChanged();
	NotifyChildrenTransformChanged();
}

void UIElement::SetParent(UIElement* parent)
{
	if (parent_ == parent) {
		return;
	}

	if (parent_) {
		parent_->RemoveChild(this);
	}

	parent_ = parent;
	if (parent_) {
		parent_->AddChild(this);
	}

	OnTransformChanged();
	OnVisualChanged();
	NotifyChildrenTransformChanged();
	NotifyChildrenVisualChanged();
}

void UIElement::AddChild(UIElement* child)
{
	if (!child) {
		return;
	}

	const auto it = std::find(children_.begin(), children_.end(), child);
	if (it == children_.end()) {
		children_.push_back(child);
	}
}

void UIElement::RemoveChild(UIElement* child)
{
	if (!child) {
		return;
	}

	const auto it = std::remove(children_.begin(), children_.end(), child);
	if (it != children_.end()) {
		children_.erase(it, children_.end());
	}
}

Vector2 UIElement::GetWorldPosition() const
{
	Vector2 world = position_;
	if (parent_) {
		const Vector2 parentWorld = parent_->GetWorldPosition();
		const float parentScale = parent_->GetWorldScale();
		world.x = parentWorld.x + (world.x * parentScale);
		world.y = parentWorld.y + (world.y * parentScale);
	}
	return world;
}

float UIElement::GetWorldScale() const
{
	if (parent_) {
		return scale_ * parent_->GetWorldScale();
	}
	return scale_;
}

float UIElement::GetWorldAlpha() const
{
	if (parent_) {
		return alpha_ * parent_->GetWorldAlpha();
	}
	return alpha_;
}

Vector2 UIElement::GetScaledSize() const
{
	const float worldScale = GetWorldScale();
	return { size_.x * worldScale, size_.y * worldScale };
}

Vector2 UIElement::GetAnchorOffset() const
{
	const Vector2 scaledSize = GetScaledSize();
	Vector2 offset{};

	switch (anchor_) {
	case Anchor::TopCenter:
		offset.x = -scaledSize.x * 0.5f;
		break;
	case Anchor::TopRight:
		offset.x = -scaledSize.x;
		break;
	case Anchor::MiddleLeft:
		offset.y = -scaledSize.y * 0.5f;
		break;
	case Anchor::Center:
		offset.x = -scaledSize.x * 0.5f;
		offset.y = -scaledSize.y * 0.5f;
		break;
	case Anchor::MiddleRight:
		offset.x = -scaledSize.x;
		offset.y = -scaledSize.y * 0.5f;
		break;
	case Anchor::BottomLeft:
		offset.y = -scaledSize.y;
		break;
	case Anchor::BottomCenter:
		offset.x = -scaledSize.x * 0.5f;
		offset.y = -scaledSize.y;
		break;
	case Anchor::BottomRight:
		offset.x = -scaledSize.x;
		offset.y = -scaledSize.y;
		break;
	case Anchor::TopLeft:
	default:
		break;
	}

	return offset;
}

Vector2 UIElement::GetLocalAnchorOffset() const
{
	Vector2 offset{};

	switch (anchor_) {
	case Anchor::TopCenter:
		offset.x = -size_.x * 0.5f;
		break;
	case Anchor::TopRight:
		offset.x = -size_.x;
		break;
	case Anchor::MiddleLeft:
		offset.y = -size_.y * 0.5f;
		break;
	case Anchor::Center:
		offset.x = -size_.x * 0.5f;
		offset.y = -size_.y * 0.5f;
		break;
	case Anchor::MiddleRight:
		offset.x = -size_.x;
		offset.y = -size_.y * 0.5f;
		break;
	case Anchor::BottomLeft:
		offset.y = -size_.y;
		break;
	case Anchor::BottomCenter:
		offset.x = -size_.x * 0.5f;
		offset.y = -size_.y;
		break;
	case Anchor::BottomRight:
		offset.x = -size_.x;
		offset.y = -size_.y;
		break;
	case Anchor::TopLeft:
	default:
		break;
	}

	return offset;
}

void UIElement::LayoutChildrenVertically(float spacing)
{
	float currentY = 0.0f;
	for (UIElement* child : children_) {
		if (!child) {
			continue;
		}

		child->SetPosition({ child->GetPosition().x, currentY });
		currentY += child->GetSize().y + spacing;
	}
}

void UIElement::LayoutChildrenHorizontally(float spacing)
{
	float currentX = 0.0f;
	for (UIElement* child : children_) {
		if (!child) {
			continue;
		}

		child->SetPosition({ currentX, child->GetPosition().y });
		currentX += child->GetSize().x + spacing;
	}
}

void UIElement::DrawChildren()
{
	if (!visible_) {
		return;
	}

	for (UIElement* child : children_) {
		if (!child || !child->IsVisible()) {
			continue;
		}
		child->Draw();
	}
}

void UIElement::NotifyChildrenTransformChanged()
{
	for (UIElement* child : children_) {
		if (!child) {
			continue;
		}
		child->OnTransformChanged();
		child->NotifyChildrenTransformChanged();
	}
}

void UIElement::NotifyChildrenVisualChanged()
{
	for (UIElement* child : children_) {
		if (!child) {
			continue;
		}
		child->OnVisualChanged();
		child->NotifyChildrenVisualChanged();
	}
}

}
