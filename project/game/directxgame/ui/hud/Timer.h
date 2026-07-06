#pragma once

#include "BitmapText.h"
#include "UIElement.h"

namespace DirectXGame {

class Timer : public UIElement {
public:
	void Initialize();
	void Update(float deltaTime);
	void Draw() override;

	void SetPosition(const Vector2& position);
	void SetScale(float scale);
	void SetTime(float time);

	float GetTime() const { return time_; }
	void Reset();
	void DebugDrawImGui();
	void SaveLayout() const;

private:
	struct LayoutSettings {
		Vector2 position{ 580.0f, 70.0f };
		float scale = 1.0f;
		bool debugEnabled = false;
	};

	void OnTransformChanged() override;
	void RefreshLayout();
	void UpdateBounds();
	void ApplyLayout();
	void UpdateDisplay();

	float time_ = 0.0f;
	BitmapText timeText_;
	Vector2 digitSize_{ 24.0f, 32.0f };
	LayoutSettings layoutSettings_{};
};

}
