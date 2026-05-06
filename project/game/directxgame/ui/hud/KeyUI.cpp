#include "game/directxgame/ui/hud/KeyUI.h"
#include "Input.h"
#include "game/directxgame/core/DirectXGameDataPaths.h"
#include "game/directxgame/core/GameInputBindings.h"
#include "game/directxgame/core/GameSpriteFactory.h"
#include "game/directxgame/core/UILayoutIO.h"
#include <algorithm>
#ifdef _DEBUG
#include <imgui.h>
#endif

namespace {

constexpr char kKeyWPath[] = "ui/controls/key_W.png";
constexpr char kKeyAPath[] = "ui/controls/key_a.png";
constexpr char kKeySPath[] = "ui/controls/key_s.png";
constexpr char kKeyDPath[] = "ui/controls/key_d.png";
constexpr char kKeyEscPath[] = "ui/controls/key_esc.png";

}

namespace DirectXGame {

void KeyUI::Initialize()
{
	const UILayoutIO::LayoutMap layout = UILayoutIO::LoadOrDefault(DataPaths::kHudLayout, {});
	layoutSettings_.position = UILayoutIO::GetVector2(layout, "keyUiPosition", layoutSettings_.position);
	layoutSettings_.size = UILayoutIO::GetVector2(layout, "keyUiSize", layoutSettings_.size);
	layoutSettings_.visible = UILayoutIO::GetFloat(layout, "keyUiVisible", layoutSettings_.visible ? 1.0f : 0.0f) > 0.5f;

	keySprites_[ToIndex(KeyType::W)] = GameSpriteFactory::Create(kKeyWPath, layoutSettings_.position);
	keySprites_[ToIndex(KeyType::A)] = GameSpriteFactory::Create(kKeyAPath, layoutSettings_.position);
	keySprites_[ToIndex(KeyType::S)] = GameSpriteFactory::Create(kKeySPath, layoutSettings_.position);
	keySprites_[ToIndex(KeyType::D)] = GameSpriteFactory::Create(kKeyDPath, layoutSettings_.position);
	keySprites_[ToIndex(KeyType::Esc)] = GameSpriteFactory::Create(kKeyEscPath, layoutSettings_.position);

	ApplyLayout();
}

void KeyUI::Update(Engine::InputSystem::Input* input)
{
	const Vector2 move = GameInputBindings::GetMoveVector(input);
	SetKeyColor(KeyType::W, move.y > 0.15f);
	SetKeyColor(KeyType::A, move.x < -0.15f);
	SetKeyColor(KeyType::S, move.y < -0.15f);
	SetKeyColor(KeyType::D, move.x > 0.15f);
	SetKeyColor(KeyType::Esc, input && GameInputBindings::IsKeyboardPauseTriggered(input));
}

void KeyUI::Draw()
{
	if (!layoutSettings_.visible) {
		return;
	}

	for (const std::unique_ptr<Engine::Graphics2D::Sprite>& sprite : keySprites_) {
		if (!sprite) {
			continue;
		}
		sprite->Update();
		sprite->Draw();
	}
}

void KeyUI::DebugDrawImGui()
{
#ifdef _DEBUG
	if (!ImGui::CollapsingHeader("HUD Key UI")) {
		return;
	}

	ImGui::Checkbox("Show Key UI", &layoutSettings_.visible);
	ImGui::Checkbox("Enable HUD Debug##KeyUI", &layoutSettings_.debugEnabled);
	if (!layoutSettings_.debugEnabled) {
		return;
	}

	float position[2]{ layoutSettings_.position.x, layoutSettings_.position.y };
	if (ImGui::DragFloat2("Key UI Position", position, 1.0f, -400.0f, 1280.0f)) {
		layoutSettings_.position = { position[0], position[1] };
		ApplyLayout();
	}

	float size[2]{ layoutSettings_.size.x, layoutSettings_.size.y };
	if (ImGui::DragFloat2("Key UI Size", size, 1.0f, 16.0f, 1600.0f)) {
		layoutSettings_.size = { (std::max)(16.0f, size[0]), (std::max)(16.0f, size[1]) };
		ApplyLayout();
	}

	if (ImGui::Button("Save Key UI Layout")) {
		SaveLayout();
	}
#endif
}

void KeyUI::SaveLayout() const
{
	UILayoutIO::Save(DataPaths::kHudLayout,
		{
			{ "keyUiPosition", { layoutSettings_.position.x, layoutSettings_.position.y } },
			{ "keyUiSize", { layoutSettings_.size.x, layoutSettings_.size.y } },
			{ "keyUiVisible", { layoutSettings_.visible ? 1.0f : 0.0f } },
		});
}

void KeyUI::ApplyLayout()
{
	for (const std::unique_ptr<Engine::Graphics2D::Sprite>& sprite : keySprites_) {
		if (!sprite) {
			continue;
		}
		sprite->SetPosition(layoutSettings_.position);
		sprite->SetSize(layoutSettings_.size);
	}
}

void KeyUI::SetKeyColor(KeyType keyType, bool pressed)
{
	Engine::Graphics2D::Sprite* sprite = keySprites_[ToIndex(keyType)].get();
	if (!sprite) {
		return;
	}

	sprite->SetColor(pressed
		? Vector4{ 1.0f, 1.0f, 0.0f, 1.0f }
		: Vector4{ 1.0f, 1.0f, 1.0f, 1.0f });
}

}
