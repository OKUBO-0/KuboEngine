#pragma once

#include "game/directxgame/ui/common/UIBar.h"
#include <cstdint>

namespace DirectXGame {

class HpGauge {
public:
	void Initialize();
	void Update();
	void Draw();

	void SetHP(int32_t current, int32_t max);
	bool IsDepleted() const;
	void DebugDrawImGui();
	void SaveLayout() const;

private:
	struct LayoutSettings {
		Vector2 position{ 600.0f, 450.0f };
		Vector2 size{ 80.0f, 10.0f };
		bool debugEnabled = false;
	};

	void ApplyLayout();

	UIBar gauge_;
	int32_t displayedHP_ = 0;
	int32_t targetHP_ = 0;
	int32_t maxHP_ = kDefaultMaxHP;

	static constexpr int32_t kDefaultMaxHP = 1;
	LayoutSettings layoutSettings_{};
};

}
