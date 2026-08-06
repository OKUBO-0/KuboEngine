#include "KeyUI.h"
#include "Input.h"
#include "DataPaths.h"
#include "GameInputBindings.h"
#include "GameSpriteFactory.h"
#include "UILayoutIO.h"
#include <algorithm>
#include <array>
#ifdef _DEBUG
#include <imgui.h>
#endif

namespace {

constexpr char kKeyWPath[] = "ui/controls/key_W.png";
constexpr char kKeyAPath[] = "ui/controls/key_a.png";
constexpr char kKeySPath[] = "ui/controls/key_s.png";
constexpr char kKeyDPath[] = "ui/controls/key_d.png";
constexpr char kKeyEscPath[] = "ui/controls/key_esc.png";
constexpr char kCirclePath[] = "ui/controls/circle_white.png";
constexpr Vector4 kGuidePanelColor{ 0.0f, 0.0f, 0.0f, 0.66f };
constexpr Vector4 kGuideActiveColor{ 1.0f, 0.92f, 0.06f, 0.92f };
constexpr Vector4 kGuideFrameColor{ 1.0f, 0.96f, 0.0f, 1.0f };
constexpr Vector4 kGuideTextColor{ 1.0f, 1.0f, 1.0f, 1.0f };
constexpr char kGuideFontTexture[] = "ui/font/noto_sans_jp_black.png";
constexpr char kGuideFontMetrics[] = "ui/font/noto_sans_jp_black.json";

void CenterTextInRect(
	DirectXGame::BitmapText& text,
	const Vector2& position,
	const Vector2& size,
	float scale,
	float maxWidth)
{
	text.SetScaleToFit(scale, maxWidth);
	text.SetPosition({
		position.x + (size.x - text.MeasureWidth()) * 0.5f,
		position.y + (size.y - 22.0f) * 0.5f,
		});
}

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
	const std::array<const char*, kKeyboardGuideButtonCount> keyLabels{
		"W",
		"A",
		"S",
		"D",
		"SHIFT",
		"SPACE",
	};
	for (size_t index = 0; index < keyPanels_.size(); ++index) {
		keyPanels_[index].Initialize();
		keyTexts_[index].Initialize(kGuideFontTexture, kGuideFontMetrics);
		keyTexts_[index].SetText(keyLabels[index]);
		keyTexts_[index].SetScale(index >= 4 ? 0.16f : 0.28f);
		keyTexts_[index].SetAdvanceMultiplier(0.92f);
		keyTexts_[index].SetColor(kGuideTextColor);
	}
	stickBasePanel_.Initialize();
	stickKnobPanel_.Initialize();
	stickBaseSprite_ = GameSpriteFactory::Create(kCirclePath, { 0.0f, 0.0f });
	stickKnobSprite_ = GameSpriteFactory::Create(kCirclePath, { 0.0f, 0.0f });
	stickText_.Initialize(kGuideFontTexture, kGuideFontMetrics);
	stickText_.SetText("L");
	stickText_.SetScale(0.28f);
	stickText_.SetColor(kGuideTextColor);
	const std::array<const char*, 4> padLabels{ "Y", "X", "A", "B" };
	for (size_t index = 0; index < padButtonPanels_.size(); ++index) {
		padButtonPanels_[index].Initialize();
		padButtonSprites_[index] = GameSpriteFactory::Create(kCirclePath, { 0.0f, 0.0f });
		padButtonTexts_[index].Initialize(kGuideFontTexture, kGuideFontMetrics);
		padButtonTexts_[index].SetText(padLabels[index]);
		padButtonTexts_[index].SetScale(0.24f);
		padButtonTexts_[index].SetColor(kGuideTextColor);
	}

	ApplyLayout();
}

