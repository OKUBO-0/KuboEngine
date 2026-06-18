#pragma once

#include <cstddef>
#include <memory>
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
	void Update(
		float deltaTime,
		Player* player,
		std::vector<std::unique_ptr<Enemy>>& enemies,
		bool bossPhase);
	void RelocateFarEnemies(
		Player* player,
		std::vector<std::unique_ptr<Enemy>>& enemies,
		const Enemy* bossEnemy,
		bool bossPhase) const;
	std::unique_ptr<Enemy> CreateBossEnemy(Player* player) const;

private:
	struct EnemyTypeData {
		int type = 0;
		int baseHP = 2;
		float baseSpeed = 0.16f;
		int baseEXP = 8;
		int spawnCount = 1;
	};

	static constexpr size_t kDefaultMaxActiveEnemies = 84;
	static constexpr float kDefaultSpawnUnlockInterval = 18.0f;
	static constexpr float kDefaultSpawnDistance = 50.0f;
	static constexpr float kDefaultRespawnDistance = 75.0f;
	static constexpr float kDefaultRespawnRadius = 60.0f;
	static constexpr float kDefaultMinSpawnInterval = 0.85f;
	static constexpr float kDefaultBaseSpawnInterval = 1.9f;
	static constexpr float kDefaultSpawnAcceleration = 0.0075f;

	void SpawnEnemies(
		Player& player,
		std::vector<std::unique_ptr<Enemy>>& enemies);
	void SpawnOneEnemy(
		const EnemyTypeData& data,
		Player& player,
		std::vector<std::unique_ptr<Enemy>>& enemies) const;
	static size_t CountActiveEnemies(
		const std::vector<std::unique_ptr<Enemy>>& enemies);

	std::vector<EnemyTypeData> enemyTypes_;
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
};

}
