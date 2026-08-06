#pragma once

#include "Vector3.h"
#include "Player.h"
#include "PlayerProgression.h"
#include "PlayerWeaponController.h"
#include "PassiveItemData.h"
#include "UILayoutIO.h"
#include <array>
#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace DirectXGame {

enum class CharacterId : int32_t;
class EnemyManager;

/// @brief プレイヤーの成長、HP、武器、パッシブ取得を統合する管理クラス
/// @details PlayScene から直接武器コントローラや成長データを操作しないためのファサード。
///          Player の所有権は持たず、フレーム更新と描画時に必要な非所有参照として扱う。
class PlayerManager {
public:
	/// @brief PlayerProgression と PlayerWeaponController を初期化する
	/// @param player 管理対象の Player。所有権は受け取らない
	void Initialize(Player* player);

	/// @brief 武器更新や敵検索に使う EnemyManager を設定する
	/// @param enemyManager PlayScene が所有する EnemyManager
	void SetEnemyManager(EnemyManager* enemyManager)
	{
		enemyManager_ = enemyManager;
	}

	/// @brief プレイヤーの基本ステータス CSV を読み込む
	/// @param filePath 読み込む CSV パス
	void LoadStatusFromCSV(const std::string& filePath);

	/// @brief キャラクター別ステータスを読み込んで現在の Player に反映する
	/// @param filePath キャラクター定義 CSV パス
	/// @param characterKey 読み込むキャラクター行のキー
	void LoadCharacterStats(
		const std::string& filePath,
		const std::string& characterKey)
	{
		progression_.LoadCharacterStats(filePath, characterKey, player_);
	}

	/// @brief 現在のキャラクター ID を武器表示制御へ反映する
	/// @param characterId 選択中キャラクター
	void SetCharacterId(CharacterId characterId)
	{
		weapons_.SetCharacterId(characterId);
	}

	/// @brief 武器強化値の CSV を読み込む
	/// @param filePath 武器強化設定 CSV パス
	void LoadWeaponUpgradeSettings(const std::string& filePath);

	/// @brief 武器本体モデルの表示調整値を読み込む
	/// @param tuning UI レイアウト形式の調整値マップ
	void LoadWeaponVisualTuning(const UILayoutIO::LayoutMap& tuning)
	{
		weapons_.LoadVisualTuning(tuning);
	}

	/// @brief 現在の武器本体モデル調整値を保存用エントリへ追加する
	/// @param entries 追記先のレイアウトエントリ配列
	void AppendWeaponVisualTuningEntries(
		std::vector<UILayoutIO::Entry>& entries) const
	{
		weapons_.AppendVisualTuningEntries(entries);
	}
#ifdef _DEBUG
	/// @brief 武器本体モデルのデバッグ調整 UI を描画する
	void DrawWeaponVisualDebugUI()
	{
		weapons_.DrawWeaponVisualDebugUI();
	}
#endif

	/// @brief プレイヤー成長、武器、無敵時間などを 1 フレーム更新する
	/// @param deltaTime 前フレームからの経過秒
	void Update(float deltaTime);

	/// @brief プレイヤーが所有する武器・弾を描画する
	void Draw();

	/// @brief プレイヤーにダメージを与え、無敵時間と死亡状態を更新する
	/// @param damage 適用するダメージ量
	/// @return 実際にダメージが通った場合 true
	bool TakeDamage(int32_t damage = 10);

	/// @brief HP を回復する
	/// @param amount 回復量
	/// @return 実際に回復した量
	int32_t RecoverHP(int32_t amount = 1);

	/// @brief 命中時ライフスティールを適用する
	/// @return 実際に回復した量
	int32_t ApplyLifeStealOnHit();
	int32_t GetHP() const { return progression_.GetHP(); }
	int32_t GetMaxHP() const { return progression_.GetMaxHP(); }
	bool IsMaxHPAtCap() const { return progression_.IsMaxHPAtCap(); }
	bool IsInvincible() const { return invincible_; }
	bool IsDead() const { return progression_.IsDead(); }
#ifdef _DEBUG
	void ForceDebugDeath()
	{
		progression_.ForceDebugDeath();
		invincible_ = false;
		invincibleTimer_ = 0.0f;
	}
#endif

