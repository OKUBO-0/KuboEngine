#pragma once

#include "Sprite.h"
#include "Vector2.h"
#include "Vector3.h"
#include "game/directxgame/core/GameTextureCache.h"
#include <memory>
#include <vector>

namespace DirectXGame {

class EnemyManager;
class Player;

class MiniMap {
public:
	void Initialize();
	void Update(const Player* player, const EnemyManager& enemyManager);
	void Draw();
	void DebugDrawImGui();
	void SaveLayout() const;

private:
	struct LayoutSettings {
		Vector2 backgroundPosition{ 0.0f, 0.0f };
		Vector2 backgroundSize{ 640.0f, 720.0f };
		Vector2 center{ 320.0f, 360.0f };
		float radius = 180.0f;
		float scale = 3.0f;
		float playerIconSize = 20.0f;
		float enemyIconSize = 20.0f;
		float orbIconSize = 16.0f;
		bool visible = true;
		bool debugEnabled = false;
	};

	void ApplyLayout();
	void DrawIcon(Engine::Graphics2D::Sprite& sprite, const Vector2& position, float size);
	void EnsureIconSpriteCount(std::vector<std::unique_ptr<Engine::Graphics2D::Sprite>>& sprites, TextureHandle textureHandle, size_t count);
	static Vector2 ClampToCircle(const Vector2& center, const Vector2& position, float radius);

	TextureHandle playerTexture_ = 0;
	TextureHandle enemyTexture_ = 0;
	TextureHandle orbTexture_ = 0;
	TextureHandle backgroundTexture_ = 0;
	std::unique_ptr<Engine::Graphics2D::Sprite> backgroundSprite_;
	std::unique_ptr<Engine::Graphics2D::Sprite> playerIconSprite_;
	std::vector<std::unique_ptr<Engine::Graphics2D::Sprite>> enemyIconSprites_;
	std::vector<std::unique_ptr<Engine::Graphics2D::Sprite>> orbIconSprites_;
	std::vector<Vector2> enemyIconPositions_;
	std::vector<Vector2> orbIconPositions_;
	LayoutSettings layoutSettings_{};
};

}
