#pragma once

#include "Vector3.h"
#include "WeaponType.h"
#include "Drone.h"
#include "NormalBullet.h"
#include "OrbitBullet.h"
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace DirectXGame {

class EnemyManager;
class Player;

class PlayerWeaponController final {
public:
	static constexpr int32_t kNormalBulletMaxLevel = 8;
	static constexpr int32_t kOrbitBulletMaxLevel = 8;
	static constexpr int32_t kDroneMaxLevel = 8;
	static constexpr int32_t kLightningMaxLevel = 8;
	static constexpr int32_t kExplosiveBulletMaxLevel = 8;
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
		int32_t attackPower);
	void Draw();

	void UpgradeNormalBullets(Player* player);
	void AddOrbitBullets(Player* player);
	void UpgradeOrbitBullets(Player* player);
	void AddDrone();
	void UpgradeDrone();
	void AddLightning();
	void UpgradeLightning();
	void AddExplosiveBullets();
	void UpgradeExplosiveBullets();
	void UpgradeWeapon(WeaponType type, Player* player);
	void MaxAllWeapons(Player* player);

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
	const std::unique_ptr<Drone>& GetDrone() const { return drone_; }
	const std::vector<Vector3>& GetLightningEffectTargets() const
	{
		return lightningEffectTargets_;
	}
	const std::vector<std::unique_ptr<NormalBullet>>&
		GetExplosiveBullets() const
	{
		return explosiveBullets_;
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
	int32_t GetDroneLevel() const { return droneLevel_; }
	int32_t GetLightningLevel() const { return lightningLevel_; }
	int32_t GetExplosiveBulletLevel() const { return explosiveBulletLevel_; }
	int32_t GetLightningStrikeCount() const
	{
		return lightningStrikeCount_;
	}
	float GetLightningRadius() const { return lightningRadius_; }
	bool HasOrbitBullets() const { return hasOrbitBullets_; }
	bool HasDrone() const { return hasDrone_; }
	bool HasLightning() const { return hasLightning_; }
	bool HasExplosiveBullets() const { return hasExplosiveBullets_; }
	bool IsNormalBulletMaxLevel() const
	{
		return normalBulletLevel_ >= kNormalBulletMaxLevel;
	}
	bool IsOrbitBulletMaxLevel() const
	{
		return hasOrbitBullets_ &&
			orbitBulletLevel_ >= kOrbitBulletMaxLevel;
	}
	bool IsDroneMaxLevel() const
	{
		return hasDrone_ && droneLevel_ >= kDroneMaxLevel;
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
	bool IsWeaponMaxLevel(WeaponType type) const;
	int32_t GetNormalBulletDamage(int32_t attackPower) const
	{
		return attackPower + normalBulletDamageBonus_;
	}
	int32_t GetOrbitBulletDamage(int32_t attackPower) const
	{
		return attackPower;
	}
	int32_t GetDroneDamage(int32_t attackPower) const;
	int32_t GetLightningDamage(int32_t attackPower) const
	{
		return attackPower + lightningDamageBonus_;
	}
	int32_t GetExplosiveBulletDamage(int32_t attackPower) const
	{
		return attackPower + explosiveBulletDamageBonus_;
	}
	float GetExplosiveBulletRadius() const { return explosiveBulletRadius_; }
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
	void UpdateNormalBullets(float deltaTime, Player* player);
	NormalBullet& AcquireNormalBullet();
	void RecycleNormalBullet(size_t index);
	void RecycleInactiveNormalBullets();
	void UpdateOrbitBullets(float deltaTime, Player* player);
	void UpdateDrone(
		float deltaTime,
		Player* player,
		EnemyManager* enemyManager);
	void UpdateLightning(
		float deltaTime,
		EnemyManager* enemyManager,
		int32_t attackPower);
	void UpdateExplosiveBullets(float deltaTime, Player* player);
	NormalBullet& AcquireExplosiveBullet();
	void RecycleExplosiveBullet(size_t index);
	void RecycleInactiveExplosiveBullets();
	void RebuildOrbitBullets(Player* player);
	void ApplyNormalBulletUpgradeLevel(int32_t level);
	void ApplyOrbitUpgradeLevel(int32_t level);
	void ApplyDroneUpgradeLevel(int32_t level);
	void ApplyLightningUpgradeLevel(int32_t level);
	void ApplyExplosiveBulletUpgradeLevel(int32_t level);
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
	size_t peakNormalBulletCount_ = 0;
	size_t normalBulletPruneCount_ = 0;
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

	std::unordered_map<std::string, float> upgradeSettings_;
};

}
