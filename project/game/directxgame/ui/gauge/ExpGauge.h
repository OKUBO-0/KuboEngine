#pragma once

#include "Sprite.h"
#include "game/directxgame/core/GameTextureCache.h"
#include "game/directxgame/ui/common/UIBar.h"
#include "game/directxgame/ui/common/UILabel.h"
#include <array>
#include <cstdint>
#include <memory>

namespace DirectXGame {

class ExpGauge {
public:
	void Initialize();
	void Update();
	void Draw();

	void SetEXP(int32_t current, int32_t max);
	void SetLevel(int32_t level);
	bool IsFilled() const;
	void DebugDrawImGui();
	void SaveLayout() const;

private:
	struct LayoutSettings {
		Vector2 framePosition{ 0.0f, 1.0f };
		Vector2 frameSize{ 1280.0f, 50.0f };
		Vector2 gaugePosition{ 5.0f, 6.0f };
		Vector2 gaugeSize{ 1270.0f, 40.0f };
		Vector2 lvLabelPosition{ 1175.0f, 10.0f };
		Vector2 lvLabelSize{ 48.0f, 32.0f };
		Vector2 lvDigitsPosition{ 1225.0f, 10.0f };
		bool debugEnabled = false;
	};

	void ApplyLayout();

	TextureHandle lvDigitsHandle_ = 0;
	UIBar frameBar_;
	UIBar gaugeBar_;
	UILabel lvLabel_;
	static constexpr int32_t kLvDigits = 2;
	std::array<std::unique_ptr<Engine::Graphics2D::Sprite>, kLvDigits> sprite_;
	Vector2 digitSize_{ 24.0f, 32.0f };
	int32_t displayedExp_ = 0;
	int32_t targetExp_ = 0;
	int32_t maxExp_ = 1;
	LayoutSettings layoutSettings_{};
};

}
