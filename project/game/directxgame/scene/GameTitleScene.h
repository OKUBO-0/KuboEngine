#pragma once

#include "BaseScene.h"
#include "Camera.h"
#include "GameAudioCache.h"
#include "Object3D.h"
#include "Sprite.h"
#include "Vector2.h"
#include "Vector3.h"
#include "GameTextureCache.h"
#include "GameInputBindings.h"
#include "CurtainTransition.h"
#include "UILabel.h"
#include "UIPanel.h"
#include "BitmapText.h"
#include "GridPlane.h"
#include <array>
#include <cstdint>
#include <memory>

namespace DirectXGame {

class GameSession;
class TitleSceneDebugUIController;

class TitleScene : public Engine::Scene::BaseScene {
	friend class TitleSceneDebugUIController;

public:
	explicit TitleScene(std::shared_ptr<GameSession> sessionContext);

	void Initialize() override;
	void Finalize() override;
	void Update() override;
	void Draw() override;

private:
	struct LayoutSettings {
		Vector2 titlePosition{ 0.0f, 0.0f };
		Vector2 titleSize{ 1280.0f, 720.0f };
		Vector2 cursorBasePosition{ 0.0f, 0.0f };
		Vector2 cursorSize{ 1280.0f, 720.0f };
		Vector2 cursorShopOffset{ 300.0f, 0.0f };
		Vector2 cursorQuitOffset{ 0.0f, 128.0f };
		float cursorEasingSpeed = 14.0f;
		Vector2 menuHitboxPosition{ 145.0f, 340.0f };
		Vector2 menuHitboxSize{ 300.0f, 88.0f };
		std::array<Vector2, 5> shopItemPositions{ {
			{ 227.0f, 164.0f }, { 400.0f, 164.0f }, { 573.0f, 164.0f },
			{ 746.0f, 164.0f }, { 920.0f, 164.0f },
		} };
		Vector2 shopItemHitboxSize{ 134.0f, 134.0f };
		Vector2 shopLevelSquareSize{ 14.0f, 14.0f };
		float shopLevelSquareStepX = 19.0f;
		float shopLevelSquareOffsetY = 116.0f;
		Vector2 shopPriceOffset{ 31.0f, 150.0f };
		Vector2 shopPriceDigitSize{ 18.0f, 24.0f };
		float shopPriceDigitStepX = 17.0f;
		Vector2 shopCoinPosition{ 48.0f, 42.0f };
		Vector2 shopCoinDigitSize{ 20.0f, 26.0f };
		float shopCoinDigitStepX = 20.0f;
		Vector3 modelBasePosition{ -18.0f, 0.0f, 8.0f };
		Vector3 modelScale{ 4.5f, 4.5f, 4.5f };
		Vector3 cameraTarget{ 0.0f, 4.5f, 0.0f };
		float cameraDistance = 76.0f;
		float cameraHeight = 44.0f;
		float cameraPitch = 0.48f;
		float cameraYaw = 0.0f;
		float cameraOrbitSpeed = 0.12f;
		Vector2 selectBackgroundPosition{ 0.0f, 0.0f };
		Vector2 selectBackgroundSize{ 1280.0f, 720.0f };
		Vector2 selectTitleTextPosition{ 72.0f, 98.0f };
		float selectTitleTextScale = 0.36f;
		Vector2 selectIconBasePosition{ 52.0f, 142.0f };
		Vector2 selectIconSize{ 92.0f, 92.0f };
		Vector2 selectIconHitboxSize{ 104.0f, 104.0f };
		float selectIconStepX = 103.0f;
		float selectIconStepY = 104.0f;
		Vector3 selectIconModelBasePosition{ -24.0f, 1.4f, 8.0f };
		Vector3 selectIconModelStep{ 5.0f, 0.0f, 0.0f };
		Vector3 selectIconModelScale{ 0.0048f, 0.0048f, 0.0048f };
		Vector3 selectIconModelRotation{ 0.0f, 0.34f, 0.0f };
		Vector3 selectIconWeaponOffset{ 1.0f, 2.4f, 0.2f };
		Vector3 selectIconWeaponScale{ 0.01f, 0.01f, 0.01f };
		Vector3 selectIconWeaponRotation{ 0.0f, 0.34f, 0.0f };
		Vector2 selectDetailFacePosition{ 940.0f, 165.0f };
		Vector2 selectDetailFaceSize{ 92.0f, 92.0f };
		Vector2 selectWeaponIconPosition{ 940.0f, 292.0f };
		Vector2 selectWeaponIconSize{ 112.0f, 112.0f };
		Vector2 selectWeaponDescriptionPosition{ 1064.0f, 310.0f };
		float selectWeaponDescriptionScale = 0.18f;
		float selectWeaponDescriptionMaxWidth = 150.0f;
		Vector2 selectActionButtonPosition{ 1014.0f, 528.0f };
		Vector2 selectActionButtonSize{ 154.0f, 48.0f };
		Vector2 selectActionTextOffset{ 38.0f, 8.0f };
		float selectActionTextScale = 0.28f;
		Vector3 selectModelPosition{ 4.0f, 2.0f, 8.0f };
		Vector3 selectModelScale{ 0.019f, 0.019f, 0.019f };
		Vector3 selectModelRotation{ 0.0f, -2.618f, 0.0f };
		Vector3 selectModelWeaponOffset{ 2.8f, 2.2f, 0.3f };
		Vector3 selectModelWeaponScale{ 0.019f, 0.019f, 0.019f };
		Vector3 selectModelWeaponRotation{ 0.0f, 0.0f, 0.0f };
		Vector3 selectDetailModelPosition{ 20.8f, 2.2f, 8.0f };
		Vector3 selectDetailModelScale{ 0.007f, 0.007f, 0.007f };
		Vector3 selectDetailModelRotation{ 0.0f, 0.34f, 0.0f };
		Vector3 selectDetailWeaponOffset{ 1.2f, 1.0f, 0.2f };
		Vector3 selectDetailWeaponScale{ 0.0105f, 0.0105f, 0.0105f };
		Vector3 selectDetailWeaponRotation{ 0.0f, 0.34f, 0.0f };
		bool debugEnabled = false;
	};