void KeyUI::Update(Engine::InputSystem::Input* input)
{
	inputDevice_ = GameInputBindings::DetectNavigationInputDevice(input, inputDevice_);
	const Vector2 move = GameInputBindings::GetMoveVector(input);
	lastMoveVector_ = move;
	UpdateKeyboardGuide(input);
	UpdateGamepadGuide(input);
	SetKeyColor(KeyType::W, move.y > 0.15f);
	SetKeyColor(KeyType::A, move.x < -0.15f);
	SetKeyColor(KeyType::S, move.y < -0.15f);
	SetKeyColor(KeyType::D, move.x > 0.15f);
	SetKeyColor(KeyType::Esc, input && GameInputBindings::IsPauseTriggered(input));
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
		sprite->SetColor({ 1.0f, 1.0f, 1.0f, 0.0f });
	}
	if (inputDevice_ == GameInputBindings::NavigationInputDevice::Gamepad) {
		DrawGamepadGuide();
	} else {
		DrawKeyboardGuide();
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
	const Vector2 base{ layoutSettings_.position.x, layoutSettings_.position.y };
	const Vector2 keySize{ 34.0f, 34.0f };
	const std::array<Vector2, kKeyboardGuideButtonCount> keyPositions{
		Vector2{ base.x + 48.0f, base.y },
		Vector2{ base.x + 8.0f, base.y + 38.0f },
		Vector2{ base.x + 48.0f, base.y + 38.0f },
		Vector2{ base.x + 88.0f, base.y + 38.0f },
		Vector2{ base.x + 8.0f, base.y + 78.0f },
		Vector2{ base.x + 86.0f, base.y + 78.0f },
	};
	const std::array<Vector2, kKeyboardGuideButtonCount> keySizes{
		keySize,
		keySize,
		keySize,
		keySize,
		Vector2{ 72.0f, 30.0f },
		Vector2{ 92.0f, 30.0f },
	};
	for (size_t index = 0; index < keyPanels_.size(); ++index) {
		keyPanels_[index].SetPosition(keyPositions[index]);
		keyPanels_[index].SetSize(keySizes[index]);
		CenterTextInRect(
			keyTexts_[index],
			keyPositions[index],
			keySizes[index],
			index >= 4 ? 0.16f : 0.28f,
			keySizes[index].x - 8.0f);
	}
	const Vector2 stickBase{ base.x + 8.0f, base.y + 8.0f };
	stickBasePanel_.SetPosition(stickBase);
	stickBasePanel_.SetSize({ 76.0f, 76.0f });
	if (stickBaseSprite_) {
		stickBaseSprite_->SetPosition(stickBase);
		stickBaseSprite_->SetSize({ 76.0f, 76.0f });
	}
	if (stickKnobSprite_) {
		stickKnobSprite_->SetSize({ 22.0f, 22.0f });
	}
	CenterTextInRect(stickText_, stickBase, { 76.0f, 76.0f }, 0.28f, 46.0f);
	const std::array<Vector2, 4> buttonPositions{
		Vector2{ base.x + 148.0f, base.y + 4.0f },
		Vector2{ base.x + 110.0f, base.y + 42.0f },
		Vector2{ base.x + 148.0f, base.y + 80.0f },
		Vector2{ base.x + 186.0f, base.y + 42.0f },
	};
	for (size_t index = 0; index < padButtonPanels_.size(); ++index) {
		padButtonPanels_[index].SetPosition(buttonPositions[index]);
		padButtonPanels_[index].SetSize({ 34.0f, 34.0f });
		if (padButtonSprites_[index]) {
			padButtonSprites_[index]->SetPosition(buttonPositions[index]);
			padButtonSprites_[index]->SetSize({ 34.0f, 34.0f });
		}
		CenterTextInRect(
			padButtonTexts_[index],
			buttonPositions[index],
			{ 34.0f, 34.0f },
			0.24f,
			24.0f);
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

void KeyUI::SetGuidePanel(UIPanel& panel, const Vector2& position, const Vector2& size, bool active)
{
	panel.SetPosition(position);
	panel.SetSize(size);
	panel.SetColor(active ? kGuideActiveColor : kGuidePanelColor);
}

void KeyUI::UpdateKeyboardGuide(Engine::InputSystem::Input* input)
{
	const std::array<bool, kKeyboardGuideButtonCount> active{
		input && input->PushKey(DIK_W),
		input && input->PushKey(DIK_A),
		input && input->PushKey(DIK_S),
		input && input->PushKey(DIK_D),
		input && (input->PushKey(DIK_LSHIFT) || input->PushKey(DIK_RSHIFT)),
		input && input->PushKey(DIK_SPACE),
	};
	for (size_t index = 0; index < keyPanels_.size(); ++index) {
		keyPanels_[index].SetColor(active[index] ? kGuideActiveColor : kGuidePanelColor);
	}
}

void KeyUI::UpdateGamepadGuide(Engine::InputSystem::Input* input)
{
	stickBasePanel_.SetColor(kGuidePanelColor);
	const Vector2 base = stickBasePanel_.GetPosition();
	const Vector2 size = stickBasePanel_.GetSize();
	const Vector2 knobPosition{
		base.x + size.x * 0.5f - 11.0f + lastMoveVector_.x * 20.0f,
		base.y + size.y * 0.5f - 11.0f - lastMoveVector_.y * 20.0f,
	};
	stickKnobPanel_.SetPosition(knobPosition);
	if (stickKnobSprite_) {
		stickKnobSprite_->SetPosition(knobPosition);
	}
	stickKnobPanel_.SetSize({ 22.0f, 22.0f });
	if (stickBaseSprite_) {
		stickBaseSprite_->SetColor(kGuidePanelColor);
	}
	const bool stickActive =
		(std::abs(lastMoveVector_.x) + std::abs(lastMoveVector_.y)) > 0.15f;
	const Vector4 stickKnobColor = stickActive ? kGuideActiveColor : kGuideFrameColor;
	if (stickKnobSprite_) {
		stickKnobSprite_->SetColor(stickKnobColor);
	}
	stickKnobPanel_.SetColor(stickKnobColor);
	const std::array<bool, 4> active{
		input && input->PushGamePadButton(XINPUT_GAMEPAD_Y),
		input && input->PushGamePadButton(XINPUT_GAMEPAD_X),
		input && input->PushGamePadButton(XINPUT_GAMEPAD_A),
		input && input->PushGamePadButton(XINPUT_GAMEPAD_B),
	};
	for (size_t index = 0; index < padButtonPanels_.size(); ++index) {
		const Vector4 color = active[index] ? kGuideActiveColor : kGuidePanelColor;
		padButtonPanels_[index].SetColor(color);
		if (padButtonSprites_[index]) {
			padButtonSprites_[index]->SetColor(color);
		}
	}
}

void KeyUI::DrawKeyboardGuide()
{
	for (UIPanel& panel : keyPanels_) {
		panel.Draw();
	}
	for (BitmapText& text : keyTexts_) {
		text.Draw();
	}
}

void KeyUI::DrawGamepadGuide()
{
	if (stickBaseSprite_) {
		stickBaseSprite_->Update();
		stickBaseSprite_->Draw();
	}
	if (stickKnobSprite_) {
		stickKnobSprite_->Update();
		stickKnobSprite_->Draw();
	}
	stickText_.Draw();
	for (const std::unique_ptr<Engine::Graphics2D::Sprite>& sprite : padButtonSprites_) {
		if (sprite) {
			sprite->Update();
			sprite->Draw();
		}
	}
	for (BitmapText& text : padButtonTexts_) {
		text.Draw();
	}
}

}
