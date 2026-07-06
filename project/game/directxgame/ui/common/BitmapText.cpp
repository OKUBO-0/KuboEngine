#include "BitmapText.h"
#include "GameSpriteFactory.h"
#include "ResourcePaths.h"
#include <algorithm>
#include <fstream>
#include <regex>
#include <sstream>
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

void BitmapText::Initialize(
	const std::string& texturePath,
	const std::string& metadataPath)
{
	textureHandle_ = GameTextureCache::Load(texturePath);
	usesMetrics_ = LoadMetadata(metadataPath);
	if (!usesMetrics_) {
		glyphCodepoints_ = BuildAsciiGlyphs();
	}
	RebuildSprites();
}

bool BitmapText::LoadMetadata(const std::string& metadataPath)
{
	glyphMetrics_.clear();
	std::ifstream file(ResourcePaths::MakePath(metadataPath));
	if (!file) {
		return false;
	}
	const std::string json(
		(std::istreambuf_iterator<char>(file)),
		std::istreambuf_iterator<char>());
	std::smatch headerMatch;
	const std::regex ascentPattern(R"("ascent"\s*:\s*(-?\d+))");
	if (!std::regex_search(json, headerMatch, ascentPattern)) {
		return false;
	}
	ascent_ = std::stof(headerMatch[1].str());
	const std::regex glyphPattern(
		R"("codepoint"\s*:\s*(\d+)\s*,\s*"width"\s*:\s*(\d+)\s*,\s*"height"\s*:\s*(\d+)\s*,\s*"bearingX"\s*:\s*(-?\d+)\s*,\s*"bearingY"\s*:\s*(-?\d+)\s*,\s*"advance"\s*:\s*(\d+)\s*,\s*"x"\s*:\s*(\d+)\s*,\s*"y"\s*:\s*(\d+))");
	for (std::sregex_iterator it(json.begin(), json.end(), glyphPattern), end;
		it != end; ++it) {
		const std::smatch& match = *it;
		GlyphMetric metric;
		metric.textureSize = {
			std::stof(match[2].str()), std::stof(match[3].str()) };
		metric.bearing = {
			std::stof(match[4].str()), std::stof(match[5].str()) };
		metric.advance = std::stof(match[6].str());
		metric.texturePosition = {
			std::stof(match[7].str()), std::stof(match[8].str()) };
		metric.visible = std::stoul(match[1].str()) != static_cast<uint32_t>(' ');
		glyphMetrics_[static_cast<uint32_t>(std::stoul(match[1].str()))] = metric;
	}
	return !glyphMetrics_.empty();
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
	renderedGlyphMetrics_.clear();
	const std::vector<uint32_t> textCodepoints = DecodeUtf8(text_);
	glyphSprites_.reserve(textCodepoints.size());
	renderedGlyphIndices_.reserve(textCodepoints.size());
	renderedGlyphMetrics_.reserve(textCodepoints.size());
	if (textureHandle_ == 0) {
		return;
	}
	if (usesMetrics_) {
		const auto fallback = glyphMetrics_.find('?');
		for (uint32_t codepoint : textCodepoints) {
			auto glyph = glyphMetrics_.find(codepoint);
			if (glyph == glyphMetrics_.end()) {
				glyph = fallback;
			}
			if (glyph == glyphMetrics_.end()) {
				continue;
			}
			renderedGlyphMetrics_.push_back(glyph->second);
			glyphSprites_.push_back(glyph->second.visible
				? GameSpriteFactory::Create(textureHandle_, position_)
				: nullptr);
		}
		ApplyLayout();
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
	if (usesMetrics_) {
		float cursorX = position_.x;
		for (size_t index = 0; index < renderedGlyphMetrics_.size(); ++index) {
			const GlyphMetric& glyph = renderedGlyphMetrics_[index];
			std::unique_ptr<Engine::Graphics2D::Sprite>& sprite = glyphSprites_[index];
			if (sprite) {
				sprite->SetPosition({
					cursorX + glyph.bearing.x * scale_,
					position_.y + (ascent_ + glyph.bearing.y) * scale_ });
				sprite->SetSize({
					glyph.textureSize.x * scale_, glyph.textureSize.y * scale_ });
				sprite->SetTextureLeftTop(glyph.texturePosition);
				sprite->SetTextureSize(glyph.textureSize);
				sprite->SetColor(color_);
			}
			cursorX += glyph.advance * scale_ * advanceMultiplier_;
		}
		return;
	}
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