	struct DebugWindowVisibility {
		bool windowSwitcher = false;
		bool titleView = true;
		bool statisticsView = true;
		bool titleSettings = true;
		bool titleModelSettings = false;
		bool offscreenSettings = false;
		bool lightSettings = false;
		bool audio = false;
		bool keyInputDebug = true;
	};

	void InitializeResources();
	void InitializeCameraAndObjects();
	void InitializeLighting();
	void ApplyLayout();
	void ReloadDebugData();
	void SaveLayout() const;
	void UpdateCurtain();
	void UpdateNavigation();
	void UpdateStartCharacterSelectionInput();
	void UpdatePermanentUpgradeInput();
	bool TryPurchasePermanentUpgrade(int32_t index);
	void UpdateAudio();
	void UpdateModelAnimation();
	void UpdateCameraAnimation();
	void QueueDebugDraw();
	void DrawDebugUI();
	void UpdateCoinDisplay();
	void UpdatePermanentUpgradeDisplay();
	void UpdateCharacterSelectionInput();
	bool TryActivateCharacter(int32_t index);
	void UpdateCharacterSelectionDisplay();
	void UpdateStartCharacterSelectionDisplay();
	void DrawCoinDisplay();
	void DrawPermanentUpgradeDisplay();
	void DrawCharacterSelectionDisplay();
	void DrawStartCharacterSelectionDisplay();
	void ApplyStartCharacterSelectionLayout();
	void UpdateStartCharacterModel();
	void UpdateStartCharacterPreviewModels();
	void UpdateShopLevelDisplay();
	void DrawShopDisplay();

	bool IsMouseMenuConfirm(int32_t hoveredMenuIndex) const;
	void StartGameTransition();

	std::shared_ptr<GameSession> sessionContext_;

