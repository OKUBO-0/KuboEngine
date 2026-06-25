#pragma once

#include "BaseScene.h"
#include "Camera.h"
#include "Object3D.h"
#include "Sprite.h"
#include "Vector2.h"
#include "Vector3.h"
#include "game/directxgame/core/GameAudioCache.h"
#include "game/directxgame/core/GameTextureCache.h"
#include "game/directxgame/core/GameInputBindings.h"
#include "game/directxgame/effects/CurtainTransition.h"
#include "game/directxgame/ui/common/UILabel.h"
#include "game/directxgame/world/GridPlane.h"
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
	enum class GuideTransitionState {
		None,
		FadeIn,
		FadeOut,
	};

	struct LayoutSettings {
		Vector2 titlePosition{ 0.0f, 0.0f };
		Vector2 titleSize{ 1280.0f, 720.0f };
		Vector2 cursorBasePosition{ 0.0f, 0.0f };
		Vector2 cursorSize{ 1280.0f, 720.0f };
		float cursorStepY = 120.0f;
		Vector2 menuHitboxPosition{ 145.0f, 340.0f };
		Vector2 menuHitboxSize{ 300.0f, 88.0f };
		float menuHitboxStepY = 128.0f;
		Vector2 guidePosition{ 0.0f, 0.0f };
		Vector2 guideSize{ 1280.0f, 720.0f };
		Vector3 modelBasePosition{ -18.0f, -2.0f, 8.0f };
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
	void UpdateGuide();
	void UpdateNavigation();
	void UpdateAudio();
	void UpdateModelAnimation();
	void UpdateCameraAnimation();
	void QueueDebugDraw();
	void DrawDebugUI();
	void UpdateCoinDisplay();
	void DrawCoinDisplay();

	bool IsMouseMenuConfirm(int32_t hoveredMenuIndex) const;
	void StartGameTransition();
	void CloseGuide();

	std::shared_ptr<GameSession> sessionContext_;

	UILabel titleSprite_;
	UILabel cursorSprite_;
	UILabel guideSprite_;
	TextureHandle coinDigitTexture_ = 0;
	static constexpr int32_t kCoinDigitCount = 6;
	std::array<std::unique_ptr<Engine::Graphics2D::Sprite>, kCoinDigitCount> coinDigits_;
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
	bool guideActive_ = false;
	GuideTransitionState guideTransitionState_ = GuideTransitionState::None;
	float guideAlpha_ = 0.0f;
	float animationTime_ = 0.0f;
	DirectXGame::GameInputBindings::NavigationInputDevice navigationInputDevice_ =
		DirectXGame::GameInputBindings::NavigationInputDevice::Mouse;
	LayoutSettings layoutSettings_{};
	DebugWindowVisibility debugWindows_{};
};

}