	/// @brief デバッグ用に永続強化と武器を最大付近まで強化する
	void MakeDebugStrongest();

	/// @brief 経験値を加算し、必要ならレベルアップ要求を立てる
	/// @param amount 加算する経験値
	/// @return 実際に加算された経験値
	int32_t AddEXP(int32_t amount);
	int32_t GetEXP() const { return progression_.GetEXP(); }
	int32_t GetTotalEXP() const { return progression_.GetTotalEXP(); }
	int32_t GetLevel() const { return progression_.GetLevel(); }
	int32_t GetNextLevelEXP() const { return progression_.GetNextLevelEXP(); }
	bool IsLevelUpRequested() const { return progression_.IsLevelUpRequested(); }

	/// @brief レベルアップ HUD 処理完了後に要求フラグを解除する
	void ClearLevelUpRequest() { progression_.ClearLevelUpRequest(); }

	int32_t GetAttackPower() const { return progression_.GetAttackPower(); }
	const PlayerStats& GetStats() const { return progression_.GetStats(); }
	float GetDamageMultiplier() const
	{
		return progression_.GetStats().GetDamageMultiplier();
	}
	float GetAttackSpeedMultiplier() const
	{
		return progression_.GetStats().GetAttackSpeedMultiplier();
	}
	float GetDurationMultiplier() const
	{
		return progression_.GetStats().GetDurationMultiplier();
	}
	float GetCoinGainMultiplier() const
	{
		return progression_.GetStats().GetCoinGainMultiplier();
	}
	float GetKnockbackMultiplier() const
	{
		return progression_.GetStats().GetKnockbackMultiplier();
	}
	DamageResult RollDamage(int32_t baseDamage);
	int32_t GetMaxHPUpgradeLevel() const { return progression_.GetMaxHPUpgradeLevel(); }
	int32_t GetAttackPowerUpgradeLevel() const { return progression_.GetAttackPowerUpgradeLevel(); }
	void UpgradeAttackPower() { progression_.UpgradeAttackPower(); }
	void UpgradeStat(PlayerStatType type, float amount)
	{
		progression_.UpgradeStat(type, amount);
	}

	/// @brief 指定パッシブアイテムを新規取得または強化できるか調べる
	/// @param type 対象パッシブアイテム
	/// @return 取得枠・最大レベル条件を満たす場合 true
	bool CanAcquirePassiveItem(PassiveItemType type) const;

	/// @brief 指定パッシブアイテムを取得または強化する
	/// @param type 対象パッシブアイテム
	/// @return 成功した場合 true
	bool UpgradePassiveItem(PassiveItemType type);
	int32_t GetPassiveItemLevel(PassiveItemType type) const
	{
		return passiveItemLevels_[PassiveItemIndex(type)];
	}
	int32_t GetPassiveItemMaxLevel(PassiveItemType) const
	{
		return kPassiveItemMaxLevel;
	}
	void DebugSetPassiveItemLevel(PassiveItemType type, int32_t level);
	void DebugRemovePassiveItem(PassiveItemType type)
	{
		DebugSetPassiveItemLevel(type, 0);
	}
	const std::vector<PassiveItemType>& GetPassiveItemAcquisitionOrder() const
	{
		return passiveItemAcquisitionOrder_;
	}
	size_t GetEquippedPassiveItemCount() const
	{
		return passiveItemAcquisitionOrder_.size();
	}
	void IncreaseMaxHP();
	void UpgradeMoveSpeed();
	void ApplyPermanentUpgrades(
		int32_t maxHPLevel,
		int32_t attackLevel,
		int32_t moveSpeedLevel,
		int32_t expPickupRangeLevel,
		int32_t coinGainLevel);
	int32_t GetMoveSpeedLevel() const { return progression_.GetMoveSpeedLevel(); }
	bool IsMoveSpeedMaxLevel() const { return progression_.IsMoveSpeedMaxLevel(); }
	float GetExpPickupRangeMultiplier() const
	{
		return progression_.GetExpPickupRangeMultiplier();
	}

