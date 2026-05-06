#include "game/directxgame/core/GameSpriteFactory.h"
#include "Sprite.h"
#include "SpriteCommon.h"

namespace DirectXGame {

GameSpriteFactory::SpritePtr GameSpriteFactory::Create(const std::string& relativePath, const Vector2& position)
{
	return Create(DirectXGame::GameTextureCache::Load(relativePath), position);
}

GameSpriteFactory::SpritePtr GameSpriteFactory::Create(TextureHandle textureHandle, const Vector2& position)
{
	return CreateFromFullPath(DirectXGame::GameTextureCache::GetPath(textureHandle), position);
}

GameSpriteFactory::SpritePtr GameSpriteFactory::CreateFromFullPath(
	const std::string& fullPath, const Vector2& position)
{
	auto sprite = std::make_unique<Engine::Graphics2D::Sprite>();
	sprite->Initialize(Engine::Graphics2D::SpriteCommon::GetInstance(), fullPath);
	sprite->SetPosition(position);
	return sprite;
}

}
