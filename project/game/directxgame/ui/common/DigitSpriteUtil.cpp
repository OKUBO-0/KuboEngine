#include "game/directxgame/ui/common/DigitSpriteUtil.h"
#include <algorithm>

namespace DirectXGame::DigitSpriteUtil {

Vector2 CalculateDigitPosition(const Vector2& basePosition, const Vector2& size, float scale, int32_t index)
{
	return { basePosition.x + (size.x * scale * static_cast<float>(index)), basePosition.y };
}

void UpdateDigitLayout(
	Engine::Graphics2D::Sprite& sprite, const Vector2& basePosition, const Vector2& size, float scale, int32_t index)
{
	sprite.SetSize({ size.x * scale, size.y * scale });
	sprite.SetPosition(CalculateDigitPosition(basePosition, size, scale, index));
}

void SetDigitSprite(Engine::Graphics2D::Sprite& sprite, float digitWidth, const Vector2& size, int32_t number)
{
	const int32_t clamped = std::clamp(number % 10, 0, 9);
	sprite.SetTextureLeftTop({ digitWidth * static_cast<float>(clamped), 0.0f });
	sprite.SetTextureSize(size);
}

}
