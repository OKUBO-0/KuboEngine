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
	Vector3 center{};
	Vector3 forward{ 0.0f, 0.0f, 1.0f };
	float radius = 0.0f;
	float halfAngle = 0.0f;
	int32_t directionSign = 1;
};

struct FlameZoneVisual {
	Vector3 position{};
	Vector3 direction{ 0.0f, 0.0f, 1.0f };
	float radius = 0.0f;
	float remainingDuration = 0.0f;
	float totalDuration = 1.0f;
};

class PlayerWeaponController final {
public:
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
	bool HasWeapon(WeaponType type) const;
	size_t GetEquippedWeaponCount() const;
	bool CanAcquireWeapon(WeaponType type) const;

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

	bool hasLightning_ = false;
	int32_t lightningLevel_ = 0;
	int32_t lightningStrikeCount_ = 1;
	int32_t lightningDamageBonus_ = 0;
	float lightningRadius_ = 6.0f;
	float lightningInterval_ = 2.4f;
	float lightningTimer_ = 0.0f;
	std::vector<Vector3> lightningEffectTargets_;
	float lightningEffectTimer_ = 0.0f;

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

	bool hasAura_ = false;
	int32_t auraLevel_ = 0;
	int32_t auraDamageBonus_ = 0;
	float auraInterval_ = 0.65f;
	float auraTimer_ = 0.0f;
	float auraRadius_ = 6.0f;
	bool auraPulseThisFrame_ = false;

	struct FlameZone {
		Vector3 position{};
		Vector3 direction{ 0.0f, 0.0f, 1.0f };
		float radius = 0.0f;
		float remainingDuration = 0.0f;
		float totalDuration = 1.0f;
		float damageTimer = 0.0f;
	};
	static constexpr size_t kMaxFlameZones = 36;
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
	BoneWeapon boneWeapon_{};
	HandgunWeapon handgunWeapon_{};
	BoomerangWeapon boomerangWeapon_{};

	std::unordered_map<std::string, float> upgradeSettings_;
};

}