	/// @brief 弓矢武器を強化する
	void UpgradeNormalBullets();
	static constexpr size_t kMaxEquippedWeaponTypes =
		PlayerWeaponController::kMaxEquippedWeaponTypes;
	bool HasWeapon(WeaponType type) const
	{
		return weapons_.HasWeapon(type);
	}
	size_t GetEquippedWeaponCount() const
	{
		return weapons_.GetEquippedWeaponCount();
	}
	bool CanAcquireWeapon(WeaponType type) const
	{
		return weapons_.CanAcquireWeapon(type);
	}

	/// @brief 通常弾のフレーム内参照を取得する
	/// @return Controller が所有する通常弾配列
	const std::vector<std::unique_ptr<NormalBullet>>&
		GetNormalBullets() const
	{
		return weapons_.GetNormalBullets();
	}
	size_t GetPeakNormalBulletCount() const
	{
		return weapons_.GetPeakNormalBulletCount();
	}
	size_t GetNormalBulletPruneCount() const
	{
		return weapons_.GetNormalBulletPruneCount();
	}
	void ResetBulletTelemetry()
	{
		weapons_.ResetBulletTelemetry();
	}
	float GetNormalBulletInterval() const
	{
		return weapons_.GetNormalBulletInterval();
	}
	int32_t GetNormalBulletLevel() const
	{
		return weapons_.GetNormalBulletLevel();
	}
	static constexpr int32_t kNormalBulletMaxLevel =
		PlayerWeaponController::kNormalBulletMaxLevel;
	static constexpr size_t kMaxActiveNormalBullets =
		PlayerWeaponController::kMaxActiveNormalBullets;
	bool IsNormalBulletMaxLevel() const
	{
		return weapons_.IsNormalBulletMaxLevel();
	}
	int32_t GetNormalBulletDamage() const
	{
		return weapons_.GetNormalBulletDamage(GetStats());
	}

	void AddOrbitBullets();
	void UpgradeOrbitBullets();
	bool HasOrbitBullets() const
	{
		return weapons_.HasOrbitBullets();
	}
	const std::vector<std::unique_ptr<OrbitBullet>>&
		GetOrbitBullets() const
	{
		return weapons_.GetOrbitBullets();
	}
	int32_t GetOrbitBulletLevel() const
	{
		return weapons_.GetOrbitBulletLevel();
	}
	static constexpr int32_t kOrbitBulletMaxLevel =
		PlayerWeaponController::kOrbitBulletMaxLevel;
	bool IsOrbitBulletMaxLevel() const
	{
		return weapons_.IsOrbitBulletMaxLevel();
	}
	int32_t GetOrbitBulletDamage() const
	{
		return weapons_.GetOrbitBulletDamage(GetStats());
	}

	void AddLightning();
	void UpgradeLightning();
	bool HasLightning() const { return weapons_.HasLightning(); }
	int32_t GetLightningLevel() const
	{
		return weapons_.GetLightningLevel();
	}
	int32_t GetLightningDamage() const
	{
		return weapons_.GetLightningDamage(GetStats());
	}
	int32_t GetLightningStrikeCount() const
	{
		return weapons_.GetLightningStrikeCount();
	}
	float GetLightningRadius() const
	{
		return weapons_.GetLightningRadius();
	}
	const std::vector<Vector3>&
		GetLightningEffectTargets() const
	{
		return weapons_.GetLightningEffectTargets();
	}
	float GetLightningEffectTimer() const
	{
		return weapons_.GetLightningEffectTimer();
	}
	static constexpr int32_t kLightningMaxLevel =
		PlayerWeaponController::kLightningMaxLevel;
	bool IsLightningMaxLevel() const
	{
		return weapons_.IsLightningMaxLevel();
	}