	UILabel titleSprite_;
	UILabel cursorSprite_;
	UILabel shopSprite_;
	UILabel selectSprite_;
	BitmapText selectTitleText_;
	BitmapText selectActionText_;
	BitmapText selectedWeaponDescriptionText_;
	TextureHandle coinDigitTexture_ = 0;
	static constexpr int32_t kCoinDigitCount = 6;
	static constexpr int32_t kPermanentUpgradeCount = 5;
	static constexpr int32_t kShopMaxLevelSlots = 5;
	static constexpr int32_t kPermanentUpgradeCostDigitCount = 4;
	static constexpr int32_t kCharacterCount = 4;
	std::array<std::unique_ptr<Engine::Graphics2D::Sprite>, kCoinDigitCount> coinDigits_;
	std::array<UILabel, kPermanentUpgradeCount> permanentUpgradeIcons_;
	std::array<std::unique_ptr<Engine::Graphics2D::Sprite>, kPermanentUpgradeCount> permanentUpgradeLevelDigits_;
	std::array<
		std::array<std::unique_ptr<Engine::Graphics2D::Sprite>, kPermanentUpgradeCostDigitCount>,
		kPermanentUpgradeCount> permanentUpgradeCostDigits_;
	std::array<float, kPermanentUpgradeCount> permanentUpgradePurchaseFlashTimers_{};
	std::array<std::array<UIPanel, kShopMaxLevelSlots>, kPermanentUpgradeCount> shopLevelSquares_;
	std::array<UIPanel, kPermanentUpgradeCount> shopHighlights_;
	std::array<UILabel, kCharacterCount> characterIcons_;
	std::array<UILabel, 3> startCharacterIcons_;
	std::array<UIPanel, 3> startCharacterHighlights_;
	UILabel selectedCharacterFaceIcon_;
	UILabel selectedWeaponIcon_;
	UIPanel selectActionButton_;
	std::array<
		std::array<std::unique_ptr<Engine::Graphics2D::Sprite>, kPermanentUpgradeCostDigitCount>,
		kCharacterCount> characterCostDigits_;
	std::unique_ptr<CurtainTransition> curtain_;

	std::unique_ptr<Engine::CameraSystem::Camera> titleCamera_;
	std::unique_ptr<Engine::Graphics3D::Object3D> titleObject_;
	std::unique_ptr<Engine::Graphics3D::Object3D> startCharacterModel_;
	std::unique_ptr<Engine::Graphics3D::Object3D> startCharacterWeaponModel_;
	std::unique_ptr<Engine::Graphics3D::Object3D> startCharacterDetailModel_;
	std::unique_ptr<Engine::Graphics3D::Object3D> startCharacterDetailWeaponModel_;
	std::array<std::unique_ptr<Engine::Graphics3D::Object3D>, 3> startCharacterPreviewModels_;
	std::array<std::unique_ptr<Engine::Graphics3D::Object3D>, 3> startCharacterPreviewWeaponModels_;
	std::unique_ptr<Engine::Graphics3D::Object3D> skyDomeObject_;
	std::unique_ptr<GridPlane> gridPlane_;
	bool titleDebugDrawEnabled_ = true;

	SoundHandle titleBgmHandle_{};
	SoundHandle selectSeHandle_{};
	SoundHandle decideSeHandle_{};
	SoundHandle backSeHandle_{};

	int32_t menuIndex_ = 0;
	int32_t shopItemIndex_ = 0;
	int32_t characterItemIndex_ = 0;
	int32_t startCharacterSelectedIndex_ = 0;
	int32_t startCharacterAppliedSelectedIndex_ = -1;
	int32_t startCharacterInputDelayFrames_ = 0;
	bool startCharacterActionArmed_ = false;
	Vector2 cursorPosition_{};
	bool curtainStarted_ = false;
	bool curtainOpening_ = true;
	bool showingUpgradeScreen_ = false;
	bool awaitingCharacterSelect_ = false;
	bool shopCharacterSelectionActive_ = false;
	bool finished_ = false;
	float animationTime_ = 0.0f;
	DirectXGame::GameInputBindings::NavigationInputDevice navigationInputDevice_ =
		DirectXGame::GameInputBindings::NavigationInputDevice::Mouse;
	LayoutSettings layoutSettings_{};
	DebugWindowVisibility debugWindows_{};
};

}
