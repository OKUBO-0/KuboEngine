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

/// @brief 敵、ボス、EXP ドロップ、敵側攻撃、衝突判定を統合する管理クラス
/// @details PlayScene と PlayerManager から呼ばれる敵側の窓口。
///          敵コンテナを所有し、撃破時の演出イベントやドロップ生成もここで同期する。
class EnemyManager {
public:
	struct AnimationLodStats {
		uint32_t stride1Count = 0;
		uint32_t stride2Count = 0;
		uint32_t stride3Count = 0;
		uint32_t stride4Count = 0;
	};
	struct RenderLodStats {
		uint32_t fullModelCount = 0;
		uint32_t simplifiedModelCount = 0;
	};

	/// @brief 敵管理を初期化し、敵種別と参照先を設定する
	/// @param enemyTypesPath 敵種別定義 CSV パス
	/// @param player 追跡対象の Player。所有権は受け取らない
	/// @param playerManager ダメージや成長状態参照に使う PlayerManager
	void Initialize(const std::string& enemyTypesPath, Player* player, PlayerManager* playerManager);

	/// @brief 敵種別定義を CSV から読み込む
	/// @param filePath 敵種別定義 CSV パス
	void LoadEnemyTypes(const std::string& filePath);

	/// @brief 敵スポーン設定を CSV から読み込む
	/// @param filePath スポーン設定 CSV パス
	void LoadSpawnSettings(const std::string& filePath);

	/// @brief 敵生成やドロップに使う乱数シードを固定する
	/// @param seed 設定する乱数シード
	void SetRandomSeed(uint32_t seed);

	/// @brief 衝突判定テレメトリを CSV へ追記する
	/// @param frame 出力対象フレーム番号
	void AppendCollisionTelemetryCsv(uint32_t frame) const;

	/// @brief ラン結果やボス状態を同期する GameSession を設定する
	/// @param session PlayScene が保持するセッション。所有権は受け取らない
	void SetSession(GameSession* session) { session_ = session; }

	/// @brief 敵、EXP、ボス攻撃、死亡爆発を 1 フレーム更新する
	/// @param deltaTime 前フレームからの経過秒
	void Update(float deltaTime);

	/// @brief 敵、EXP、敵側攻撃を描画する
	void Draw();

	/// @brief シャドウマップ用に敵を描画する
	void DrawShadow();

	/// @brief 現在管理している敵配列を取得する
	/// @return EnemyManager が所有する敵配列。参照はフレーム内利用に限定する
	const std::vector<std::unique_ptr<Enemy>>& GetEnemies() const { return enemies_; }

	/// @brief 現在管理している EXP オーブ配列を取得する
	/// @return EnemyManager が所有する EXP オーブ配列
	const std::list<std::unique_ptr<ExpOrb>>& GetExpOrbs() const { return expOrbs_; }

	/// @brief ラン中の累計撃破数を取得する
	int32_t GetTotalKillCount() const { return totalKillCount_; }

	/// @brief アクティブな敵数を取得する
	/// @return 生存中かつ描画/判定対象の敵数
	size_t GetActiveEnemyCount() const;
	const AnimationLodStats& GetAnimationLodStats() const
	{
		return animationLodStats_;
	}
	const RenderLodStats& GetRenderLodStats() const
	{
		return renderLodStats_;
	}
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

	/// @brief 全敵へ一括ダメージを与える
	/// @param damage 適用するダメージ量
	void DamageAllEnemies(int32_t damage);

	/// @brief プレイヤー、敵、EXP オーブ、敵攻撃の衝突を処理する
	/// @param player 判定対象の Player
	/// @param playerManager HP/成長/被弾状態を更新する PlayerManager
	void CheckCollisions(Player* player, PlayerManager* playerManager);

