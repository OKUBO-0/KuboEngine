#pragma once

#include "NormalBullet.h"
#include "PlayerStats.h"
#include "WeaponUpgradeData.h"
#include "Vector3.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace DirectXGame {

struct AutoProjectileWeaponConfig {
	WeaponType weaponType = WeaponType::BowArrow;
	std::string prefix;
	float baseDamage = 1.0f;
	float interval = 1.0f;
	float speed = 1.0f;
	float range = 30.0f;
	float scale = 1.0f;
	int32_t hitCount = 1;
	const char* hitCountKey = "maxHits";
	int32_t hitCountOffset = 0;
	NormalBullet::MovementMode movementMode = NormalBullet::MovementMode::Straight;
	bool sequentialBurst = false;
	float burstInterval = 0.075f;
	NormalBullet::VisualStyle visualStyle{};
};

inline Vector3 MakePlayerProjectileSpawnPosition(
	const Vector3& playerPosition,
	const Vector3& direction,
	float forwardOffset = 0.86f,
	float heightOffset = 1.28f)
{
	return {
		playerPosition.x + direction.x * forwardOffset,
		playerPosition.y + heightOffset,
		playerPosition.z + direction.z * forwardOffset,
	};
}

class AutoProjectileWeapon {
public:
	explicit AutoProjectileWeapon(AutoProjectileWeaponConfig config)
		: config_(std::move(config)) {}

	void Upgrade(const std::unordered_map<std::string, float>& settings)
	{
		if (!active_) { active_ = true; level_ = 1; }
		else if (level_ >= kMaxLevel) return;
		else ++level_;
		ApplyLevel(settings);
	}

	void DebugReset()
	{
		bullets_.clear();
		recentShotPositions_.clear();
		recentReloadPositions_.clear();
		active_ = false;
		level_ = 0;
		damageBonus_ = 0;
		count_ = 1;
		hitCount_ = config_.hitCount;
		timer_ = 0.0f;
		burstShotsRemaining_ = 0;
		burstTimer_ = 0.0f;
		burstInterval_ = config_.burstInterval;
		interval_ = config_.interval;
		speed_ = config_.speed;
		range_ = config_.range;
		scale_ = config_.scale;
	}

	void DebugSetLevel(
		const std::unordered_map<std::string, float>& settings,
		int32_t level)
	{
		const int32_t targetLevel = std::clamp(level, 0, kMaxLevel);
		DebugReset();
		for (int32_t currentLevel = 0; currentLevel < targetLevel; ++currentLevel) {
			Upgrade(settings);
		}
	}

	void DebugFire(
		const Vector3& playerPosition,
		const Vector3& aimDirection,
		const PlayerStats& stats)
	{
		if (!active_) {
			return;
		}
		WeaponRuntimeStats runtime = stats.Resolve({
			config_.baseDamage + damageBonus_,
			interval_,
			speed_,
			range_,
			scale_,
			count_,
			}, GetWeaponStatApplicability(config_.weaponType));
		runtime.projectileCount = (std::max)(1, runtime.projectileCount);
		SpawnFan(playerPosition, aimDirection, runtime);
	}

	void Update(float deltaTime, const Vector3& playerPosition,
		const Vector3& aimDirection, const PlayerStats& stats)
	{
		recentShotPositions_.clear();
		recentReloadPositions_.clear();
		if (!active_) return;
		timer_ += deltaTime;
		burstTimer_ += deltaTime;
		const WeaponRuntimeStats runtime = stats.Resolve({
			config_.baseDamage + damageBonus_, interval_, speed_, range_,
			scale_, count_ }, GetWeaponStatApplicability(config_.weaponType));
		if (config_.sequentialBurst) {
			if (timer_ >= runtime.interval && burstShotsRemaining_ <= 0) {
				burstShotsRemaining_ = runtime.projectileCount;
				burstTimer_ = burstInterval_;
				timer_ = std::fmod(timer_, runtime.interval);
			}
			const WeaponStatApplicability applicability =
				GetWeaponStatApplicability(config_.weaponType);
			const float burstInterval = burstInterval_ /
				(std::max)(0.1f, applicability.attackSpeed
					? stats.GetAttackSpeedMultiplier()
					: 1.0f);
			if (burstShotsRemaining_ > 0 && burstTimer_ >= burstInterval) {
				WeaponRuntimeStats singleShotRuntime = runtime;
				singleShotRuntime.projectileCount = 1;
				SpawnFan(playerPosition, aimDirection, singleShotRuntime);
				--burstShotsRemaining_;
				if (burstShotsRemaining_ <= 0) {
					recentReloadPositions_.push_back(
						playerPosition + Vector3{ 0.0f, 1.18f, 0.0f });
				}
				burstTimer_ = std::fmod(burstTimer_, burstInterval);
			}
			for (auto& bullet : bullets_) bullet->Update(playerPosition, deltaTime);
			std::erase_if(bullets_, [](const auto& bullet) {
				return !bullet || !bullet->IsActive();
			});
			return;
		}
		if (timer_ >= runtime.interval) {
			SpawnFan(playerPosition, aimDirection, runtime);
			timer_ = std::fmod(timer_, runtime.interval);
		}
		for (auto& bullet : bullets_) bullet->Update(playerPosition, deltaTime);
		std::erase_if(bullets_, [](const auto& bullet) {
			return !bullet || !bullet->IsActive();
		});
	}

	void Draw()
	{
		for (auto& bullet : bullets_) bullet->Draw();
	}

	void AppendRenderObjects(
		std::vector<Engine::Graphics3D::Object3D*>& objects) const
	{
		for (const std::unique_ptr<NormalBullet>& bullet : bullets_) {
			if (!bullet) {
				continue;
			}
			if (Engine::Graphics3D::Object3D* object =
					bullet->GetRenderObject()) {
				objects.push_back(object);
			}
		}
	}

