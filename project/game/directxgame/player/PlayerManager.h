#pragma once

#include "Vector3.h"
#include "game/directxgame/core/GameLightSettings.h"
#include "game/directxgame/player/Player.h"
#include "game/directxgame/player/weapons/Drone.h"
#include "game/directxgame/player/weapons/NormalBullet.h"
#include "game/directxgame/player/weapons/OrbitBullet.h"
#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace DirectXGame {

class EnemyManager;

class PlayerManager {
public:
	void Initialize(Player* player);
	void SetEnemyManager(EnemyManager* enemyManager) { enemyManager_ = enemyManager; }
	void LoadStatusFromCSV(const std::string& filePath);
	void LoadWeaponUpgradeSettings(const std::string& filePath);
	void Update(float deltaTime);
	void Draw();

	void TakeDamage();
	void RecoverHP();
	int32_t GetHP() const { return lifeStock_; }
	int32_t GetMaxHP() const { return maxLifeStock_; }
	bool IsInvincible() const { return invincible_; }
	bool IsDead() const { return lifeStock_ <= 0; }
#ifdef _DEBUG
	void ForceDebugDeath()
	{
		lifeStock_ = 0;
		invincible_ = false;
		invincibleTimer_ = 0.0f;
	}
#endif

	void AddEXP(int32_t amount);
	int32_t GetEXP() const { return exp_; }
	int32_t GetTotalEXP() const { return totalExp_; }
	int32_t GetLevel() const { return level_; }
	int32_t GetNextLevelEXP() const { return nextLevelExp_; }
	bool IsLevelUpRequested() const { return levelUpRequested_; }
	void ClearLevelUpRequest() { levelUpRequested_ = false; }

	int32_t GetAttackPower() const { return attackPower_; }
	void UpgradeAttackPower() { ++attackPower_; }
	void IncreaseMaxHP();
	void UpgradeMoveSpeed();
	int32_t GetMoveSpeedLevel() const { return moveSpeedLevel_; }

	void UpgradeNormalBullets();
	const std::vector<std::unique_ptr<NormalBullet>>& GetNormalBullets() const { return normalBullets_; }
	size_t GetPeakNormalBulletCount() const { return peakNormalBulletCount_; }
	size_t GetNormalBulletPruneCount() const { return normalBulletPruneCount_; }
	void ResetBulletTelemetry()
	{
		peakNormalBulletCount_ = normalBullets_.size();
		normalBulletPruneCount_ = 0;
		if (drone_) {
			drone_->ResetBulletTelemetry();
		}
	}
	float GetNormalBulletInterval() const { return normalBulletInterval_; }
	int32_t GetNormalBulletLevel() const { return normalBulletLevel_; }
	static constexpr int32_t kNormalBulletMaxLevel = 8;
	static constexpr size_t kMaxActiveNormalBullets = 96;
	bool IsNormalBulletMaxLevel() const { return normalBulletLevel_ >= kNormalBulletMaxLevel; }
	int32_t GetNormalBulletDamage() const { return attackPower_ + normalBulletDamageBonus_; }

	void AddOrbitBullets();
	void UpgradeOrbitBullets();
	bool HasOrbitBullets() const { return hasOrbitBullets_; }
	const std::vector<std::unique_ptr<OrbitBullet>>& GetOrbitBullets() const { return orbitBullets_; }
	int32_t GetOrbitBulletLevel() const { return orbitBulletLevel_; }
	static constexpr int32_t kOrbitBulletMaxLevel = 8;
	bool IsOrbitBulletMaxLevel() const { return hasOrbitBullets_ && orbitBulletLevel_ >= kOrbitBulletMaxLevel; }
	int32_t GetOrbitBulletDamage() const { return attackPower_; }

	void AddDrone();
	void UpgradeDrone();
	bool HasDrone() const { return hasDrone_; }
	const std::unique_ptr<Drone>& GetDrone() const { return drone_; }
	int32_t GetDroneLevel() const { return droneLevel_; }
	static constexpr int32_t kDroneMaxLevel = 8;
	bool IsDroneMaxLevel() const { return hasDrone_ && droneLevel_ >= kDroneMaxLevel; }
	int32_t GetDroneDamage() const { return (std::max)(1, attackPower_ / 2 + droneDamageBonus_); }

