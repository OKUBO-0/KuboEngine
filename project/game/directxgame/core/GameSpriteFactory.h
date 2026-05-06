#pragma once

#include "Vector2.h"
#include "game/directxgame/core/GameTextureCache.h"
#include <memory>
#include <string>

namespace Engine::Graphics2D {
class Sprite;
}

namespace DirectXGame {

class GameSpriteFactory {
public:
	using SpritePtr = std::unique_ptr<Engine::Graphics2D::Sprite>;

	static SpritePtr Create(const std::string& relativePath, const Vector2& position);
	static SpritePtr Create(TextureHandle textureHandle, const Vector2& position);

private:
	static SpritePtr CreateFromFullPath(const std::string& fullPath, const Vector2& position);
};

}
