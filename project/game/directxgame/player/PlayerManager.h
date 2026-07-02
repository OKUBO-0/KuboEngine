#pragma once

#include "Vector3.h"
#include "Player.h"
#include "PlayerProgression.h"
#include "PlayerWeaponController.h"
#include <cstdint>
#include <memory>
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
	void LoadWeaponUpgradeSettings(const std::string& filePath);
	void Update(float deltaTime);
	void Draw();

	void TakeDamage(int32_t damage = 10);
	void RecoverHP();
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

	void AddEXP(int32_t amount);
	int32_t GetEXP() const { return progression_.GetEXP(); }
	int32_t GetTotalEXP() const { return progression_.GetTotalEXP(); }
	int32_t GetLevel() const { return progression_.GetLevel(); }
	int32_t GetNextLevelEXP() const { return progression_.GetNextLevelEXP(); }
	bool IsLevelUpRequested() const { return progression_.IsLevelUpRequested(); }
	void ClearLevelUpRequest() { progression_.ClearLevelUpRequest(); }

	int32_t GetAttackPower() const { return progression_.GetAttackPower(); }
	int32_t GetMaxHPUpgradeLevel() const { return progression_.GetMaxHPUpgradeLevel(); }
	int32_t GetAttackPowerUpgradeLevel() const { return progression_.GetAttackPowerUpgradeLevel(); }
	void UpgradeAttackPower() { progression_.UpgradeAttackPower(); }
	void IncreaseMaxHP();
	void UpgradeMoveSpeed();
	void ApplyPermanentUpgrades(
		int32_t maxHPLevel,
		int32_t attackLevel,
		int32_t moveSpeedLevel,
		int32_t expPickupRangeLevel);
	int32_t GetMoveSpeedLevel() const { return progression_.GetMoveSpeedLevel(); }
	bool IsMoveSpeedMaxLevel() const { return progression_.IsMoveSpeedMaxLevel(); }
	float GetExpPickupRangeMultiplier() const
	{
		return progression_.GetExpPickupRangeMultiplier();
	}

	void UpgradeNormalBullets();
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
		return weapons_.GetNormalBulletDamage(GetAttackPower());
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
		return weapons_.GetOrbitBulletDamage(GetAttackPower());
	}

	void AddDrone();
	void UpgradeDrone();
	bool HasDrone() const { return weapons_.HasDrone(); }
	const std::unique_ptr<Drone>& GetDrone() const
	{
		return weapons_.GetDrone();
	}
	int32_t GetDroneLevel() const
	{
		return weapons_.GetDroneLevel();
	}
	static constexpr int32_t kDroneMaxLevel =
		PlayerWeaponController::kDroneMaxLevel;
	bool IsDroneMaxLevel() const
	{
		return weapons_.IsDroneMaxLevel();
	}
	int32_t GetDroneDamage() const
	{
		return weapons_.GetDroneDamage(GetAttackPower());
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
		return weapons_.GetLightningDamage(GetAttackPower());
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
		return weapons_.GetExplosiveBulletDamage(GetAttackPower());
	}
	float GetExplosiveBulletRadius() const
	{
		return weapons_.GetExplosiveBulletRadius();
	}

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
};

} // namespace DirectXGame
