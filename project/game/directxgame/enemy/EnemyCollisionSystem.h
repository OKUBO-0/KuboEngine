#pragma once

#include "Vector3.h"
#include "FloatingNumberEvent.h"
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace DirectXGame {

class Enemy;
class Player;
class PlayerManager;

struct EnemyCollisionContext {
	using EnemyCellMap = std::unordered_map<uint64_t, std::vector<Enemy*>>;

	EnemyCellMap spatialMap;
	std::vector<Enemy*> activeEnemies;
	std::vector<Enemy*> nearbyEnemies;
};

class EnemyCollisionSystem final {
public:
	static void RebuildContext(
		std::vector<std::unique_ptr<Enemy>>& enemies,
		EnemyCollisionContext& context);
	static void CheckCollisions(
		Player& player,
		PlayerManager& playerManager,
		std::vector<std::unique_ptr<Enemy>>& enemies,
		std::vector<Vector3>& hitEffectPositions,
		std::vector<Vector3>& deathEffectPositions,
		std::vector<FloatingNumberEvent>& numberEvents);
	static void CheckCollisions(
		Player& player,
		PlayerManager& playerManager,
		EnemyCollisionContext& context,
		std::vector<Vector3>& hitEffectPositions,
		std::vector<Vector3>& deathEffectPositions,
		std::vector<FloatingNumberEvent>& numberEvents);
	static void ApplyAreaDamage(
		const Vector3& center,
		float radius,
		int32_t damage,
		std::vector<std::unique_ptr<Enemy>>& enemies,
		std::vector<Vector3>& hitEffectPositions,
		std::vector<FloatingNumberEvent>& numberEvents,
		PlayerManager* damageOwner = nullptr);
	static void ApplyArcDamage(
		const Vector3& center,
		const Vector3& forward,
		float radius,
		float halfAngleRadians,
		int32_t damage,
		std::vector<std::unique_ptr<Enemy>>& enemies,
		std::vector<Vector3>& hitEffectPositions,
		std::vector<FloatingNumberEvent>& numberEvents,
		PlayerManager* damageOwner = nullptr);
	static void ResolveEnemySeparation(
		std::vector<std::unique_ptr<Enemy>>& enemies);
	static void ResolveEnemySeparation(
		EnemyCollisionContext& context);
};

}
