#pragma once

#include "GameTextureCache.h"
#include "Sprite.h"
#include "Vector2.h"
#include "Vector4.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace DirectXGame {

class BitmapText final {
public:
	void Initialize(
		const std::string& texturePath,
		const Vector2& glyphSize,
		int32_t columns);
	void Initialize(
		const std::string& texturePath,
		const Vector2& glyphSize,
		int32_t columns,
		const std::string& glyphCharacters);
	void SetText(const std::string& text);
	void SetPosition(const Vector2& position);
	void SetScale(float scale);
	void SetAdvanceMultiplier(float multiplier);
	void SetColor(const Vector4& color);
	void Draw();

private:
	void RebuildSprites();
	void ApplyLayout();

	TextureHandle textureHandle_ = 0;
	Vector2 glyphSize_{ 18.0f, 26.0f };
	int32_t columns_ = 16;
	std::string text_;
	std::vector<uint32_t> glyphCodepoints_;
	std::vector<int32_t> renderedGlyphIndices_;
	Vector2 position_{};
	float scale_ = 1.0f;
	float advanceMultiplier_ = 1.0f;
	Vector4 color_{ 1.0f, 1.0f, 1.0f, 1.0f };
	std::vector<std::unique_ptr<Engine::Graphics2D::Sprite>> glyphSprites_;
};

}
