#pragma once

#include "Vector3.h"
#include "Player.h"
#include "PlayerProgression.h"
#include "PlayerWeaponController.h"
#include "PassiveItemData.h"
#include <array>
#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace DirectXGame {

class EnemyManager;

class PlayerManager {
public:
	void Initialize(Player* player);
	void SetEnemyManager(EnemyManager* enemyManager)
	{
		enemyManager_ = enemyManager;
	}
	void LoadStatusFromCSV(const std::string& filePath);
	void LoadCharacterStats(
		const std::string& filePath,
		const std::string& characterKey)
	{
		progression_.LoadCharacterStats(filePath, characterKey, player_);
	}
	void LoadWeaponUpgradeSettings(const std::string& filePath);
	void Update(float deltaTime);
	void Draw();

	bool TakeDamage(int32_t damage = 10);
	int32_t RecoverHP(int32_t amount = 1);
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
	void MakeDebugStrongest();

	int32_t AddEXP(int32_t amount);
	int32_t GetEXP() const { return progression_.GetEXP(); }
	int32_t GetTotalEXP() const { return progression_.GetTotalEXP(); }
	int32_t GetLevel() const { return progression_.GetLevel(); }
	int32_t GetNextLevelEXP() const { return progression_.GetNextLevelEXP(); }
	bool IsLevelUpRequested() const { return progression_.IsLevelUpRequested(); }
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
	DamageResult RollDamage(int32_t baseDamage);
	int32_t GetMaxHPUpgradeLevel() const { return progression_.GetMaxHPUpgradeLevel(); }
	int32_t GetAttackPowerUpgradeLevel() const { return progression_.GetAttackPowerUpgradeLevel(); }
	void UpgradeAttackPower() { progression_.UpgradeAttackPower(); }
	void UpgradeStat(PlayerStatType type, float amount)
	{
		progression_.UpgradeStat(type, amount);
	}
	bool CanAcquirePassiveItem(PassiveItemType type) const;
	bool UpgradePassiveItem(PassiveItemType type);
	int32_t GetPassiveItemLevel(PassiveItemType type) const
	{
		return passiveItemLevels_[PassiveItemIndex(type)];
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

	void MaxAllWeapons();
	void PlayLevelUpEffect();

private:
	void UpdateInvincibility(float deltaTime);

	Player* player_ = nullptr;
	EnemyManager* enemyManager_ = nullptr;
	PlayerProgression progression_{};
	PlayerWeaponController weapons_{};

	bool invincible_ = false;
	float invincibleTimer_ = 0.0f;
	bool visible_ = true;
	float invincibilityDuration_ = 1.25f;
	float hpRegenAccumulator_ = 0.0f;
	std::mt19937 damageRandomEngine_{ std::random_device{}() };
	std::array<int32_t, static_cast<size_t>(PassiveItemType::Count)>
		passiveItemLevels_{};
	std::vector<PassiveItemType> passiveItemAcquisitionOrder_;
};

} // namespace DirectXGame
