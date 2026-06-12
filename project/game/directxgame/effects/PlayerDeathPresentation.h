#pragma once

namespace DirectXGame {

class GameParticleEffects;
class Player;

class PlayerDeathPresentation final {
public:
	void Reset();
	void Start(Player& player, const GameParticleEffects& particleEffects);
	bool Update(Player& player, float deltaTime, bool skipRequested);

	float GetElapsedTime() const { return elapsedTime_; }
	float GetOverlayAlpha() const;

private:
	float elapsedTime_ = 0.0f;
	bool transitionRequested_ = false;
};

}