	void AddLightning();
	void UpgradeLightning();
	bool HasLightning() const { return hasLightning_; }
	int32_t GetLightningLevel() const { return lightningLevel_; }
	int32_t GetLightningDamage() const { return attackPower_ + lightningDamageBonus_; }
	int32_t GetLightningStrikeCount() const { return lightningStrikeCount_; }
	float GetLightningRadius() const { return lightningRadius_; }
	const std::vector<Vector3>& GetLightningEffectTargets() const { return lightningEffectTargets_; }
	float GetLightningEffectTimer() const { return lightningEffectTimer_; }
	static constexpr int32_t kLightningMaxLevel = 8;
	bool IsLightningMaxLevel() const { return hasLightning_ && lightningLevel_ >= kLightningMaxLevel; }

	void MaxAllWeapons();
	void PlayLevelUpEffect();
	void SetLightSettings(const GameLightSettings& lightSettings);

private:
	void UpdateInvincibility(float deltaTime);
	void UpdateNormalBullets(float deltaTime);
	void UpdateOrbitBullets(float deltaTime);
	void UpdateDrone(float deltaTime);
	void UpdateLightning(float deltaTime);
	void RebuildOrbitBullets();
	float GetWeaponUpgradeSetting(const std::string& key, float fallback) const;

	Player* player_ = nullptr;
	EnemyManager* enemyManager_ = nullptr;
	GameLightSettings lightSettings_{};

	bool invincible_ = false;
	float invincibleTimer_ = 0.0f;
	bool visible_ = true;
	float invincibilityDuration_ = 1.25f;

	int32_t level_ = 1;
	int32_t nextLevelExp_ = 10;
	int32_t maxLifeStock_ = 3;
	int32_t lifeStock_ = 3;
	int32_t exp_ = 0;
	int32_t totalExp_ = 0;
	int32_t attackPower_ = 1;
	int32_t moveSpeedLevel_ = 0;
	bool levelUpRequested_ = false;

	std::vector<std::unique_ptr<NormalBullet>> normalBullets_;
	bool hasNormalBullets_ = true;
	float normalBulletInterval_ = 0.85f;
	float normalBulletTimer_ = 0.0f;
	int32_t normalBulletLevel_ = 1;
	int32_t normalBulletAmount_ = 1;
	int32_t normalBulletDamageBonus_ = 0;
	int32_t normalBulletPierceCount_ = 1;
	float normalBulletSpeed_ = 1.0f;
	float normalBulletRange_ = 30.0f;
	size_t peakNormalBulletCount_ = 0;
	size_t normalBulletPruneCount_ = 0;

	int32_t maxLifeStockCap_ = 6;
	int32_t moveSpeedUpgradeCap_ = 5;
	float moveSpeedUpgradeStep_ = 3.0f;
	float moveSpeedMax_ = 45.0f;
	float normalBulletUpgradeMultiplier_ = 0.84f;
	float normalBulletMinInterval_ = 0.18f;

	std::vector<std::unique_ptr<OrbitBullet>> orbitBullets_;
	bool hasOrbitBullets_ = false;
	int32_t orbitBulletLevel_ = 0;
	int32_t orbitBulletCount_ = 1;
	float orbitRadius_ = 10.0f;
	float orbitRadiusUpgradeStep_ = 2.0f;
	float orbitAngularSpeed_ = 0.03f;
	float orbitAngularSpeedUpgradeStep_ = 0.01f;
	float orbitBulletScale_ = 1.0f;
	float orbitBulletScaleUpgradeStep_ = 0.2f;
	float orbitHitInterval_ = 0.5f;
	float orbitHitIntervalUpgradeMultiplier_ = 0.8f;

	std::unique_ptr<Drone> drone_;
	bool hasDrone_ = false;
	int32_t droneLevel_ = 0;
	int32_t droneShotCount_ = 1;
	int32_t droneDamageBonus_ = 0;
	int32_t dronePierceCount_ = 1;
	float droneInterval_ = 2.0f;
	float droneTimer_ = 0.0f;
	float droneUpgradeMultiplier_ = 0.8f;
	float droneBulletSpeed_ = 1.0f;
	float droneBulletRange_ = 30.0f;

	bool hasLightning_ = false;
	int32_t lightningLevel_ = 0;
	int32_t lightningStrikeCount_ = 1;
	int32_t lightningDamageBonus_ = 0;
	float lightningRadius_ = 6.0f;
	float lightningInterval_ = 2.4f;
	float lightningTimer_ = 0.0f;
	std::vector<Vector3> lightningEffectTargets_;
	float lightningEffectTimer_ = 0.0f;

	std::unordered_map<std::string, float> weaponUpgradeSettings_;
};

} // namespace DirectXGame
