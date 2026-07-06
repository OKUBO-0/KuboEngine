#pragma once

#include "BitmapText.h"
#include "UIBar.h"
#include "UILabel.h"
#include "UIPanel.h"
#include <cstdint>

namespace DirectXGame {

class ExpGauge {
public:
	void Initialize();
	void Update();
	void Draw();

	void SetEXP(int32_t current, int32_t max);
	void SetLevel(int32_t level);
	void SetLevelUpSelectionActive(bool active);
	bool IsFilled() const;
	void DebugDrawImGui();
	void SaveLayout() const;

private:
	struct LayoutSettings {
		Vector2 framePosition{ 0.0f, 1.0f };
		Vector2 frameSize{ 1280.0f, 50.0f };
		Vector2 gaugePosition{ 5.0f, 6.0f };
		Vector2 gaugeSize{ 1270.0f, 40.0f };
		Vector2 lvLabelPosition{ 1168.0f, 10.0f };
		Vector2 lvLabelSize{ 48.0f, 32.0f };
		Vector2 lvDigitsPosition{ 1214.0f, 10.0f };
		Vector2 lvDigitSize{ 24.0f, 32.0f };
		float lvScale = 1.0f;
		bool debugEnabled = false;
	};

	void ApplyLayout();

	UIBar glowBar_;
	UIBar frameBar_;
	UIBar gaugeBar_;
	std::array<UIPanel, 4> lightSweeps_;
	BitmapText lvLabelText_;
	BitmapText lvDigitsText_;
	int32_t displayedExp_ = 0;
	int32_t targetExp_ = 0;
	int32_t maxExp_ = 1;
	int32_t level_ = 1;
	float feedbackPulseTimer_ = 0.0f;
	float selectionPulseTime_ = 0.0f;
	bool levelUpSelectionActive_ = false;
	LayoutSettings layoutSettings_{};
};

}
