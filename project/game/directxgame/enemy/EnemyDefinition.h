#pragma once

#include <cstdint>

namespace DirectXGame {

enum class EnemyType : int32_t {
	Standard = 1,
	Tackler = 2,
	Bomb = 3,
	Fast = 4,
	Heavy = 5,
	Gold = 6,
	Boss = 100,
};

enum class EnemyBehaviorType : int32_t {
	Chase = 0,
	Tackle = 1,
	Boss = 100,
};

struct EnemyDefinition {
	EnemyType type = EnemyType::Standard;
	int32_t baseHP = 2;
	float baseSpeed = 0.16f;
	int32_t baseEXP = 8;
	int32_t spawnCount = 1;
	int32_t coinReward = 0;
	int32_t damage = 10;
	float knockbackResistance = 0.0f;
	EnemyBehaviorType behavior = EnemyBehaviorType::Chase;
	float deathBombDelay = 0.0f;
	float deathBombRadius = 0.0f;
	int32_t deathBombDamage = 0;
};

} // namespace DirectXGame
