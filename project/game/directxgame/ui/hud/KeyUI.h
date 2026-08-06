#pragma once

#include "Sprite.h"
#include "UIPanel.h"
#include "BitmapText.h"
#include "Vector2.h"
#include "GameInputBindings.h"
#include <array>
#include <memory>

namespace Engine::InputSystem {
class Input;
}

namespace DirectXGame {

class KeyUI {
public:
	void Initialize();
	void Update(Engine::InputSystem::Input* input);
	void Draw();
	void DebugDrawImGui();
	void SaveLayout() const;

private:
	enum class KeyType {
		W,
		A,
		S,
		D,
		Esc,
	};
	static constexpr size_t kKeyCount = 5;
	static constexpr size_t kKeyboardGuideButtonCount = 6;

	struct LayoutSettings {
		Vector2 position{ 0.0f, 0.0f };
		Vector2 size{ 1280.0f, 720.0f };
		bool visible = true;
		bool debugEnabled = false;
	};

	void ApplyLayout();
	void SetKeyColor(KeyType keyType, bool pressed);
	void SetGuidePanel(UIPanel& panel, const Vector2& position, const Vector2& size, bool active);
	void UpdateKeyboardGuide(Engine::InputSystem::Input* input);
	void UpdateGamepadGuide(Engine::InputSystem::Input* input);
	void DrawKeyboardGuide();
	void DrawGamepadGuide();
	static constexpr size_t ToIndex(KeyType keyType) { return static_cast<size_t>(keyType); }

	std::array<std::unique_ptr<Engine::Graphics2D::Sprite>, kKeyCount> keySprites_;
	std::array<UIPanel, kKeyboardGuideButtonCount> keyPanels_;
	std::array<BitmapText, kKeyboardGuideButtonCount> keyTexts_;
	UIPanel stickBasePanel_;
	UIPanel stickKnobPanel_;
	std::unique_ptr<Engine::Graphics2D::Sprite> stickBaseSprite_;
	std::unique_ptr<Engine::Graphics2D::Sprite> stickKnobSprite_;
	BitmapText stickText_;
	std::array<UIPanel, 4> padButtonPanels_;
	std::array<std::unique_ptr<Engine::Graphics2D::Sprite>, 4> padButtonSprites_;
	std::array<BitmapText, 4> padButtonTexts_;
	GameInputBindings::NavigationInputDevice inputDevice_ =
		GameInputBindings::NavigationInputDevice::Keyboard;
	Vector2 lastMoveVector_{};
	LayoutSettings layoutSettings_{};
};

}
