#pragma once

#include "Sprite.h"
#include "Vector2.h"
#include <cstdint>

namespace DirectXGame::DigitSpriteUtil {

Vector2 CalculateDigitPosition(const Vector2& basePosition, const Vector2& size, float scale, int32_t index);
void UpdateDigitLayout(Engine::Graphics2D::Sprite& sprite, const Vector2& basePosition, const Vector2& size, float scale, int32_t index);
void SetDigitSprite(Engine::Graphics2D::Sprite& sprite, float digitWidth, const Vector2& size, int32_t number);

template <typename SpriteContainer>
void SetNumberSprites(const SpriteContainer& sprites, float digitWidth, const Vector2& size, int32_t number, int32_t initialDigit)
{
	int32_t digit = initialDigit;
	for (size_t index = 0; index < sprites.size(); ++index) {
		if (!sprites[index]) {
			digit /= 10;
			continue;
		}

		const int32_t currentNumber = digit > 0 ? number / digit : 0;
		SetDigitSprite(*sprites[index], digitWidth, size, currentNumber);
		if (digit > 0) {
			number %= digit;
			digit /= 10;
		}
	}
}

}
