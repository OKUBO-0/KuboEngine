#pragma once

#include "Vector2.h"
#include "Vector4.h"
#include <cstdint>
#include <memory>

namespace DirectXGame {

class HpGaugeCapsulePanel;

class HpGauge {
public:
	HpGauge();
	~HpGauge();

	void Initialize();
	void Update();
	void Draw();

	void SetHP(int32_t current, int32_t max);
	bool IsDepleted() const;
	void DebugDrawImGui();
	void SaveLayout() const;

private:
	struct LayoutSettings {
		Vector2 position{ 440.0f, 660.0f };
		Vector2 size{ 400.0f, 18.0f };
		bool debugEnabled = false;
	};

	void ApplyLayout();
	void RefreshFillLayout();

	std::unique_ptr<HpGaugeCapsulePanel> frame_;
	std::unique_ptr<HpGaugeCapsulePanel> background_;
	std::unique_ptr<HpGaugeCapsulePanel> fill_;
	int32_t displayedHP_ = 0;
	int32_t targetHP_ = 0;
	int32_t maxHP_ = kDefaultMaxHP;
	float displayedRate_ = 0.0f;

	static constexpr int32_t kDefaultMaxHP = 1;
	LayoutSettings layoutSettings_{};
};

}