	void AddExplosiveBullets();
	void UpgradeExplosiveBullets();
	bool HasExplosiveBullets() const { return weapons_.HasExplosiveBullets(); }
	const std::vector<std::unique_ptr<NormalBullet>>&
		GetExplosiveBullets() const
	{
		return weapons_.GetExplosiveBullets();
	}
	const std::vector<Vector3>& GetRecentNormalBulletShotPositions() const
	{
		return weapons_.GetRecentNormalBulletShotPositions();
	}
	const std::vector<Vector3>& GetRecentExplosiveBulletShotPositions() const
	{
		return weapons_.GetRecentExplosiveBulletShotPositions();
	}
	int32_t GetExplosiveBulletLevel() const
	{
		return weapons_.GetExplosiveBulletLevel();
	}
	static constexpr int32_t kExplosiveBulletMaxLevel =
		PlayerWeaponController::kExplosiveBulletMaxLevel;
	bool IsExplosiveBulletMaxLevel() const
	{
		return weapons_.IsExplosiveBulletMaxLevel();
	}
	int32_t GetExplosiveBulletDamage() const
	{
		return weapons_.GetExplosiveBulletDamage(GetStats());
	}
	float GetExplosiveBulletRadius() const
	{
		return weapons_.GetExplosiveBulletRadius(GetStats());
	}

	void UpgradeSword() { weapons_.UpgradeSword(); }
	bool HasSword() const { return weapons_.HasSword(); }
	int32_t GetSwordLevel() const { return weapons_.GetSwordLevel(); }
	bool IsSwordMaxLevel() const { return weapons_.IsSwordMaxLevel(); }
	int32_t GetSwordDamage() const { return weapons_.GetSwordDamage(GetStats()); }
	void UpgradeAura() { weapons_.UpgradeAura(); }
	bool HasAura() const { return weapons_.HasAura(); }
	int32_t GetAuraLevel() const { return weapons_.GetAuraLevel(); }
	bool IsAuraMaxLevel() const { return weapons_.IsAuraMaxLevel(); }
	int32_t GetAuraDamage() const { return weapons_.GetAuraDamage(GetStats()); }
	float GetAuraRadius() const { return weapons_.GetAuraRadius(GetStats()); }
	void UpgradeFlameShoes() { weapons_.UpgradeFlameShoes(); }
	bool HasFlameShoes() const { return weapons_.HasFlameShoes(); }
	int32_t GetFlameShoesLevel() const { return weapons_.GetFlameShoesLevel(); }
	bool IsFlameShoesMaxLevel() const { return weapons_.IsFlameShoesMaxLevel(); }
	int32_t GetFlameShoesDamage() const
	{
		return weapons_.GetFlameShoesDamage(GetStats());
	}
	size_t GetFlameZoneCount() const { return weapons_.GetFlameZoneCount(); }
	bool DidAuraPulseThisFrame() const
	{
		return weapons_.DidAuraPulseThisFrame();
	}
	const std::vector<Vector3>& GetRecentFlameZoneSpawns() const
	{
		return weapons_.GetRecentFlameZoneSpawns();
	}
	const std::vector<FlameZoneVisual>& GetFlameZoneVisuals() const
	{
		return weapons_.GetFlameZoneVisuals();
	}
	const std::vector<SwordSlashEvent>& GetRecentSwordSlashes() const
	{
		return weapons_.GetRecentSwordSlashes();
	}
	void UpgradeBone() { weapons_.UpgradeBone(); }
	void UpgradeHandgun() { weapons_.UpgradeHandgun(); }
	void UpgradeBoomerang() { weapons_.UpgradeBoomerang(); }
	bool HasBone() const { return weapons_.HasBone(); }
	bool HasHandgun() const { return weapons_.HasHandgun(); }
	bool HasBoomerang() const { return weapons_.HasBoomerang(); }
	int32_t GetBoneLevel() const { return weapons_.GetBoneLevel(); }
	int32_t GetHandgunLevel() const { return weapons_.GetHandgunLevel(); }
	int32_t GetBoomerangLevel() const { return weapons_.GetBoomerangLevel(); }
	bool IsBoneMaxLevel() const { return weapons_.IsBoneMaxLevel(); }
	bool IsHandgunMaxLevel() const { return weapons_.IsHandgunMaxLevel(); }
	bool IsBoomerangMaxLevel() const { return weapons_.IsBoomerangMaxLevel(); }
	int32_t GetBoneDamage() const { return weapons_.GetBoneDamage(GetStats()); }
	int32_t GetHandgunDamage() const { return weapons_.GetHandgunDamage(GetStats()); }
	int32_t GetBoomerangDamage() const { return weapons_.GetBoomerangDamage(GetStats()); }
	const auto& GetBoneBullets() const { return weapons_.GetBoneBullets(); }
	const auto& GetHandgunBullets() const { return weapons_.GetHandgunBullets(); }
	const auto& GetBoomerangBullets() const { return weapons_.GetBoomerangBullets(); }
	const std::vector<Vector3>& GetRecentBoneShotPositions() const
	{
		return weapons_.GetRecentBoneShotPositions();
	}
	const std::vector<Vector3>& GetRecentHandgunShotPositions() const
	{
		return weapons_.GetRecentHandgunShotPositions();
	}
	const std::vector<Vector3>& GetRecentHandgunReloadPositions() const
	{
		return weapons_.GetRecentHandgunReloadPositions();
	}
	const std::vector<Vector3>& GetRecentBoomerangShotPositions() const
	{
		return weapons_.GetRecentBoomerangShotPositions();
	}