	/// @brief 指定位置から最も近い敵位置を探す
	/// @param origin 探索中心
	/// @param maxDistance 探索最大距離
	/// @param outPosition 見つかった敵位置の出力先
	/// @return 対象が見つかった場合 true
	bool FindNearestEnemyPosition(const Vector3& origin, float maxDistance, Vector3& outPosition) const;

	/// @brief 雷撃対象として使う敵位置を抽出する
	/// @param count 最大抽出数
	/// @return 攻撃対象位置の配列
	std::vector<Vector3> PickLightningTargets(int32_t count) const;

	/// @brief 雷撃チェイン用に近い敵を順番に抽出する
	/// @param origin 初撃の探索中心
	/// @param count 最大チェイン数
	/// @param chainRange 2撃目以降が届く距離
	/// @return 攻撃対象位置の配列
	std::vector<Vector3> PickLightningChainTargets(
		const Vector3& origin,
		int32_t count,
		float chainRange) const;

	/// @brief 指定中心の雷撃範囲ダメージを適用する
	/// @param center 攻撃中心
	/// @param radius 攻撃半径
	/// @param damage ダメージ量
	void ApplyLightningDamage(const Vector3& center, float radius, int32_t damage);

	/// @brief 円形範囲ダメージを適用する
	/// @param center 攻撃中心
	/// @param radius 攻撃半径
	/// @param damage ダメージ量
	void ApplyAreaDamage(const Vector3& center, float radius, int32_t damage);

	/// @brief 前方扇形範囲ダメージを適用する
	/// @param center 攻撃中心
	/// @param forward 扇形の前方方向
	/// @param radius 攻撃半径
	/// @param halfAngleRadians 扇形の半角
	/// @param damage ダメージ量
	/// @param knockStrength ノックバック強度
	void ApplyArcDamage(
		const Vector3& center,
		const Vector3& forward,
		float radius,
		float halfAngleRadians,
		int32_t damage,
		float knockStrength = 0.8f);

	/// @brief ボスフェーズを開始し、通常スポーンからボス管理へ切り替える
	void StartBossPhase();

	/// @brief 現在ボスフェーズ中かを取得する
	bool IsBossPhase() const { return bossPhase_; }

	/// @brief ボス撃破済みかを取得する
	bool IsBossDefeated() const { return bossDefeated_; }

	/// @brief 生存中のボスが存在するかを取得する
	bool HasActiveBoss() const;

	/// @brief ボス現在 HP を取得する
	/// @return ボス不在なら 0
	int32_t GetBossHP() const;

	/// @brief ボス最大 HP を取得する
	/// @return ボス不在なら 0
	int32_t GetBossMaxHP() const;

	/// @brief ボス演出用の表示位置を取得する
	/// @param outPosition 表示位置の出力先
	/// @return 位置を取得できた場合 true
	bool GetBossPresentationPosition(Vector3& outPosition) const;

	/// @brief ボス演出用の表示位置を上書きする
	/// @param position 設定する表示位置
	void SetBossPresentationPosition(const Vector3& position);
	void ClearBossPresentationPositionOverride();

	/// @brief ボス死亡演出中の状態を更新する
	/// @param elapsedTime 演出開始からの経過秒
	/// @param duration 演出全体の秒数
	void UpdateBossDeathPresentation(float elapsedTime, float duration);

	/// @brief ボスフェーズ変更イベントを 1 回だけ消費する
	/// @param outPosition イベント発生位置
	/// @param outPhase 変更後フェーズ番号
	/// @return 未消費イベントがあった場合 true
	bool ConsumeBossPhaseChanged(Vector3& outPosition, int32_t& outPhase);

	void ReloadBossAttackTuning();
#ifdef _DEBUG
	void SaveBossAttackTuning() const;
	void QueueDebugBossAttack(BossAttackType type);
	void DrawBossAttackTuningDebugUI();
#endif

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
	void SpawnBossSummonProjectileBurst(const Vector3& position, int32_t burstIndex);
	void EnsureBossSummonCrystals(const Vector3& targetPosition);
	void DrawBossAttackTelegraph() const;
	void UpdateBossAttackVisuals(float deltaTime);

