#pragma once

#include "Sprite.h"
#include "Vector2.h"
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

	struct LayoutSettings {
		Vector2 position{ 0.0f, 0.0f };
		Vector2 size{ 1280.0f, 720.0f };
		bool visible = true;
		bool debugEnabled = false;
	};

	void ApplyLayout();
	void SetKeyColor(KeyType keyType, bool pressed);
	static constexpr size_t ToIndex(KeyType keyType) { return static_cast<size_t>(keyType); }

	std::array<std::unique_ptr<Engine::Graphics2D::Sprite>, kKeyCount> keySprites_;
	LayoutSettings layoutSettings_{};
};

}
