#include "BitmapText.h"
#include "GameSpriteFactory.h"
#include <algorithm>
#include <string_view>

namespace {

std::vector<uint32_t> DecodeUtf8(std::string_view text)
{
	std::vector<uint32_t> codepoints;
	for (size_t index = 0; index < text.size();) {
		const uint8_t lead = static_cast<uint8_t>(text[index]);
		uint32_t codepoint = lead;
		size_t length = 1;
		if ((lead & 0xE0u) == 0xC0u) {
			codepoint = lead & 0x1Fu;
			length = 2;
		} else if ((lead & 0xF0u) == 0xE0u) {
			codepoint = lead & 0x0Fu;
			length = 3;
		} else if ((lead & 0xF8u) == 0xF0u) {
			codepoint = lead & 0x07u;
			length = 4;
		}
		if (index + length > text.size()) {
			codepoints.push_back('?');
			break;
		}
		for (size_t continuation = 1; continuation < length; ++continuation) {
			const uint8_t byte = static_cast<uint8_t>(text[index + continuation]);
			if ((byte & 0xC0u) != 0x80u) {
				codepoint = '?';
				length = continuation;
				break;
			}
			codepoint = (codepoint << 6u) | (byte & 0x3Fu);
		}
		codepoints.push_back(codepoint);
		index += length;
	}
	return codepoints;
}

std::vector<uint32_t> BuildAsciiGlyphs()
{
	std::vector<uint32_t> glyphs;
	glyphs.reserve(95);
	for (uint32_t codepoint = 32; codepoint <= 126; ++codepoint) {
		glyphs.push_back(codepoint);
	}
	return glyphs;
}

} // namespace

namespace DirectXGame {

void BitmapText::Initialize(
	const std::string& texturePath,
	const Vector2& glyphSize,
	int32_t columns)
{
	Initialize(texturePath, glyphSize, columns, {});
}

void BitmapText::Initialize(
	const std::string& texturePath,
	const Vector2& glyphSize,
	int32_t columns,
	const std::string& glyphCharacters)
{
	textureHandle_ = GameTextureCache::Load(texturePath);
	glyphSize_ = glyphSize;
	columns_ = (std::max)(1, columns);
	glyphCodepoints_ = glyphCharacters.empty()
		? BuildAsciiGlyphs()
		: DecodeUtf8(glyphCharacters);
	RebuildSprites();
}

void BitmapText::SetText(const std::string& text)
{
	if (text_ == text) {
		return;
	}
	text_ = text;
	RebuildSprites();
}

void BitmapText::SetPosition(const Vector2& position)
{
	position_ = position;
	ApplyLayout();
}

void BitmapText::SetScale(float scale)
{
	scale_ = (std::max)(0.1f, scale);
	ApplyLayout();
}

void BitmapText::SetAdvanceMultiplier(float multiplier)
{
	advanceMultiplier_ = (std::max)(0.1f, multiplier);
	ApplyLayout();
}

void BitmapText::SetColor(const Vector4& color)
{
	color_ = color;
	ApplyLayout();
}

void BitmapText::Draw()
{
	for (std::unique_ptr<Engine::Graphics2D::Sprite>& sprite : glyphSprites_) {
		if (!sprite) {
			continue;
		}
		sprite->Update();
		sprite->Draw();
	}
}

void BitmapText::RebuildSprites()
{
	glyphSprites_.clear();
	renderedGlyphIndices_.clear();
	const std::vector<uint32_t> textCodepoints = DecodeUtf8(text_);
	glyphSprites_.reserve(textCodepoints.size());
	renderedGlyphIndices_.reserve(textCodepoints.size());
	if (textureHandle_ == 0) {
		return;
	}
	const auto fallback = std::find(glyphCodepoints_.begin(), glyphCodepoints_.end(), '?');
	const int32_t fallbackIndex = fallback == glyphCodepoints_.end()
		? 0
		: static_cast<int32_t>(std::distance(glyphCodepoints_.begin(), fallback));
	for (uint32_t codepoint : textCodepoints) {
		if (codepoint == ' ') {
			glyphSprites_.push_back(nullptr);
			renderedGlyphIndices_.push_back(-1);
			continue;
		}
		const auto glyph = std::find(glyphCodepoints_.begin(), glyphCodepoints_.end(), codepoint);
		renderedGlyphIndices_.push_back(glyph == glyphCodepoints_.end()
			? fallbackIndex
			: static_cast<int32_t>(std::distance(glyphCodepoints_.begin(), glyph)));
		glyphSprites_.push_back(GameSpriteFactory::Create(textureHandle_, position_));
	}
	ApplyLayout();
}

void BitmapText::ApplyLayout()
{
	const Vector2 scaledGlyphSize{
		glyphSize_.x * scale_,
		glyphSize_.y * scale_,
	};
	float cursorX = position_.x;
	for (size_t index = 0; index < renderedGlyphIndices_.size(); ++index) {
		const int32_t glyphIndex = renderedGlyphIndices_[index];
		std::unique_ptr<Engine::Graphics2D::Sprite>& sprite = glyphSprites_[index];
		if (glyphIndex < 0) {
			cursorX += scaledGlyphSize.x * 0.6f;
			continue;
		}
		const int32_t column = glyphIndex % columns_;
		const int32_t row = glyphIndex / columns_;
		sprite->SetPosition({ cursorX, position_.y });
		sprite->SetSize(scaledGlyphSize);
		sprite->SetTextureLeftTop({
			glyphSize_.x * static_cast<float>(column),
			glyphSize_.y * static_cast<float>(row),
			});
		sprite->SetTextureSize(glyphSize_);
		sprite->SetColor(color_);
		cursorX += scaledGlyphSize.x *
			(glyphIndex >= 95 ? 0.95f : 0.72f) * advanceMultiplier_;
	}
}

}
