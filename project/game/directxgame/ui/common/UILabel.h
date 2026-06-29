#pragma once

#include "Sprite.h"
#include "Vector4.h"
#include "GameTextureCache.h"
#include "UIElement.h"
#include <memory>
#include <string>

namespace DirectXGame {

class UILabel : public UIElement {
public:
	void Initialize(const std::string& relativePath, const Vector2& position);
	void Initialize(TextureHandle textureHandle, const Vector2& position);
	void SetTexture(const std::string& relativePath);
	void SetTexture(TextureHandle textureHandle);
	void SetColor(const Vector4& color);
	void Draw() override;

private:
	void OnTransformChanged() override;
	void OnVisualChanged() override;

	Vector4 color_{ 1.0f, 1.0f, 1.0f, 1.0f };
	std::unique_ptr<Engine::Graphics2D::Sprite> sprite_;
};

}
