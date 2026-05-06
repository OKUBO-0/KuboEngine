#pragma once

#include "Vector4.h"
#include "game/directxgame/ui/common/UIElement.h"
#include "game/directxgame/ui/common/UIPanel.h"

namespace DirectXGame {

class UIBar : public UIElement {
public:
	void Initialize();
	void SetColors(const Vector4& backgroundColor, const Vector4& fillColor);
	void SetRate(float rate);
	void Draw() override;

private:
	void OnTransformChanged() override;
	void RefreshLayout();

	UIPanel background_;
	UIPanel fill_;
	float rate_ = 1.0f;
};

}
