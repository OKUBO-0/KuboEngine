#pragma once

#include "EnemyDefinition.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace DirectXGame {

class Enemy;
class Player;

class EnemySpawnController final {
public:
	void Initialize(
		const std::string& enemyTypesPath,
		const std::string& spawnSettingsPath);
	void LoadEnemyTypes(const std::string& filePath);
	void LoadSpawnSettings(const std::string& filePath);
	void SetRandomSeed(uint32_t seed) { randomEngine_.seed(seed); }
	void SpawnBenchmarkEnemies(
		Player* player,
		std::vector<std::unique_ptr<Enemy>>& enemies,
		size_t targetCount);
	void Update(
		float deltaTime,
		Player* player,
		std::vector<std::unique_ptr<Enemy>>& enemies,
		bool bossPhase);
	void RelocateFarEnemies(
		Player* player,
		std::vector<std::unique_ptr<Enemy>>& enemies,
		const Enemy* bossEnemy,
		bool bossPhase);
	std::unique_ptr<Enemy> CreateBossEnemy(
		Player* player,
		int32_t playerLevel) const;

private:
	static constexpr size_t kDefaultMaxActiveEnemies = 96;
	static constexpr float kDefaultSpawnUnlockInterval = 10.8f;
	static constexpr float kDefaultSpawnDistance = 50.0f;
	static constexpr float kDefaultRespawnDistance = 75.0f;
	static constexpr float kDefaultRespawnRadius = 60.0f;
	static constexpr float kDefaultMinSpawnInterval = 0.75f;
	static constexpr float kDefaultBaseSpawnInterval = 1.75f;
	static constexpr float kDefaultSpawnAcceleration = 0.0085f;

	void SpawnEnemies(
		Player& player,
		std::vector<std::unique_ptr<Enemy>>& enemies);
	void SpawnOneEnemy(
		const EnemyDefinition& data,
		Player& player,
		std::vector<std::unique_ptr<Enemy>>& enemies);
	float RandomAngle();
	static size_t CountActiveEnemies(
		const std::vector<std::unique_ptr<Enemy>>& enemies);

	std::vector<EnemyDefinition> enemyTypes_;
	float elapsedTime_ = 0.0f;
	float spawnTimer_ = 0.0f;
	float spawnInterval_ = kDefaultBaseSpawnInterval;
	size_t maxActiveEnemies_ = kDefaultMaxActiveEnemies;
	float spawnUnlockInterval_ = kDefaultSpawnUnlockInterval;
	float spawnDistance_ = kDefaultSpawnDistance;
	float respawnDistance_ = kDefaultRespawnDistance;
	float respawnRadius_ = kDefaultRespawnRadius;
	float minSpawnInterval_ = kDefaultMinSpawnInterval;
	float baseSpawnInterval_ = kDefaultBaseSpawnInterval;
	float spawnAcceleration_ = kDefaultSpawnAcceleration;
	std::mt19937 randomEngine_{ std::random_device{}() };
};

}
