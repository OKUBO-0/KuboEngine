#pragma once

#include "Vector3.h"
#include "game/directxgame/core/GameLightSettings.h"
#include "game/directxgame/enemy/Enemy.h"
#include "game/directxgame/enemy/ExpOrb.h"
#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace DirectXGame {

class Player;
class PlayerManager;

class EnemyManager {
public:
	void Initialize(const std::string& enemyTypesPath, Player* player, PlayerManager* playerManager);
	void LoadEnemyTypes(const std::string& filePath);
	void LoadSpawnSettings(const std::string& filePath);
	void Update(float deltaTime);
	void Draw();
	void SetLightSettings(const GameLightSettings& lightSettings);

	const std::vector<std::unique_ptr<Enemy>>& GetEnemies() const { return enemies_; }
	const std::list<std::unique_ptr<ExpOrb>>& GetExpOrbs() const { return expOrbs_; }
	int32_t GetTotalKillCount() const { return totalKillCount_; }
	size_t GetActiveEnemyCount() const;
	size_t GetExpOrbCount() const { return expOrbs_.size(); }
	void DamageAllEnemies(int32_t damage);
	void CheckCollisions(Player* player, PlayerManager* playerManager);
	bool FindNearestEnemyPosition(const Vector3& origin, float maxDistance, Vector3& outPosition) const;
	std::vector<Vector3> PickLightningTargets(int32_t count) const;
	void ApplyLightningDamage(const Vector3& center, float radius, int32_t damage);

private:
	using EnemyCellMap = std::unordered_map<int64_t, std::vector<Enemy*>>;

	struct EnemyTypeData {
		int32_t type = 0;
		int32_t baseHP = 2;
		float baseSpeed = 0.16f;
		int32_t baseEXP = 8;
		int32_t spawnCount = 1;
	};

	static constexpr size_t kDefaultMaxActiveEnemies = 84;
	static constexpr float kDefaultSpawnUnlockInterval = 18.0f;
	static constexpr float kDefaultSpawnDistance = 50.0f;
	static constexpr float kDefaultRespawnDistance = 75.0f;
	static constexpr float kDefaultRespawnRadius = 60.0f;
	static constexpr float kDefaultMinSpawnInterval = 0.85f;
	static constexpr float kDefaultBaseSpawnInterval = 1.9f;
	static constexpr float kDefaultSpawnAcceleration = 0.0075f;
	static constexpr float kEnemySeparationDistance = 3.2f;
	static constexpr float kEnemySeparationStrength = 1.1f;
	static constexpr float kSpatialCellSize = 8.0f;
	static constexpr float kNormalBulletHitDistanceSq = 4.0f;
	static constexpr float kOrbitBulletHitDistanceSq = 25.0f;
	static constexpr float kPlayerContactDistance = 2.5f;
	static constexpr float kPlayerContactDistanceSq = kPlayerContactDistance * kPlayerContactDistance;

	void UpdateSpawnState(float deltaTime);
	void SpawnEnemies();
	void SpawnOneEnemy(const EnemyTypeData& data);
	void UpdateEnemies(float deltaTime);
	void RemoveInactiveEnemies();
	void RelocateFarEnemies();
	void UpdateExpOrbs(float deltaTime);
	void ResolveEnemySeparation();
	void SpawnDeathDrop(const Enemy& enemy);
	bool TryHandleBulletHit(Enemy& enemy, const Vector3& impactPosition, int32_t damage, float knockStrength);
	void CheckNormalBulletCollisions(PlayerManager& playerManager, const EnemyCellMap& spatialMap);
	void CheckOrbitBulletCollisions(PlayerManager& playerManager, const EnemyCellMap& spatialMap);
	void CheckDroneBulletCollisions(PlayerManager& playerManager, const EnemyCellMap& spatialMap);
	void CheckPlayerCollisions(Player& player, PlayerManager& playerManager, const EnemyCellMap& spatialMap);
	void BuildActiveEnemySpatialMap(EnemyCellMap& outMap, std::vector<Enemy*>& activeEnemies) const;
	void CollectNearbyEnemies(const EnemyCellMap& spatialMap, const Vector3& center, float radius, std::vector<Enemy*>& outEnemies) const;
	static int32_t ToCellCoord(float value);
	static int64_t MakeCellKey(int32_t cellX, int32_t cellZ);

	std::vector<std::unique_ptr<Enemy>> enemies_;
	std::list<std::unique_ptr<ExpOrb>> expOrbs_;
	std::vector<EnemyTypeData> enemyTypes_;
	Player* player_ = nullptr;
	PlayerManager* playerManager_ = nullptr;
	GameLightSettings lightSettings_{};

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
	int32_t totalKillCount_ = 0;
};

} // namespace DirectXGame
