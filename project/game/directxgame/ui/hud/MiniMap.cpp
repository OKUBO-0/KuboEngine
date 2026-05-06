#include "game/directxgame/ui/hud/MiniMap.h"
#include "game/directxgame/core/DirectXGameDataPaths.h"
#include "game/directxgame/core/GameSpriteFactory.h"
#include "game/directxgame/core/UILayoutIO.h"
#include "game/directxgame/enemy/Enemy.h"
#include "game/directxgame/enemy/EnemyManager.h"
#include "game/directxgame/enemy/ExpOrb.h"
#include "game/directxgame/player/Player.h"
#include <algorithm>
#include <cmath>
#ifdef _DEBUG
#include <imgui.h>
#endif

namespace {

constexpr char kPlayerPath[] = "ui/game/minimap_player.png";
constexpr char kEnemyPath[] = "ui/game/minimap_enemy.png";
constexpr char kOrbPath[] = "ui/game/minimap_orb.png";
constexpr char kBackgroundPath[] = "ui/game/minimap_bg.png";

}

namespace DirectXGame {

void MiniMap::Initialize()
{
	const UILayoutIO::LayoutMap layout = UILayoutIO::LoadOrDefault(DataPaths::kHudLayout, {});
	layoutSettings_.backgroundPosition = UILayoutIO::GetVector2(layout, "miniMapBackgroundPosition", layoutSettings_.backgroundPosition);
	layoutSettings_.backgroundSize = UILayoutIO::GetVector2(layout, "miniMapBackgroundSize", layoutSettings_.backgroundSize);
	layoutSettings_.center = UILayoutIO::GetVector2(layout, "miniMapCenter", layoutSettings_.center);
	layoutSettings_.radius = UILayoutIO::GetFloat(layout, "miniMapRadius", layoutSettings_.radius);
	layoutSettings_.scale = UILayoutIO::GetFloat(layout, "miniMapScale", layoutSettings_.scale);
	layoutSettings_.playerIconSize = UILayoutIO::GetFloat(layout, "miniMapPlayerIconSize", layoutSettings_.playerIconSize);
	layoutSettings_.enemyIconSize = UILayoutIO::GetFloat(layout, "miniMapEnemyIconSize", layoutSettings_.enemyIconSize);
	layoutSettings_.orbIconSize = UILayoutIO::GetFloat(layout, "miniMapOrbIconSize", layoutSettings_.orbIconSize);
	layoutSettings_.visible = UILayoutIO::GetFloat(layout, "miniMapVisible", layoutSettings_.visible ? 1.0f : 0.0f) > 0.5f;

	playerTexture_ = GameTextureCache::Load(kPlayerPath);
	enemyTexture_ = GameTextureCache::Load(kEnemyPath);
	orbTexture_ = GameTextureCache::Load(kOrbPath);
	backgroundTexture_ = GameTextureCache::Load(kBackgroundPath);
	backgroundSprite_ = GameSpriteFactory::Create(backgroundTexture_, layoutSettings_.backgroundPosition);
	playerIconSprite_ = GameSpriteFactory::Create(playerTexture_, layoutSettings_.center);
	playerIconSprite_->SetAnchorPoint({ 0.5f, 0.5f });

	ApplyLayout();
}

void MiniMap::Update(const Player* player, const EnemyManager& enemyManager)
{
	enemyIconPositions_.clear();
	orbIconPositions_.clear();
	if (!player) {
		return;
	}

	const Vector3 playerPosition = player->GetWorldPosition();
	auto addIcon = [this, &playerPosition](const Vector3& objectPosition, bool enemy) {
		const Vector2 relative{
			(objectPosition.x - playerPosition.x) * layoutSettings_.scale,
			-(objectPosition.z - playerPosition.z) * layoutSettings_.scale,
		};
		const Vector2 unclamped{
			layoutSettings_.center.x + relative.x,
			layoutSettings_.center.y + relative.y,
		};
		Vector2 position = ClampToCircle(layoutSettings_.center, unclamped, layoutSettings_.radius);
		if (enemy) {
			enemyIconPositions_.push_back(position);
		} else {
			orbIconPositions_.push_back(position);
		}
	};

	for (const std::unique_ptr<Enemy>& enemy : enemyManager.GetEnemies()) {
		if (enemy && enemy->IsActive()) {
			addIcon(enemy->GetPosition(), true);
		}
	}

	for (const std::unique_ptr<ExpOrb>& orb : enemyManager.GetExpOrbs()) {
		if (orb && orb->IsActive()) {
			addIcon(orb->GetPosition(), false);
		}
	}
}

void MiniMap::Draw()
{
	if (!layoutSettings_.visible) {
		return;
	}

	if (backgroundSprite_) {
		backgroundSprite_->Update();
		backgroundSprite_->Draw();
	}

	if (playerIconSprite_) {
		DrawIcon(*playerIconSprite_, layoutSettings_.center, layoutSettings_.playerIconSize);
	}

	EnsureIconSpriteCount(enemyIconSprites_, enemyTexture_, enemyIconPositions_.size());
	for (size_t index = 0; index < enemyIconPositions_.size(); ++index) {
		DrawIcon(*enemyIconSprites_[index], enemyIconPositions_[index], layoutSettings_.enemyIconSize);
	}

	EnsureIconSpriteCount(orbIconSprites_, orbTexture_, orbIconPositions_.size());
	for (size_t index = 0; index < orbIconPositions_.size(); ++index) {
		DrawIcon(*orbIconSprites_[index], orbIconPositions_[index], layoutSettings_.orbIconSize);
	}
}

void MiniMap::DebugDrawImGui()
{
#ifdef _DEBUG
	if (!ImGui::CollapsingHeader("HUD Mini Map")) {
		return;
	}

	ImGui::Checkbox("Show Mini Map", &layoutSettings_.visible);
	ImGui::Checkbox("Enable HUD Debug##MiniMap", &layoutSettings_.debugEnabled);
	if (!layoutSettings_.debugEnabled) {
		return;
	}

	float backgroundPosition[2]{ layoutSettings_.backgroundPosition.x, layoutSettings_.backgroundPosition.y };
	if (ImGui::DragFloat2("MiniMap Background Position", backgroundPosition, 1.0f, -400.0f, 1280.0f)) {
		layoutSettings_.backgroundPosition = { backgroundPosition[0], backgroundPosition[1] };
		ApplyLayout();
	}

	float backgroundSize[2]{ layoutSettings_.backgroundSize.x, layoutSettings_.backgroundSize.y };
	if (ImGui::DragFloat2("MiniMap Background Size", backgroundSize, 1.0f, 32.0f, 1200.0f)) {
		layoutSettings_.backgroundSize = { (std::max)(32.0f, backgroundSize[0]), (std::max)(32.0f, backgroundSize[1]) };
		ApplyLayout();
	}

	float center[2]{ layoutSettings_.center.x, layoutSettings_.center.y };
	if (ImGui::DragFloat2("MiniMap Center", center, 1.0f, -400.0f, 1280.0f)) {
		layoutSettings_.center = { center[0], center[1] };
	}
	ImGui::DragFloat("MiniMap Radius", &layoutSettings_.radius, 1.0f, 16.0f, 360.0f);
	ImGui::DragFloat("MiniMap World Scale", &layoutSettings_.scale, 0.05f, 0.1f, 12.0f);
	ImGui::DragFloat("MiniMap Player Icon", &layoutSettings_.playerIconSize, 0.5f, 4.0f, 64.0f);
	ImGui::DragFloat("MiniMap Enemy Icon", &layoutSettings_.enemyIconSize, 0.5f, 4.0f, 64.0f);
	ImGui::DragFloat("MiniMap Orb Icon", &layoutSettings_.orbIconSize, 0.5f, 4.0f, 64.0f);

	if (ImGui::Button("Save Mini Map Layout")) {
		SaveLayout();
	}
#endif
}

void MiniMap::SaveLayout() const
{
	UILayoutIO::Save(DataPaths::kHudLayout,
		{
			{ "miniMapBackgroundPosition", { layoutSettings_.backgroundPosition.x, layoutSettings_.backgroundPosition.y } },
			{ "miniMapBackgroundSize", { layoutSettings_.backgroundSize.x, layoutSettings_.backgroundSize.y } },
			{ "miniMapCenter", { layoutSettings_.center.x, layoutSettings_.center.y } },
			{ "miniMapRadius", { layoutSettings_.radius } },
			{ "miniMapScale", { layoutSettings_.scale } },
			{ "miniMapPlayerIconSize", { layoutSettings_.playerIconSize } },
			{ "miniMapEnemyIconSize", { layoutSettings_.enemyIconSize } },
			{ "miniMapOrbIconSize", { layoutSettings_.orbIconSize } },
			{ "miniMapVisible", { layoutSettings_.visible ? 1.0f : 0.0f } },
		});
}

void MiniMap::ApplyLayout()
{
	if (backgroundSprite_) {
		backgroundSprite_->SetPosition(layoutSettings_.backgroundPosition);
		backgroundSprite_->SetSize(layoutSettings_.backgroundSize);
	}
}

void MiniMap::DrawIcon(Engine::Graphics2D::Sprite& sprite, const Vector2& position, float size)
{
	sprite.SetPosition(position);
	sprite.SetSize({ size, size });
	sprite.Update();
	sprite.Draw();
}

void MiniMap::EnsureIconSpriteCount(std::vector<std::unique_ptr<Engine::Graphics2D::Sprite>>& sprites, TextureHandle textureHandle, size_t count)
{
	while (sprites.size() < count) {
		std::unique_ptr<Engine::Graphics2D::Sprite> sprite = GameSpriteFactory::Create(textureHandle, layoutSettings_.center);
		sprite->SetAnchorPoint({ 0.5f, 0.5f });
		sprites.push_back(std::move(sprite));
	}
}

Vector2 MiniMap::ClampToCircle(const Vector2& center, const Vector2& position, float radius)
{
	const float dx = position.x - center.x;
	const float dy = position.y - center.y;
	const float distance = std::sqrt(dx * dx + dy * dy);
	if (distance <= radius || distance <= 0.0001f) {
		return position;
	}

	const float ratio = radius / distance;
	return {
		center.x + dx * ratio,
		center.y + dy * ratio,
	};
}

}
