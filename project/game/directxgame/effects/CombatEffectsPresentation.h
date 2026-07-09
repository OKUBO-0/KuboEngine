#pragma once

#include "Object3D.h"
#include "Vector3.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>
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
	struct SwordSlashVisual {
		std::array<
			std::unique_ptr<Engine::Graphics3D::Object3D>,
			9> segments;
		Vector3 center{};
		Vector3 forward{ 0.0f, 0.0f, 1.0f };
		float radius = 0.0f;
		float lifetime = 0.0f;
		float age = 0.0f;
		int32_t directionSign = 1;
	};

	void SpawnSwordSlashVisual(
		const Vector3& center,
		const Vector3& forward,
		float radius,
		int32_t directionSign);
	void UpdateSwordSlashVisuals(float deltaTime);
	void UpdateLightningVisuals(const PlayerManager& playerManager);

	static constexpr size_t kLightningSegmentCount = 6;
	static constexpr size_t kLightningMaxTargets = 4;
	static constexpr size_t kMaxSwordSlashVisuals = 12;
	static constexpr size_t kSwordSlashSegmentCount = 9;
	std::array<
		std::unique_ptr<Engine::Graphics3D::Object3D>,
		kLightningSegmentCount * kLightningMaxTargets> lightningObjects_;
	std::vector<SwordSlashVisual> swordSlashVisuals_;
	int32_t previousHp_ = 0;
	int32_t previousTotalExp_ = 0;
	float previousLightningTimer_ = 0.0f;
};

}
