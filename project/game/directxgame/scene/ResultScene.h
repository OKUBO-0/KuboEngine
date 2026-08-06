#pragma once

#include "BaseScene.h"
#include "Sprite.h"
#include "GameAudioCache.h"
#include "GameInputBindings.h"
#include "GameTextureCache.h"
#include "CurtainTransition.h"
#include "UILabel.h"
#include "BitmapText.h"
#include "GridPlane.h"
#include "SkyDome.h"
#include "Camera.h"
#include "UIPanel.h"
#include "Vector3.h"
#include <array>
#include <memory>
#include <string>

namespace DirectXGame {

class GameSession;
class ResultSceneDebugUIController;

class ResultScene : public Engine::Scene::BaseScene {
	friend class ResultSceneDebugUIController;

public:
	explicit ResultScene(std::shared_ptr<GameSession> sessionContext);

	void Initialize() override;
	void Finalize() override;
	void Update() override;
	void Draw() override;

private:
	void InitializeUi();
	void ApplyLayout();
	void ReloadDebugData();
	void SaveLayout() const;
	void UpdateCountUp(float deltaTime);
	void UpdateFinishUiPulse();
	void SetNumberText(
		BitmapText& text,
		const Vector2& basePosition,
		int32_t value,
		float scaleMultiplier = 1.0f,
		float alpha = 1.0f);
	bool IsCountUpFinished() const;
	void FinishCountUp();
	void RequestSceneChange(const char* sceneId);
	void UpdateCurtain(float deltaTime);
	void DrawDebugUI();
	void InitializeWorldBackground();
	void DrawWorldBackground();
	void InitializeResultPanels();
	void ApplyResultPanelLayout();
	void DrawResultPanels();

	struct DebugWindowVisibility {
		bool windowSwitcher = false;
		bool sceneView = true;
		bool statisticsView = true;
		bool sceneSettings = true;
		bool audio = false;
		bool keyInputDebug = true;
	};

	std::shared_ptr<GameSession> sessionContext_;
	UILabel background_;
	UILabel resultUi_;
	UILabel finishUi_;
	std::unique_ptr<Engine::CameraSystem::Camera> resultCamera_;
	std::unique_ptr<GridPlane> gridPlane_;
	std::unique_ptr<SkyDome> skyDome_;
	std::unique_ptr<CurtainTransition> curtain_;
	SoundHandle finishSeHandle_{};
	SoundHandle decideSeHandle_{};
	UIPanel dimPanel_;
	UIPanel resultPanel_;
	std::array<UIPanel, 4> resultPanelBorders_;
	std::array<UIPanel, 5> resultRowPanels_;
	std::array<UIPanel, 20> resultRowBorders_;
	BitmapText titleText_;
	BitmapText expLabelText_;
	BitmapText levelLabelText_;
	BitmapText killLabelText_;
	BitmapText coinLabelText_;
	BitmapText totalScoreLabelText_;
	BitmapText promptText_;
	BitmapText expText_;
	BitmapText levelText_;
	BitmapText killText_;
	BitmapText coinText_;
	BitmapText totalScoreText_;
	Vector2 backgroundPosition_{ 0.0f, 0.0f };
	Vector2 backgroundSize_{ 1280.0f, 720.0f };
	Vector2 resultPosition_{ 0.0f, 0.0f };
	Vector2 resultSize_{ 1280.0f, 720.0f };
	Vector2 finishPosition_{ 0.0f, 0.0f };
	Vector2 finishSize_{ 1280.0f, 720.0f };
	Vector2 expPosition_{ 500.0f, 195.0f };
	Vector2 levelPosition_{ 500.0f, 290.0f };
	Vector2 killPosition_{ 500.0f, 395.0f };
	Vector2 coinPosition_{ 500.0f, 465.0f };
	Vector2 totalScorePosition_{ 660.0f, 560.0f };
	Vector2 digitSize_{ 24.0f, 32.0f };
	Vector2 panelPosition_{ 360.0f, 84.0f };
	Vector2 panelSize_{ 560.0f, 560.0f };
	float scoreScale_ = 1.5f;
	float displayedExp_ = 0.0f;
	float displayedLevel_ = 0.0f;
	float displayedKills_ = 0.0f;
	float displayedCoins_ = 0.0f;
	float displayedTotalScore_ = 0.0f;
	float resultAnimationTime_ = 0.0f;
	std::string pendingSceneId_;
	GameInputBindings::NavigationInputDevice navigationInputDevice_ =
		GameInputBindings::NavigationInputDevice::Keyboard;
	bool layoutDebugEnabled_ = false;
	bool countUpFinished_ = false;
	bool finishSePlayed_ = false;
	DebugWindowVisibility debugWindows_{};
};

}
