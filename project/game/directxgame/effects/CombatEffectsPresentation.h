#pragma once

#include "Object3D.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace DirectXGame {

class EnemyManager;
class GameParticleEffects;
class Player;
class PlayerManager;

class CombatEffectsPresentation final {
public:
	void Initialize();
	void Reset(const PlayerManager& playerManager);
	bool Update(
		Player& player,
		PlayerManager& playerManager,
		EnemyManager* enemyManager,
		const GameParticleEffects& particleEffects);
	void Draw() const;

private:
	void UpdateLightningVisuals(const PlayerManager& playerManager);

	static constexpr size_t kLightningSegmentCount = 6;
	static constexpr size_t kLightningMaxTargets = 4;
	std::array<
		std::unique_ptr<Engine::Graphics3D::Object3D>,
		kLightningSegmentCount * kLightningMaxTargets> lightningObjects_;
	int32_t previousHp_ = 0;
	int32_t previousTotalExp_ = 0;
	float previousLightningTimer_ = 0.0f;
};

}
