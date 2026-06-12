#pragma once

#include "game/directxgame/core/GameInputBindings.h"
#include "game/directxgame/core/LevelUpChoiceService.h"
#include "game/directxgame/ui/common/UILabel.h"
#include <array>
#include <cstdint>
#include <vector>

namespace DirectXGame {

class GameParticleEffects;
class Player;
class PlayerManager;

class LevelUpSelectionHud final {
public:
	void Initialize();
	void Start(PlayerManager& playerManager);
	bool Update(
		PlayerManager& playerManager,
		float deltaTime,
		float animationTime,
		int32_t moveDelta,
		bool confirmTriggered,
		GameInputBindings::NavigationInputDevice inputDevice);
	void Draw();
	void SpawnConfetti(
		const Player& player,
		const GameParticleEffects& particleEffects) const;

private:
	enum class AnimationState {
		Hidden,
		Entering,
		Idle,
		Exiting,
	};

	void BuildChoices(const PlayerManager& playerManager);
	void ApplyLayout();
	void MoveSelection(int32_t delta);
	int32_t GetHoveredChoiceIndex() const;

	static constexpr size_t kChoiceCount = 3;
	UILabel overlay_;
	std::array<UILabel, kChoiceCount> choiceSprites_;
	std::array<UILabel, kChoiceCount> choiceIcons_;
	std::vector<LevelUpChoice> choices_;
	Vector2 choiceSize_{ 1280.0f, 720.0f };
	Vector2 choiceHitboxOffset_{ 465.0f, 214.0f };
	Vector2 choiceHitboxSize_{ 435.0f, 68.0f };
	float choiceStepY_ = 140.0f;
	float slideOffsetX_ = 1280.0f;
	int32_t selection_ = 0;
	AnimationState animationState_ = AnimationState::Hidden;
	bool selectionPending_ = false;
};

}
