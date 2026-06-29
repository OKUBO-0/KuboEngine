#pragma once

#include "Vector2.h"
#include "GameInputBindings.h"
#include "UILabel.h"
#include <array>
#include <cstdint>

namespace DirectXGame {

class PlayerManager;

enum class PauseMenuAction {
	None,
	Resume,
	BackToTitle,
};

class PauseBuildHud final {
public:
	void Initialize();
	void Start();
	PauseMenuAction Update(
		const PlayerManager& playerManager,
		float animationTime,
		int32_t moveDelta,
		bool confirmTriggered,
		bool cancelTriggered,
		GameInputBindings::NavigationInputDevice inputDevice);
	void Draw();
	void SaveLayout() const;

#ifdef _DEBUG
	void DrawDebugUI();
#endif

private:
	struct Layout {
		Vector2 position{ 52.0f, 116.0f };
		float stepX = 112.0f;
		Vector2 iconSize{ 96.0f, 54.0f };
		bool visible = true;
		bool debugEnabled = false;
	};

	struct MenuLayout {
		std::array<Vector2, 2> hitboxPositions{
			Vector2{ 840.0f, 294.0f },
			Vector2{ 840.0f, 462.0f },
		};
		Vector2 hitboxSize{ 280.0f, 92.0f };
	};

	void ApplyLayout();
	void UpdateBuildIcons(
		const PlayerManager& playerManager,
		float animationTime);
	void MoveSelection(int32_t delta);
	int32_t GetHoveredMenuIndex() const;

	UILabel overlay_;
	UILabel cursor_;
	std::array<UILabel, 5> icons_;
	Layout layout_{};
	MenuLayout menuLayout_{};
	int32_t selection_ = 0;
};

}
