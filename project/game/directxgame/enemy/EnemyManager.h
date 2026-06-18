#pragma once

#include "Vector3.h"
#include "game/directxgame/enemy/EnemyCollisionSystem.h"
#include "game/directxgame/enemy/Enemy.h"
#include "game/directxgame/enemy/EnemySpawnController.h"
#include "game/directxgame/enemy/ExpOrb.h"
#include <cstdint>
#include <list>
#include <memory>
#include <string>
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
	void UpdateBossDeathPresentation(float elapsedTime, float duration);
	bool ConsumeBossPhaseChanged(Vector3& outPosition, int32_t& outPhase);

private:
	void UpdateEnemies(float deltaTime);
	void RemoveInactiveEnemies();
	void UpdateExpOrbs(float deltaTime);
	void SpawnDeathDrop(const Enemy& enemy);

	std::vector<std::unique_ptr<Enemy>> enemies_;
	std::list<std::unique_ptr<ExpOrb>> expOrbs_;
	EnemySpawnController spawnController_{};
	Player* player_ = nullptr;
	PlayerManager* playerManager_ = nullptr;

	int32_t totalKillCount_ = 0;
	size_t peakExpOrbCount_ = 0;
	size_t expOrbPruneCount_ = 0;
	std::vector<Vector3> recentHitEffectPositions_;
	std::vector<Vector3> recentDeathEffectPositions_;
	EnemyCollisionContext collisionContext_;
	Enemy* bossEnemy_ = nullptr;
	bool bossPhase_ = false;
	bool bossDefeated_ = false;
};

} // namespace DirectXGame
