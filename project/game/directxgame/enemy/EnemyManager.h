#pragma once

#include "Vector3.h"
#include "EnemyCollisionSystem.h"
#include "EnemyDeathBomb.h"
#include "BossInkProjectile.h"
#include "BossAttackVisual.h"
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
	// EnemyManager は敵生成、EXP ドロップ、敵側攻撃、ボス進行、衝突判定の統合窓口。
	// 個別の移動/反応/衝突計算は専用クラスへ委譲し、PlayScene からはここだけを呼ぶ。
	void Initialize(const std::string& enemyTypesPath, Player* player, PlayerManager* playerManager);
	void LoadEnemyTypes(const std::string& filePath);
	void LoadSpawnSettings(const std::string& filePath);
	void SetRandomSeed(uint32_t seed);
	void AppendCollisionTelemetryCsv(uint32_t frame) const;
	void SetSession(GameSession* session) { session_ = session; }
	void Update(float deltaTime);
	void Draw();
	void DrawShadow();

	// HUD/演出/デバッグが読む集計値。所有権は渡さず、コンテナ参照はフレーム内参照に限定する。
	const std::vector<std::unique_ptr<Enemy>>& GetEnemies() const { return enemies_; }
	const std::list<std::unique_ptr<ExpOrb>>& GetExpOrbs() const { return expOrbs_; }
	int32_t GetTotalKillCount() const { return totalKillCount_; }
	size_t GetActiveEnemyCount() const;
	size_t GetExpOrbCount() const { return expOrbs_.size(); }
	size_t GetPeakExpOrbCount() const { return peakExpOrbCount_; }
	const EnemyCollisionContext::Telemetry& GetCollisionTelemetry() const
	{
		return collisionContext_.telemetry;
	}
	EnemyBroadPhaseMode GetBroadPhaseMode() const
	{
		return collisionContext_.broadPhaseMode;
	}
	void SetBroadPhaseMode(EnemyBroadPhaseMode mode)
	{
		collisionContext_.broadPhaseMode = mode;
	}
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

	// プレイヤー武器や演出から呼ばれる敵検索/範囲ダメージ API。
	// ダメージ適用後の撃破処理と EXP ドロップは EnemyManager 側で一貫して処理する。
	void DamageAllEnemies(int32_t damage);
	void CheckCollisions(Player* player, PlayerManager* playerManager);
	bool FindNearestEnemyPosition(const Vector3& origin, float maxDistance, Vector3& outPosition) const;
	std::vector<Vector3> PickLightningTargets(int32_t count) const;
	void ApplyLightningDamage(const Vector3& center, float radius, int32_t damage);
	void ApplyAreaDamage(const Vector3& center, float radius, int32_t damage);
	void ApplyArcDamage(
		const Vector3& center,
		const Vector3& forward,
		float radius,
		float halfAngleRadians,
		int32_t damage,
		float knockStrength = 0.8f);
	void StartBossPhase();
	bool IsBossPhase() const { return bossPhase_; }
	bool IsBossDefeated() const { return bossDefeated_; }
	bool GetBossPresentationPosition(Vector3& outPosition) const;
	void SetBossPresentationPosition(const Vector3& position);
	void UpdateBossDeathPresentation(float elapsedTime, float duration);
	bool ConsumeBossPhaseChanged(Vector3& outPosition, int32_t& outPhase);

private:
	// 通常敵・EXP・死亡爆発の更新順序を固定する内部ステップ。
	void UpdateEnemies(float deltaTime);
	void RemoveInactiveEnemies();
	void UpdateExpOrbs(float deltaTime);
	void SpawnDeathDrop(const Enemy& enemy);
	void SpawnDeathBomb(const Enemy& enemy);
	void UpdateDeathBombs(float deltaTime);
	void ProcessBossAttackEvents();
	void UpdateBossInkProjectiles(float deltaTime);
	void SpawnBossInkWave(const Vector3& position, int32_t waveIndex);
	void DrawBossAttackTelegraph() const;
	void UpdateBossAttackVisuals(float deltaTime);

	// 敵とドロップの所有権。erase/remove 時に関連演出や統計もここで同期する。
	std::vector<std::unique_ptr<Enemy>> enemies_;
	std::list<std::unique_ptr<ExpOrb>> expOrbs_;
	std::vector<std::unique_ptr<EnemyDeathBomb>> deathBombs_;

	// ボス専用の弾・予兆・波状攻撃状態。
	std::vector<std::unique_ptr<BossInkProjectile>> bossInkProjectiles_;
	Vector3 bossInkWavePosition_{};
	int32_t pendingBossInkWaves_ = 0;
	int32_t bossInkNextWaveIndex_ = 0;
	float bossInkWaveTimer_ = 0.0f;
	BossRushTelegraph bossRushTelegraph_{};
	std::vector<std::unique_ptr<BossSlamCube>> bossSlamCubes_;
	EnemySpawnController spawnController_{};

	// 非所有参照。実体は PlayScene / PlayerManager / GameSession が持つ。
	Player* player_ = nullptr;
	PlayerManager* playerManager_ = nullptr;
	GameSession* session_ = nullptr;

	// 戦闘結果とソフトキャップ検証用の統計値。
	int32_t totalKillCount_ = 0;
	size_t peakExpOrbCount_ = 0;
	size_t expOrbPruneCount_ = 0;
	std::vector<Vector3> recentHitEffectPositions_;
	std::vector<Vector3> recentDeathEffectPositions_;
	std::vector<Vector3> recentExplosionEffectPositions_;
	std::vector<FloatingNumberEvent> recentFloatingNumberEvents_;
	EnemyCollisionContext collisionContext_;

	// ボス状態と乱数。bossEnemy_ は enemies_ 内要素への非所有参照。
	Enemy* bossEnemy_ = nullptr;
	bool bossPhase_ = false;
	bool bossDefeated_ = false;
	bool collisionTelemetryEnabled_ = false;
	mutable std::mt19937 randomEngine_{ std::random_device{}() };
};

} // namespace DirectXGame
