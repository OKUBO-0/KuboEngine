#pragma once

#include "BaseScene.h"
#include "Camera.h"
#include "Object3D.h"
#include "Sprite.h"
#include "Vector2.h"
#include "Vector3.h"
#include "GameAudioCache.h"
#include "GameTextureCache.h"
#include "GameInputBindings.h"
#include "CurtainTransition.h"
#include "UILabel.h"
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
		float cursorStepY = 120.0f;
		Vector2 menuHitboxPosition{ 145.0f, 340.0f };
		Vector2 menuHitboxSize{ 300.0f, 88.0f };
		float menuHitboxStepY = 128.0f;
		Vector3 modelBasePosition{ -18.0f, 0.0f, 8.0f };
		Vector3 modelScale{ 4.5f, 4.5f, 4.5f };
		Vector3 cameraTarget{ 0.0f, 4.5f, 0.0f };
		float cameraDistance = 76.0f;
		float cameraHeight = 44.0f;
		float cameraPitch = 0.48f;
		float cameraYaw = 0.0f;
		float cameraOrbitSpeed = 0.12f;
		bool debugEnabled = false;
	};

	struct DebugWindowVisibility {
		bool windowSwitcher = false;
		bool titleView = true;
		bool statisticsView = true;
		bool titleSettings = true;
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
	void UpdatePermanentUpgradeInput();
	void UpdateAudio();
	void UpdateModelAnimation();
	void UpdateCameraAnimation();
	void QueueDebugDraw();
	void DrawDebugUI();
	void UpdateCoinDisplay();
	void UpdatePermanentUpgradeDisplay();
	void UpdateCharacterSelectionInput();
	void UpdateCharacterSelectionDisplay();
	void DrawCoinDisplay();
	void DrawPermanentUpgradeDisplay();
	void DrawCharacterSelectionDisplay();

	bool IsMouseMenuConfirm(int32_t hoveredMenuIndex) const;
	void StartGameTransition();

	std::shared_ptr<GameSession> sessionContext_;

	UILabel titleSprite_;
	UILabel cursorSprite_;
	TextureHandle coinDigitTexture_ = 0;
	static constexpr int32_t kCoinDigitCount = 6;
	static constexpr int32_t kPermanentUpgradeCount = 4;
	static constexpr int32_t kPermanentUpgradeCostDigitCount = 4;
	static constexpr int32_t kCharacterCount = 4;
	std::array<std::unique_ptr<Engine::Graphics2D::Sprite>, kCoinDigitCount> coinDigits_;
	std::array<UILabel, kPermanentUpgradeCount> permanentUpgradeIcons_;
	std::array<std::unique_ptr<Engine::Graphics2D::Sprite>, kPermanentUpgradeCount> permanentUpgradeLevelDigits_;
	std::array<
		std::array<std::unique_ptr<Engine::Graphics2D::Sprite>, kPermanentUpgradeCostDigitCount>,
		kPermanentUpgradeCount> permanentUpgradeCostDigits_;
	std::array<float, kPermanentUpgradeCount> permanentUpgradePurchaseFlashTimers_{};
	std::array<UILabel, kCharacterCount> characterIcons_;
	std::array<
		std::array<std::unique_ptr<Engine::Graphics2D::Sprite>, kPermanentUpgradeCostDigitCount>,
		kCharacterCount> characterCostDigits_;
	std::unique_ptr<CurtainTransition> curtain_;

	std::unique_ptr<Engine::CameraSystem::Camera> titleCamera_;
	std::unique_ptr<Engine::Graphics3D::Object3D> titleObject_;
	std::unique_ptr<Engine::Graphics3D::Object3D> skyDomeObject_;
	std::unique_ptr<GridPlane> gridPlane_;
	bool titleDebugDrawEnabled_ = true;

	SoundHandle titleBgmHandle_{};
	SoundHandle selectSeHandle_{};
	SoundHandle decideSeHandle_{};

	int32_t menuIndex_ = 0;
	bool curtainStarted_ = false;
	bool curtainOpening_ = true;
	bool finished_ = false;
	float animationTime_ = 0.0f;
	DirectXGame::GameInputBindings::NavigationInputDevice navigationInputDevice_ =
		DirectXGame::GameInputBindings::NavigationInputDevice::Mouse;
	LayoutSettings layoutSettings_{};
	DebugWindowVisibility debugWindows_{};
};

}
