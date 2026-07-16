#pragma once

#include "Vector3.h"
#include "WeaponType.h"
#include "WeaponUpgradeData.h"
#include "PlayerStats.h"
#include "NormalBullet.h"
#include "OrbitBullet.h"
#include "AutoProjectileWeapon.h"
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace DirectXGame {

class EnemyManager;
class Player;

struct SwordSlashEvent {
	// 演出側が 1 フレーム分の斬撃を再構成するための軽量イベント。
	Vector3 center{};
	Vector3 forward{ 0.0f, 0.0f, 1.0f };
	float radius = 0.0f;
	float halfAngle = 0.0f;
	int32_t directionSign = 1;
};

struct FlameZoneVisual {
	// 炎床の描画に必要な情報だけを公開する。ダメージタイマーなどの内部状態は持たない。
	Vector3 position{};
	Vector3 direction{ 0.0f, 0.0f, 1.0f };
	float radius = 0.0f;
	float remainingDuration = 0.0f;
	float totalDuration = 1.0f;
};

class PlayerWeaponController final {
public:
	// 武器ごとの最大レベルと実体数上限。
	// レベルアップ候補生成、デバッグ強化、ソフトキャップ表示で同じ値を参照する。
	static constexpr int32_t kNormalBulletMaxLevel = 8;
	static constexpr int32_t kOrbitBulletMaxLevel = 8;
	static constexpr int32_t kLightningMaxLevel = 8;
	static constexpr int32_t kExplosiveBulletMaxLevel = 8;
	static constexpr int32_t kSwordMaxLevel = 8;
	static constexpr int32_t kAuraMaxLevel = 8;
	static constexpr int32_t kFlameShoesMaxLevel = 8;
	static constexpr int32_t kBoneMaxLevel = 8;
	static constexpr int32_t kHandgunMaxLevel = 8;
	static constexpr int32_t kBoomerangMaxLevel = 8;
	static constexpr size_t kMaxEquippedWeaponTypes = 4;
	static constexpr size_t kMaxActiveNormalBullets = 96;

	// 初期化とデータ駆動設定。
	// CSV のキーを内部パラメータに反映し、武器追加時にコード以外の調整余地を残す。
	void Initialize(const std::string& upgradeSettingsPath);
	bool LoadStatusValue(
		const std::string& key,
		const std::string& value);
	void LoadUpgradeSettings(const std::string& filePath);
	void Update(
		float deltaTime,
		Player* player,
		EnemyManager* enemyManager,
		const PlayerStats& playerStats);
	void Draw();

	// 外部公開する強化 API。
	// PlayerManager からのみ呼ばれる想定で、取得済みフラグ・レベル・弾再構築をここで一貫処理する。
	void UpgradeNormalBullets(Player* player);
	void AddOrbitBullets(Player* player);
	void UpgradeOrbitBullets(Player* player);
	void AddLightning();
	void UpgradeLightning();
	void AddExplosiveBullets();
	void UpgradeExplosiveBullets();
	void UpgradeSword();
	void UpgradeAura();
	void UpgradeFlameShoes();
	void UpgradeBone();
	void UpgradeHandgun();
	void UpgradeBoomerang();
	void UpgradeWeapon(WeaponType type, Player* player);
	void MaxAllWeapons(Player* player);
	int32_t GetWeaponLevel(WeaponType type) const;
	int32_t GetWeaponMaxLevel(WeaponType type) const;
	void DebugSetWeaponLevel(WeaponType type, Player* player, int32_t level);
	void DebugRemoveWeapon(WeaponType type, Player* player);
	void DebugFireWeapon(
		WeaponType type,
		Player* player,
		EnemyManager* enemyManager,
		const PlayerStats& playerStats);
	bool HasWeapon(WeaponType type) const;
	size_t GetEquippedWeaponCount() const;
	bool CanAcquireWeapon(WeaponType type) const;

	// 弾・演出イベントの参照公開。
	// 所有権は Controller 側に残し、HUD/演出/デバッグはフレーム内読み取りだけ行う。
	const std::vector<std::unique_ptr<NormalBullet>>&
		GetNormalBullets() const
	{
		return normalBullets_;
	}
	const std::vector<std::unique_ptr<OrbitBullet>>&
		GetOrbitBullets() const
	{
		return orbitBullets_;
	}
	const std::vector<Vector3>& GetLightningEffectTargets() const
	{
		return lightningEffectTargets_;
	}
	const std::vector<std::unique_ptr<NormalBullet>>&
		GetExplosiveBullets() const
	{
		return explosiveBullets_;
	}
	const auto& GetBoneBullets() const { return boneWeapon_.GetBullets(); }
	const auto& GetHandgunBullets() const { return handgunWeapon_.GetBullets(); }
	const auto& GetBoomerangBullets() const { return boomerangWeapon_.GetBullets(); }
	const std::vector<Vector3>& GetRecentHandgunShotPositions() const
	{
		return handgunWeapon_.GetRecentShotPositions();
	}
	const std::vector<Vector3>& GetRecentHandgunReloadPositions() const
	{
		return handgunWeapon_.GetRecentReloadPositions();
	}
	float GetLightningEffectTimer() const
	{
		return lightningEffectTimer_;
	}
	float GetNormalBulletInterval() const
	{
		return normalBulletInterval_;
	}
	int32_t GetNormalBulletLevel() const
	{
		return normalBulletLevel_;
	}
	int32_t GetOrbitBulletLevel() const
	{
		return orbitBulletLevel_;
	}
	int32_t GetLightningLevel() const { return lightningLevel_; }
	int32_t GetExplosiveBulletLevel() const { return explosiveBulletLevel_; }
	int32_t GetSwordLevel() const { return swordLevel_; }
	int32_t GetAuraLevel() const { return auraLevel_; }
	int32_t GetFlameShoesLevel() const { return flameShoesLevel_; }
	int32_t GetBoneLevel() const { return boneWeapon_.GetLevel(); }
	int32_t GetHandgunLevel() const { return handgunWeapon_.GetLevel(); }
	int32_t GetBoomerangLevel() const { return boomerangWeapon_.GetLevel(); }
	int32_t GetLightningStrikeCount() const
	{
		return lightningStrikeCount_;
	}
	float GetLightningRadius() const { return lightningRadius_; }
	bool HasOrbitBullets() const { return hasOrbitBullets_; }
	bool HasLightning() const { return hasLightning_; }
	bool HasExplosiveBullets() const { return hasExplosiveBullets_; }
	bool HasSword() const { return hasSword_; }
	bool HasAura() const { return hasAura_; }
	bool HasFlameShoes() const { return hasFlameShoes_; }
	bool HasBone() const { return boneWeapon_.IsActive(); }
	bool HasHandgun() const { return handgunWeapon_.IsActive(); }
	bool HasBoomerang() const { return boomerangWeapon_.IsActive(); }
	bool IsNormalBulletMaxLevel() const
	{
		return normalBulletLevel_ >= kNormalBulletMaxLevel;
	}
	bool IsOrbitBulletMaxLevel() const
	{
		return hasOrbitBullets_ &&
			orbitBulletLevel_ >= kOrbitBulletMaxLevel;
	}
	bool IsLightningMaxLevel() const
	{
		return hasLightning_ &&
			lightningLevel_ >= kLightningMaxLevel;
	}
	bool IsExplosiveBulletMaxLevel() const
	{
		return hasExplosiveBullets_ &&
			explosiveBulletLevel_ >= kExplosiveBulletMaxLevel;
	}
	bool IsSwordMaxLevel() const { return hasSword_ && swordLevel_ >= kSwordMaxLevel; }
	bool IsAuraMaxLevel() const { return hasAura_ && auraLevel_ >= kAuraMaxLevel; }
	bool IsFlameShoesMaxLevel() const
	{
		return hasFlameShoes_ && flameShoesLevel_ >= kFlameShoesMaxLevel;
	}
	bool IsBoneMaxLevel() const { return boneWeapon_.IsMaxLevel(); }
	bool IsHandgunMaxLevel() const
	{
		return handgunWeapon_.IsMaxLevel();
	}
	bool IsBoomerangMaxLevel() const
	{
		return boomerangWeapon_.IsMaxLevel();
	}
	bool IsWeaponMaxLevel(WeaponType type) const;
	int32_t GetNormalBulletDamage(const PlayerStats& stats) const
	{
		return stats.Resolve(
			{ 10.0f + normalBulletDamageBonus_ },
			GetWeaponStatApplicability(WeaponType::BowArrow)).damage;
	}
	int32_t GetOrbitBulletDamage(const PlayerStats& stats) const
	{
		return stats.Resolve(
			{ 10.0f + orbitDamageBonus_ },
			GetWeaponStatApplicability(WeaponType::Rock)).damage;
	}
	int32_t GetLightningDamage(const PlayerStats& stats) const
	{
		return stats.Resolve(
			{ 10.0f + lightningDamageBonus_ },
			GetWeaponStatApplicability(WeaponType::ThunderStaff)).damage;
	}
	int32_t GetExplosiveBulletDamage(const PlayerStats& stats) const
	{
		return stats.Resolve(
			{ 10.0f + explosiveBulletDamageBonus_ },
			GetWeaponStatApplicability(WeaponType::FlameStaff)).damage;
	}
	float GetExplosiveBulletRadius(const PlayerStats& stats) const
	{
		return explosiveBulletRadius_ * stats.GetAreaSizeMultiplier();
	}
	int32_t GetSwordDamage(const PlayerStats& stats) const
	{
		return stats.Resolve(
			{ 14.0f + swordDamageBonus_ },
			GetWeaponStatApplicability(WeaponType::Sword)).damage;
	}
	int32_t GetAuraDamage(const PlayerStats& stats) const
	{
		return stats.Resolve(
			{ 8.0f + auraDamageBonus_ },
			GetWeaponStatApplicability(WeaponType::Aura)).damage;
	}
	float GetAuraRadius(const PlayerStats& stats) const
	{
		return stats.Resolve(
			{ 1.0f, 1.0f, 1.0f, 1.0f, auraRadius_, 1 },
			GetWeaponStatApplicability(WeaponType::Aura)).areaSize;
	}
	int32_t GetFlameShoesDamage(const PlayerStats& stats) const
	{
		return stats.Resolve(
			{ 5.0f + flameShoesDamageBonus_ },
			GetWeaponStatApplicability(WeaponType::FlameShoes)).damage;
	}
	int32_t GetBoneDamage(const PlayerStats& stats) const
	{
		return boneWeapon_.GetDamage(stats);
	}
	int32_t GetHandgunDamage(const PlayerStats& stats) const
	{
		return handgunWeapon_.GetDamage(stats);
	}
	int32_t GetBoomerangDamage(const PlayerStats& stats) const
	{
		return boomerangWeapon_.GetDamage(stats);
	}
	size_t GetFlameZoneCount() const { return flameZones_.size(); }
	bool DidAuraPulseThisFrame() const { return auraPulseThisFrame_; }
	const std::vector<Vector3>& GetRecentFlameZoneSpawns() const
	{
		return recentFlameZoneSpawns_;
	}
	const std::vector<FlameZoneVisual>& GetFlameZoneVisuals() const
	{
		return flameZoneVisuals_;
	}
	const std::vector<SwordSlashEvent>& GetRecentSwordSlashes() const
	{
		return recentSwordSlashes_;
	}
	size_t GetPeakNormalBulletCount() const
	{
		return peakNormalBulletCount_;
	}
	size_t GetNormalBulletPruneCount() const
	{
		return normalBulletPruneCount_;
	}
	void ResetBulletTelemetry();

private:
	// 武器ごとの更新単位。
	// Update() から固定順で呼び、弾生成、敵検索、ダメージ適用、演出イベント生成を分離する。
	void UpdateNormalBullets(
		float deltaTime,
		Player* player,
		EnemyManager* enemyManager,
		const PlayerStats& stats);
	NormalBullet& AcquireNormalBullet();
	void RecycleNormalBullet(size_t index);
	void RecycleInactiveNormalBullets();
	void UpdateOrbitBullets(float deltaTime, Player* player, const PlayerStats& stats);
	void UpdateLightning(
		float deltaTime,
		EnemyManager* enemyManager,
		const PlayerStats& stats);
	void UpdateExplosiveBullets(
		float deltaTime,
		Player* player,
		EnemyManager* enemyManager,
		const PlayerStats& stats);
	void UpdateSword(
		float deltaTime,
		Player* player,
		EnemyManager* enemyManager,
		const PlayerStats& stats);
	void UpdateAura(float deltaTime, Player* player,
		EnemyManager* enemyManager, const PlayerStats& stats);
	void UpdateFlameShoes(float deltaTime, Player* player,
		EnemyManager* enemyManager, const PlayerStats& stats);
	void UpdateBone(float deltaTime, Player* player,
		EnemyManager* enemyManager, const PlayerStats& stats);
	void UpdateHandgun(float deltaTime, Player* player,
		EnemyManager* enemyManager, const PlayerStats& stats);
	void UpdateBoomerang(float deltaTime, Player* player,
		EnemyManager* enemyManager, const PlayerStats& stats);
	Vector3 ResolveAimDirection(
		const Player& player,
		const EnemyManager* enemyManager) const;

	// 弾プール管理。
	// 頻繁な生成破棄を避け、上限超過時は非アクティブ弾を再利用する。
	NormalBullet& AcquireExplosiveBullet();
	void RecycleExplosiveBullet(size_t index);
	void RecycleInactiveExplosiveBullets();
	void RebuildOrbitBullets(Player* player, int32_t projectileCountBonus = 0);
	void ApplyNormalBulletUpgradeLevel(int32_t level);
	void ApplyOrbitUpgradeLevel(int32_t level);
	void ApplyLightningUpgradeLevel(int32_t level);
	void ApplyExplosiveBulletUpgradeLevel(int32_t level);
	void ApplySwordUpgradeLevel(int32_t level);
	void ApplyAuraUpgradeLevel(int32_t level);
	void ApplyFlameShoesUpgradeLevel(int32_t level);
	void ResetNormalBulletWeapon();
	void ResetOrbitWeapon();
	void ResetLightningWeapon();
	void ResetExplosiveWeapon();
	void ResetSwordWeapon();
	void ResetAuraWeapon();
	void ResetFlameShoesWeapon();

	// CSV 設定の参照ヘルパー。
	// 未設定キーは fallback を使い、古い CSV でも起動できるようにする。
	int32_t GetLevelUpgradeSettingInt(
		const std::string& prefix,
		int32_t level,
		const std::string& suffix,
		int32_t fallback) const;
	float GetLevelUpgradeSetting(
		const std::string& prefix,
		int32_t level,
		const std::string& suffix,
		float fallback) const;
	float GetUpgradeSetting(
		const std::string& key,
		float fallback) const;

	// 通常弾。最初から有効な基礎武器で、弾プールと発射間隔を持つ。
	std::vector<std::unique_ptr<NormalBullet>> normalBullets_;
	std::vector<std::unique_ptr<NormalBullet>> normalBulletPool_;
	bool hasNormalBullets_ = true;
	float normalBulletInterval_ = 0.85f;
	float normalBulletTimer_ = 0.0f;
	int32_t normalBulletLevel_ = 1;
	int32_t normalBulletAmount_ = 1;
	int32_t normalBulletDamageBonus_ = 0;
	int32_t normalBulletPierceCount_ = 1;
	float normalBulletSpeed_ = 1.0f;
	float normalBulletRange_ = 30.0f;
	float normalBulletScale_ = 1.0f;
	size_t peakNormalBulletCount_ = 0;
	size_t normalBulletPruneCount_ = 0;
	float normalBulletMinInterval_ = 0.18f;

	// 周回弾。プレイヤー周辺に弾を再配置するため、レベル変更時に Rebuild する。
	std::vector<std::unique_ptr<OrbitBullet>> orbitBullets_;
	bool hasOrbitBullets_ = false;
	int32_t orbitBulletLevel_ = 0;
	int32_t orbitBulletCount_ = 1;
	int32_t orbitDamageBonus_ = 0;
	float orbitRadius_ = 10.0f;
	float orbitRadiusUpgradeStep_ = 2.0f;
	float orbitAngularSpeed_ = 0.03f;
	float orbitAngularSpeedUpgradeStep_ = 0.01f;
	float orbitBulletScale_ = 1.0f;
	float orbitBulletScaleUpgradeStep_ = 0.2f;
	float orbitHitInterval_ = 0.5f;
	float orbitHitIntervalUpgradeMultiplier_ = 0.8f;

	// 雷。実弾を持たず、一定間隔で EnemyManager から候補位置を取得して範囲ダメージを入れる。
	bool hasLightning_ = false;
	int32_t lightningLevel_ = 0;
	int32_t lightningStrikeCount_ = 1;
	int32_t lightningDamageBonus_ = 0;
	float lightningRadius_ = 6.0f;
	float lightningInterval_ = 2.4f;
	float lightningTimer_ = 0.0f;
	std::vector<Vector3> lightningEffectTargets_;
	float lightningEffectTimer_ = 0.0f;

	// 爆発弾。通常弾と同じ NormalBullet 実体を使い、着弾時の範囲ダメージだけ別扱いにする。
	std::vector<std::unique_ptr<NormalBullet>> explosiveBullets_;
	std::vector<std::unique_ptr<NormalBullet>> explosiveBulletPool_;
	bool hasExplosiveBullets_ = false;
	int32_t explosiveBulletLevel_ = 0;
	int32_t explosiveBulletDamageBonus_ = 4;
	float explosiveBulletInterval_ = 2.0f;
	float explosiveBulletTimer_ = 0.0f;
	float explosiveBulletSpeed_ = 0.82f;
	float explosiveBulletRange_ = 28.0f;
	float explosiveBulletRadius_ = 4.2f;
	int32_t explosiveBulletCount_ = 1;
	int32_t explosiveBurstShotsRemaining_ = 0;
	float explosiveBurstTimer_ = 0.0f;
	float explosiveBurstInterval_ = 0.14f;

	// 剣。弾を生成せず、前方扇形の即時ダメージと斬撃イベントを発行する。
	bool hasSword_ = false;
	int32_t swordLevel_ = 0;
	int32_t swordDamageBonus_ = 0;
	float swordInterval_ = 1.15f;
	float swordTimer_ = 0.0f;
	float swordRadius_ = 7.0f;
	float swordHalfAngle_ = 0.9f;
	int32_t swordSlashCount_ = 1;
	float swordKnockbackStrength_ = 0.8f;
	std::vector<SwordSlashEvent> recentSwordSlashes_;

	// オーラ。プレイヤー中心の周期パルスとして扱い、演出は auraPulseThisFrame_ を参照する。
	bool hasAura_ = false;
	int32_t auraLevel_ = 0;
	int32_t auraDamageBonus_ = 0;
	float auraInterval_ = 0.65f;
	float auraTimer_ = 0.0f;
	float auraRadius_ = 6.0f;
	bool auraPulseThisFrame_ = false;

	struct FlameZone {
		// ダメージ判定用の内部状態。描画へは FlameZoneVisual に変換して渡す。
		Vector3 position{};
		Vector3 direction{ 0.0f, 0.0f, 1.0f };
		float radius = 0.0f;
		float remainingDuration = 0.0f;
		float totalDuration = 1.0f;
		float damageTimer = 0.0f;
	};
	static constexpr size_t kMaxFlameZones = 36;
	// 炎靴。移動方向に複数の炎床を置き、短時間だけ範囲ダメージを継続する。
	bool hasFlameShoes_ = false;
	int32_t flameShoesLevel_ = 0;
	int32_t flameShoesDamageBonus_ = 0;
	float flameShoesZoneDuration_ = 1.5f;
	float flameShoesDamageInterval_ = 0.6f;
	float flameShoesRadius_ = 3.2f;
	float flameShoesSpawnDistance_ = 7.0f;
	int32_t flameShoesZoneCount_ = 1;
	Vector3 flameShoesLastSpawnPosition_{};
	Vector3 flameShoesLastDirection_{ 0.0f, 0.0f, 1.0f };
	bool flameShoesPositionInitialized_ = false;
	std::vector<FlameZone> flameZones_;
	std::vector<FlameZoneVisual> flameZoneVisuals_;
	std::vector<Vector3> recentFlameZoneSpawns_;

	// 汎用自動投射武器。個別の弾種差分は AutoProjectileWeapon 側へ閉じ込める。
	BoneWeapon boneWeapon_{};
	HandgunWeapon handgunWeapon_{};
	BoomerangWeapon boomerangWeapon_{};

	std::unordered_map<std::string, float> upgradeSettings_;
};

}