	bool IsActive() const { return active_; }
	bool IsMaxLevel() const { return active_ && level_ >= kMaxLevel; }
	int32_t GetLevel() const { return level_; }
	int32_t GetDamage(const PlayerStats& stats) const
	{
		return stats.Resolve(
			{ config_.baseDamage + damageBonus_ },
			GetWeaponStatApplicability(config_.weaponType)).damage;
	}
	const std::vector<std::unique_ptr<NormalBullet>>& GetBullets() const
	{
		return bullets_;
	}
	const std::vector<Vector3>& GetRecentShotPositions() const
	{
		return recentShotPositions_;
	}
	const std::vector<Vector3>& GetRecentReloadPositions() const
	{
		return recentReloadPositions_;
	}

private:
	static constexpr int32_t kMaxLevel = 8;

	float Setting(const std::unordered_map<std::string, float>& settings,
		const std::string& suffix, float fallback) const
	{
		const std::string key = config_.prefix + ".lv" +
			std::to_string(level_) + "." + suffix;
		const auto found = settings.find(key);
		return found == settings.end() ? fallback : found->second;
	}

	void ApplyLevel(const std::unordered_map<std::string, float>& settings)
	{
		damageBonus_ += static_cast<int32_t>(Setting(settings, "damageBonusAdd", 0.0f));
		count_ = static_cast<int32_t>(Setting(settings, "count", static_cast<float>(count_)));
		hitCount_ = static_cast<int32_t>(Setting(
			settings, config_.hitCountKey, static_cast<float>(hitCount_)));
		speed_ *= Setting(settings, "speedMultiplier", 1.0f);
		scale_ *= Setting(settings, "scaleMultiplier", 1.0f);
		interval_ *= Setting(settings, "intervalMultiplier", 1.0f);
		burstInterval_ *= Setting(settings, "burstIntervalMultiplier", 1.0f);
	}

	void SpawnFan(const Vector3& position, const Vector3& forward,
		const WeaponRuntimeStats& runtime)
	{
		const Vector3 right{ forward.z, 0.0f, -forward.x };
		const float center = static_cast<float>(runtime.projectileCount - 1) * 0.5f;
		for (int32_t index = 0; index < runtime.projectileCount; ++index) {
			const float angle = (static_cast<float>(index) - center) * 0.13f;
			const float cosine = std::cos(angle);
			const float sine = std::sin(angle);
			const Vector3 direction{
				forward.x * cosine + right.x * sine, 0.0f,
				forward.z * cosine + right.z * sine };
			const Vector3 spawnPosition =
				MakePlayerProjectileSpawnPosition(position, direction);
			auto bullet = std::make_unique<NormalBullet>();
			bullet->InitializeForward(spawnPosition, direction, runtime.projectileSpeed,
				runtime.duration, hitCount_ + config_.hitCountOffset, runtime.areaSize,
				config_.movementMode, config_.visualStyle);
			bullets_.push_back(std::move(bullet));
			recentShotPositions_.push_back(
				spawnPosition + direction * 0.35f);
		}
	}

	AutoProjectileWeaponConfig config_;
	std::vector<std::unique_ptr<NormalBullet>> bullets_;
	std::vector<Vector3> recentShotPositions_;
	std::vector<Vector3> recentReloadPositions_;
	bool active_ = false;
	int32_t level_ = 0;
	int32_t damageBonus_ = 0;
	int32_t count_ = 1;
	int32_t hitCount_ = config_.hitCount;
	float timer_ = 0.0f;
	int32_t burstShotsRemaining_ = 0;
	float burstTimer_ = 0.0f;
	float burstInterval_ = config_.burstInterval;
	float interval_ = config_.interval;
	float speed_ = config_.speed;
	float range_ = config_.range;
	float scale_ = config_.scale;
};

class BoneWeapon final : public AutoProjectileWeapon {
public:
	BoneWeapon() : AutoProjectileWeapon({ WeaponType::Bone, "bone", 11.0f, 1.25f, 0.55f,
		36.0f, 1.0f, 1, "bounceCount", 1,
		NormalBullet::MovementMode::Straight, false, 0.075f,
		{ "quaternius_weapons/bone.glb", { 1.0f, 0.96f, 0.82f, 1.0f }, { 210.0f, 210.0f, 210.0f }, { 0.0f, 1.57079633f, 0.0f }, 0.68f } }) {}
};

class HandgunWeapon final : public AutoProjectileWeapon {
public:
	HandgunWeapon() : AutoProjectileWeapon({ WeaponType::Handgun, "handgun", 5.0f, 1.85f,
		2.0f, 48.0f, 0.72f, 0, "ricochetCount", 1,
		NormalBullet::MovementMode::Straight, true, 0.20f,
		{ "pistol_bullet.glb", { 1.0f, 0.88f, 0.25f, 1.0f }, { 760.0f, 760.0f, 1180.0f }, { 0.0f, 1.57079633f, 0.0f }, 0.56f } }) {}
};

class BoomerangWeapon final : public AutoProjectileWeapon {
public:
	BoomerangWeapon() : AutoProjectileWeapon({ WeaponType::Boomerang, "boomerang", 9.0f, 1.45f,
		0.95f, 38.0f, 1.0f, 8, "maxHits", 0,
		NormalBullet::MovementMode::ReturnToPlayer, false, 0.075f,
		{ "quaternius_weapons/axe_small.glb", { 0.86f, 1.0f, 0.76f, 1.0f }, { 210.0f, 210.0f, 210.0f }, { 0.0f, 0.0f, 1.57079633f }, 0.72f, true } }) {}
};

} // namespace DirectXGame
