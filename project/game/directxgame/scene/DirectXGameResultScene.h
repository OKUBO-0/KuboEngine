#pragma once

#include "BaseScene.h"
#include "Sprite.h"
#include "game/directxgame/core/GameAudioCache.h"
#include "game/directxgame/core/GameInputBindings.h"
#include "game/directxgame/core/GameTextureCache.h"
#include "game/directxgame/effects/CurtainTransition.h"
#include "game/directxgame/ui/common/UILabel.h"
#include <array>
#include <memory>
#include <string>

namespace DirectXGame {

class DirectXGameSessionContext;

class DirectXGameResultScene : public Engine::Scene::BaseScene {
public:
	explicit DirectXGameResultScene(std::shared_ptr<DirectXGameSessionContext> sessionContext);

	void Initialize() override;
	void Finalize() override;
	void Update() override;
	void Draw() override;

private:
	void InitializeUi();
	void ApplyLayout();
	void SaveLayout() const;
	void UpdateCountUp(float deltaTime);
	void DrawNumber(const std::array<std::unique_ptr<Engine::Graphics2D::Sprite>, 6>& sprites);
	void SetNumberSprites(std::array<std::unique_ptr<Engine::Graphics2D::Sprite>, 6>& sprites, const Vector2& basePosition, int32_t value);
	bool IsCountUpFinished() const;
	void FinishCountUp();
	void RequestSceneChange(const char* sceneId);
	void UpdateCurtain(float deltaTime);

	std::shared_ptr<DirectXGameSessionContext> sessionContext_;
	UILabel background_;
	UILabel resultUi_;
	UILabel finishUi_;
	std::unique_ptr<CurtainTransition> curtain_;
	TextureHandle numberTexture_ = 0;
	SoundHandle finishSeHandle_ = 0;
	std::array<std::unique_ptr<Engine::Graphics2D::Sprite>, 6> expDigits_;
	std::array<std::unique_ptr<Engine::Graphics2D::Sprite>, 6> levelDigits_;
	std::array<std::unique_ptr<Engine::Graphics2D::Sprite>, 6> killDigits_;
	std::array<std::unique_ptr<Engine::Graphics2D::Sprite>, 6> totalScoreDigits_;
	Vector2 backgroundPosition_{ 0.0f, 0.0f };
	Vector2 backgroundSize_{ 1280.0f, 720.0f };
	Vector2 resultPosition_{ 0.0f, 0.0f };
	Vector2 resultSize_{ 1280.0f, 720.0f };
	Vector2 finishPosition_{ 0.0f, 0.0f };
	Vector2 finishSize_{ 1280.0f, 720.0f };
	Vector2 expPosition_{ 500.0f, 195.0f };
	Vector2 levelPosition_{ 500.0f, 290.0f };
	Vector2 killPosition_{ 500.0f, 395.0f };
	Vector2 totalScorePosition_{ 660.0f, 560.0f };
	Vector2 digitSize_{ 24.0f, 32.0f };
	float scoreScale_ = 1.5f;
	float displayedExp_ = 0.0f;
	float displayedLevel_ = 0.0f;
	float displayedKills_ = 0.0f;
	float displayedTotalScore_ = 0.0f;
	std::string pendingSceneId_;
	GameInputBindings::NavigationInputDevice navigationInputDevice_ =
		GameInputBindings::NavigationInputDevice::Keyboard;
	bool layoutDebugEnabled_ = false;
	bool countUpFinished_ = false;
	bool finishSePlayed_ = false;
};

}
