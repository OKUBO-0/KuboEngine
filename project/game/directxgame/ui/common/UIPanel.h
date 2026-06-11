#pragma once

#include "Sprite.h"
#include "Vector4.h"
#include "game/directxgame/ui/common/UIElement.h"
#include <memory>

namespace DirectXGame {

class UIPanel : public UIElement {
public:
	void Initialize();
	void SetColor(const Vector4& color);
	void SetRotation(float rotation);
	void SetSkewX(float skewX);
	void Draw() override;

private:
	void OnTransformChanged() override;
	void OnVisualChanged() override;

	Vector4 color_{ 1.0f, 1.0f, 1.0f, 1.0f };
	float rotation_ = 0.0f;
	float skewX_ = 0.0f;
	std::unique_ptr<Engine::Graphics2D::Sprite> sprite_;
};

}