	struct ActiveBossShockwaveDamage {
		Vector3 center{};
		float radius = 0.0f;
		float delay = 0.0f;
		float duration = 0.0f;
		float elapsedTime = 0.0f;
		int32_t damage = 0;
		bool inward = false;
		bool hitPlayer = false;
	};

	struct ActiveBossBeamExplosionDamage {
		Vector3 center{};
		float radius = 0.0f;
		float delay = 0.0f;
		float elapsedTime = 0.0f;
		int32_t damage = 0;
		bool resolved = false;
	};

	struct PendingBossSummonProjectileBurst {
		Vector3 center{};
		float delay = 0.0f;
		float elapsedTime = 0.0f;
		int32_t burstIndex = 0;
	};

	struct BossAttackTuning {
		float beamExtraLength = 28.0f;
		float beamMinLength = 62.0f;
		float beamWidth = 3.1f;
		float tripleBeamAngleOffset = 0.52f;
		float beamExplosionSpacing = 7.0f;
		float beamExplosionInterval = 0.08f;
		float beamExplosionRadius = 3.2f;
		float beamExplosionStartDelay = 0.22f;
		float beamJumpSafeHeight = 1.15f;
		float shockwaveJumpSafeHeight = 0.65f;
		float shockwaveRadius = 135.0f;
		float shockwaveDuration = 3.8f;
		float summonShockwaveRadius = 155.0f;
		float summonSubRadius = 120.0f;
		float summonSubDelay = 0.45f;
		float summonSubDamageScale = 0.66f;
		float bulletHellProjectileSpeed = 12.8f;
		float bulletHellProjectileLifetime = 6.8f;
		float bulletHellProjectileRadius = 0.88f;
		int32_t bulletHellProjectileCount = 32;
		int32_t bulletHellExtraWaveCount = 4;
		float bulletHellFirstDelay = 0.96f;
		float bulletHellWaveInterval = 0.96f;
		float bulletHellAngleStepDivisor = 64.0f;
		int32_t convergingProjectileCount = 24;
		float convergingSpawnRadius = 23.5f;
		float convergingRingRadius = 58.0f;
		float domeBurstRadius = 58.0f;
		float domeBurstDamageScale = 1.5f;
	};

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
	BossAreaTelegraph bossAreaTelegraph_{};
	std::vector<std::unique_ptr<BossBeamBurst>> bossBeamBursts_;
	std::vector<std::unique_ptr<BossBeamExplosionChain>> bossBeamExplosionChains_;
	std::vector<std::unique_ptr<BossShockwaveRing>> bossShockwaveRings_;
	std::vector<std::unique_ptr<BossDomeBurstVisual>> bossDomeBurstVisuals_;
	std::vector<std::unique_ptr<BossSummonCrystal>> bossSummonCrystals_;
	std::vector<std::unique_ptr<BossSlamCube>> bossSlamCubes_;
	std::vector<PendingBossSummonProjectileBurst> pendingBossSummonProjectileBursts_;
	std::vector<ActiveBossBeamExplosionDamage> bossBeamExplosionDamages_;
	std::vector<ActiveBossShockwaveDamage> bossShockwaveDamageWaves_;
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
	AnimationLodStats animationLodStats_{};
	RenderLodStats renderLodStats_{};
	EnemyCollisionContext collisionContext_;
	BossAttackTuning bossAttackTuning_{};

	// ボス状態と乱数。bossEnemy_ は enemies_ 内要素への非所有参照。
	Enemy* bossEnemy_ = nullptr;
	bool bossPhase_ = false;
	bool bossDefeated_ = false;
	bool bossPresentationPositionOverrideActive_ = false;
	Vector3 bossPresentationPositionOverride_{};
	bool collisionTelemetryEnabled_ = false;
	mutable std::mt19937 randomEngine_{ std::random_device{}() };
};

} // namespace DirectXGame
