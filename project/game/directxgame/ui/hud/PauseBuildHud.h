#pragma once

#include "Vector2.h"
#include "GameInputBindings.h"
#include "GameAudioCache.h"
#include "UILabel.h"
#include "UIPanel.h"
#include "BitmapText.h"
#include "PassiveItemType.h"
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
		Vector2 statusPosition{ 930.0f, 210.0f };
		float statusLineStep = 23.0f;
		float statusScale = 0.21f;
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
	void UpdateStatusTexts(const PlayerManager& playerManager);
	void MoveSelection(int32_t delta);
	int32_t GetHoveredMenuIndex() const;

	UILabel overlay_;
	UILabel leftCursor_;
	UILabel rightCursor_;
	UIPanel menuPanel_;
	std::array<UIPanel, 4> menuPanelBorders_;
	std::array<UIPanel, 3> menuOptionBackgrounds_;
	std::array<UIPanel, 12> menuOptionBorders_;
	std::array<BitmapText, 3> menuOptionTexts_;
	BitmapText pauseTitleText_;
	std::array<UIPanel, 8> cursorPanels_;
	UIPanel buildPanel_;
	std::array<UIPanel, 4> buildPanelBorders_;
	UIPanel statusPanel_;
	std::array<UIPanel, 4> statusPanelBorders_;
	static constexpr size_t kWeaponIconCount = 10;
	static constexpr size_t kPassiveItemSlotCount = 6;
	static constexpr size_t kIconCount =
		kWeaponIconCount + kPassiveItemSlotCount;
	static constexpr size_t kMaxLevelPipsPerIcon = 8;
	std::array<std::unique_ptr<Engine::Graphics2D::Sprite>, kIconCount> icons_;
	std::array<std::unique_ptr<Engine::Graphics2D::Sprite>, kPassiveItemSlotCount> itemSubIcons_;
	std::array<std::unique_ptr<Engine::Graphics2D::Sprite>, kIconCount> levelDigits_;
	std::array<UIPanel, kIconCount * kMaxLevelPipsPerIcon> levelPips_;
	static constexpr size_t kStatusLineCount = 16;
	std::array<BitmapText, kStatusLineCount> statusTexts_;
	std::array<BitmapText, kStatusLineCount> statusValueTexts_;
	std::array<PassiveItemType, kPassiveItemSlotCount> displayedItemTypes_{
		PassiveItemType::Count, PassiveItemType::Count,
		PassiveItemType::Count, PassiveItemType::Count,
		PassiveItemType::Count, PassiveItemType::Count };
	std::vector<int32_t> weaponAcquisitionOrder_{ 0 };
	std::array<bool, kIconCount> acquisitionRecorded_{
		true, false, false, false, false, false, false,
		false, false, false, false, false, false, false,
		false, false };
	UIPanel vignetteBase_;
	static constexpr size_t kVignetteLayerCount = 8;
	std::array<std::array<UIPanel, 4>, kVignetteLayerCount> vignettePanels_;
	Layout layout_{};
	MenuLayout menuLayout_{};
	int32_t selection_ = 0;
	Vector2 currentLeftCursorPosition_{};
	Vector2 currentRightCursorPosition_{};
	bool cursorPositionInitialized_ = false;
	SoundHandle selectSeHandle_{};
	SoundHandle decideSeHandle_{};
	SoundHandle backSeHandle_{};
};

}