	void MaxAllWeapons();

	/// @brief 指定武器の現在レベルを取得する
	/// @param type 対象武器
	/// @return 未所持なら 0、所持済みなら現在レベル
	int32_t GetWeaponLevel(WeaponType type) const
	{
		return weapons_.GetWeaponLevel(type);
	}

	/// @brief 指定武器の最大レベルを取得する
	/// @param type 対象武器
	/// @return 最大レベル
	int32_t GetWeaponMaxLevel(WeaponType type) const
	{
		return weapons_.GetWeaponMaxLevel(type);
	}

	/// @brief デバッグ用に指定武器レベルを直接設定する
	/// @param type 対象武器
	/// @param level 設定するレベル
	void DebugSetWeaponLevel(WeaponType type, int32_t level)
	{
		weapons_.DebugSetWeaponLevel(type, player_, level);
	}
	void DebugRemoveWeapon(WeaponType type)
	{
		weapons_.DebugRemoveWeapon(type, player_);
	}
	void DebugFireWeapon(WeaponType type)
	{
		weapons_.DebugFireWeapon(
			type,
			player_,
			enemyManager_,
			progression_.GetStats());
	}
	void PlayLevelUpEffect();

private:
	// 被弾直後の点滅/無敵時間を更新する。
	void UpdateInvincibility(float deltaTime);

	// 非所有参照。実体は PlayScene / EnemyManager が所有し、PlayerManager はフレーム内連携だけに使う。
	Player* player_ = nullptr;
	EnemyManager* enemyManager_ = nullptr;

	// 成長値と武器状態の実データ。PlayerManager は外部公開 API の薄い統合層にする。
	PlayerProgression progression_{};
	PlayerWeaponController weapons_{};

	// 被弾演出と確率ダメージ用の一時状態。
	bool invincible_ = false;
	float invincibleTimer_ = 0.0f;
	bool visible_ = true;
	float invincibilityDuration_ = 1.25f;
	float hpRegenAccumulator_ = 0.0f;
	std::mt19937 damageRandomEngine_{ std::random_device{}() };

	// パッシブアイテムは固定 enum 数でレベルを持ち、取得順は HUD 表示と上限判定に使う。
	std::array<int32_t, static_cast<size_t>(PassiveItemType::Count)>
		passiveItemLevels_{};
	std::vector<PassiveItemType> passiveItemAcquisitionOrder_;
};

} // namespace DirectXGame
