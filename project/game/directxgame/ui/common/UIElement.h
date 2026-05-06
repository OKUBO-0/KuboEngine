#pragma once

#include "Vector2.h"
#include <vector>

namespace DirectXGame {

class UIElement {
public:
	enum class Anchor {
		TopLeft,
		TopCenter,
		TopRight,
		MiddleLeft,
		Center,
		MiddleRight,
		BottomLeft,
		BottomCenter,
		BottomRight,
	};

	virtual ~UIElement() = default;

	void SetPosition(const Vector2& position);
	void SetSize(const Vector2& size);
	void SetVisible(bool visible);
	void SetScale(float scale);
	void SetAlpha(float alpha);
	void SetAnchor(Anchor anchor);
	void SetParent(UIElement* parent);
	void AddChild(UIElement* child);
	void RemoveChild(UIElement* child);
	void LayoutChildrenVertically(float spacing);
	void LayoutChildrenHorizontally(float spacing);

	const Vector2& GetPosition() const { return position_; }
	const Vector2& GetSize() const { return size_; }
	bool IsVisible() const { return visible_; }
	float GetScale() const { return scale_; }
	float GetAlpha() const { return alpha_; }
	Anchor GetAnchor() const { return anchor_; }
	UIElement* GetParent() const { return parent_; }

	Vector2 GetWorldPosition() const;
	float GetWorldScale() const;
	float GetWorldAlpha() const;
	Vector2 GetScaledSize() const;
	Vector2 GetAnchorOffset() const;
	Vector2 GetLocalAnchorOffset() const;
	void DrawChildren();

	virtual void Draw() = 0;

protected:
	virtual void OnTransformChanged() {}
	virtual void OnVisualChanged() {}

	Vector2 position_{ 0.0f, 0.0f };
	Vector2 size_{ 0.0f, 0.0f };
	bool visible_ = true;
	float scale_ = 1.0f;
	float alpha_ = 1.0f;
	Anchor anchor_ = Anchor::TopLeft;
	UIElement* parent_ = nullptr;
	std::vector<UIElement*> children_;

private:
	void NotifyChildrenTransformChanged();
	void NotifyChildrenVisualChanged();
};

}
