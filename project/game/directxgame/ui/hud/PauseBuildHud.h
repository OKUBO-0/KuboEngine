#pragma once

#include "Vector2.h"
#include "GameInputBindings.h"
#include "UILabel.h"
#include "UIPanel.h"
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace DirectXGame {

class PlayerManager;

enum class PauseMenuAction {
	None,
	Resume,
	Restart,
	Exit,
};

class PauseBuildHud final {
public:
	void Initialize();
	void Start();
	void TrackAcquisitions(const PlayerManager& playerManager);
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
		Vector2 position{ 52.0f, 235.0f };
		float stepX = 62.0f;
		float stepY = 190.0f;
		Vector2 iconSize{ 58.0f, 58.0f };
		bool visible = true;
		bool debugEnabled = false;
	};

	struct MenuLayout {
		std::array<Vector2, 3> hitboxPositions{
			Vector2{ 470.0f, 255.0f },
			Vector2{ 450.0f, 385.0f },
			Vector2{ 540.0f, 520.0f },
		};
		std::array<Vector2, 3> hitboxSizes{
			Vector2{ 350.0f, 82.0f },
			Vector2{ 390.0f, 82.0f },
			Vector2{ 200.0f, 82.0f },
		};
		std::array<Vector2, 3> leftCursorOffsets{
			Vector2{ 0.0f, 0.0f }, Vector2{ -16.0f, 128.0f }, Vector2{ 75.0f, 266.0f },
		};
		std::array<Vector2, 3> rightCursorOffsets{
			Vector2{ 0.0f, 0.0f }, Vector2{ 15.0f, 128.0f }, Vector2{ -77.0f, 266.0f },
		};
	};

	void ApplyLayout();
	void UpdateBuildIcons(
		const PlayerManager& playerManager,
		float animationTime);
	void MoveSelection(int32_t delta);
	int32_t GetHoveredMenuIndex() const;

	UILabel overlay_;
	UILabel leftCursor_;
	UILabel rightCursor_;
	static constexpr size_t kIconCount = 9;
	std::array<std::unique_ptr<Engine::Graphics2D::Sprite>, kIconCount> icons_;
	std::array<std::unique_ptr<Engine::Graphics2D::Sprite>, kIconCount> levelDigits_;
	std::vector<int32_t> weaponAcquisitionOrder_{ 0 };
	std::vector<int32_t> itemAcquisitionOrder_;
	std::array<bool, kIconCount> acquisitionRecorded_{ true, false, false, false, false, false, false, false, false };
	bool acquisitionBaselineInitialized_ = false;
	int32_t baselineMaxHP_ = 0;
	int32_t baselineAttackPower_ = 0;
	int32_t baselineMoveSpeedLevel_ = 0;
	float baselineExpPickupRangeMultiplier_ = 1.0f;
	UIPanel vignetteBase_;
	static constexpr size_t kVignetteLayerCount = 8;
	std::array<std::array<UIPanel, 4>, kVignetteLayerCount> vignettePanels_;
	Layout layout_{};
	MenuLayout menuLayout_{};
	int32_t selection_ = 0;
	Vector2 currentLeftCursorPosition_{};
	Vector2 currentRightCursorPosition_{};
	bool cursorPositionInitialized_ = false;
};

}
