#pragma once

#include "Vector3.h"
#include "EnemyCollisionSystem.h"
#include "EnemyDeathBomb.h"
#include "Enemy.h"
#include "EnemySpawnController.h"
#include "ExpOrb.h"
#include <cstdint>
#include <list>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace DirectXGame {

class Player;
class PlayerManager;
class GameSession;

class EnemyManager {
public:
	void Initialize(const std::string& enemyTypesPath, Player* player, PlayerManager* playerManager);
	void LoadEnemyTypes(const std::string& filePath);
	void LoadSpawnSettings(const std::string& filePath);
	void SetRandomSeed(uint32_t seed);
	void SetSession(GameSession* session) { session_ = session; }
	void Update(float deltaTime);
	void Draw();
	void DrawShadow();

	const std::vector<std::unique_ptr<Enemy>>& GetEnemies() const { return enemies_; }
	const std::list<std::unique_ptr<ExpOrb>>& GetExpOrbs() const { return expOrbs_; }
	int32_t GetTotalKillCount() const { return totalKillCount_; }
	size_t GetActiveEnemyCount() const;
	size_t GetExpOrbCount() const { return expOrbs_.size(); }
	size_t GetPeakExpOrbCount() const { return peakExpOrbCount_; }
	size_t GetExpOrbPruneCount() const { return expOrbPruneCount_; }
	void ResetExpOrbTelemetry()
	{
		peakExpOrbCount_ = expOrbs_.size();
		expOrbPruneCount_ = 0;
	}
	static constexpr size_t kMaxExpOrbs = 160;
	const std::vector<Vector3>& GetRecentHitEffectPositions() const { return recentHitEffectPositions_; }
	const std::vector<Vector3>& GetRecentDeathEffectPositions() const { return recentDeathEffectPositions_; }
	const std::vector<Vector3>& GetRecentExplosionEffectPositions() const { return recentExplosionEffectPositions_; }
	const std::vector<FloatingNumberEvent>& GetRecentFloatingNumberEvents() const { return recentFloatingNumberEvents_; }
	void ClearRecentEffectPositions();
	void DamageAllEnemies(int32_t damage);
	void CheckCollisions(Player* player, PlayerManager* playerManager);
	bool FindNearestEnemyPosition(const Vector3& origin, float maxDistance, Vector3& outPosition) const;
	std::vector<Vector3> PickLightningTargets(int32_t count) const;
	void ApplyLightningDamage(const Vector3& center, float radius, int32_t damage);
	void StartBossPhase();
	bool IsBossPhase() const { return bossPhase_; }
	bool IsBossDefeated() const { return bossDefeated_; }
	bool GetBossPresentationPosition(Vector3& outPosition) const;
	void SetBossPresentationPosition(const Vector3& position);
	void UpdateBossDeathPresentation(float elapsedTime, float duration);
	bool ConsumeBossPhaseChanged(Vector3& outPosition, int32_t& outPhase);

private:
	void UpdateEnemies(float deltaTime);
	void RemoveInactiveEnemies();
	void UpdateExpOrbs(float deltaTime);
	void SpawnDeathDrop(const Enemy& enemy);
	void SpawnDeathBomb(const Enemy& enemy);
	void UpdateDeathBombs(float deltaTime);

	std::vector<std::unique_ptr<Enemy>> enemies_;
	std::list<std::unique_ptr<ExpOrb>> expOrbs_;
	std::vector<std::unique_ptr<EnemyDeathBomb>> deathBombs_;
	EnemySpawnController spawnController_{};
	Player* player_ = nullptr;
	PlayerManager* playerManager_ = nullptr;
	GameSession* session_ = nullptr;

	int32_t totalKillCount_ = 0;
	size_t peakExpOrbCount_ = 0;
	size_t expOrbPruneCount_ = 0;
	std::vector<Vector3> recentHitEffectPositions_;
	std::vector<Vector3> recentDeathEffectPositions_;
	std::vector<Vector3> recentExplosionEffectPositions_;
	std::vector<FloatingNumberEvent> recentFloatingNumberEvents_;
	EnemyCollisionContext collisionContext_;
	Enemy* bossEnemy_ = nullptr;
	bool bossPhase_ = false;
	bool bossDefeated_ = false;
	mutable std::mt19937 randomEngine_{ std::random_device{}() };
};

} // namespace DirectXGame
