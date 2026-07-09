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
};

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

	void Update(float deltaTime, const Vector3& playerPosition,
		const Vector3& aimDirection, const PlayerStats& stats)
	{
		if (!active_) return;
		timer_ += deltaTime;
		const WeaponRuntimeStats runtime = stats.Resolve({
			config_.baseDamage + damageBonus_, interval_, speed_, range_,
			scale_, count_ }, GetWeaponStatApplicability(config_.weaponType));
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
			auto bullet = std::make_unique<NormalBullet>();
			bullet->InitializeForward(position, direction, runtime.projectileSpeed,
				runtime.duration, hitCount_ + config_.hitCountOffset, runtime.areaSize,
				config_.movementMode);
			bullets_.push_back(std::move(bullet));
		}
	}

	AutoProjectileWeaponConfig config_;
	std::vector<std::unique_ptr<NormalBullet>> bullets_;
	bool active_ = false;
	int32_t level_ = 0;
	int32_t damageBonus_ = 0;
	int32_t count_ = 1;
	int32_t hitCount_ = config_.hitCount;
	float timer_ = 0.0f;
	float interval_ = config_.interval;
	float speed_ = config_.speed;
	float range_ = config_.range;
	float scale_ = config_.scale;
};

class BoneWeapon final : public AutoProjectileWeapon {
public:
	BoneWeapon() : AutoProjectileWeapon({ WeaponType::Bone, "bone", 9.0f, 1.3f, 0.82f,
		36.0f, 1.0f, 2, "bounceCount", 1 }) {}
};

class HandgunWeapon final : public AutoProjectileWeapon {
public:
	HandgunWeapon() : AutoProjectileWeapon({ WeaponType::Handgun, "handgun", 11.0f, 0.48f,
		1.65f, 48.0f, 0.72f, 0, "ricochetCount", 1 }) {}
};

class BoomerangWeapon final : public AutoProjectileWeapon {
public:
	BoomerangWeapon() : AutoProjectileWeapon({ WeaponType::Boomerang, "boomerang", 8.0f, 1.55f,
		0.78f, 38.0f, 1.15f, 8, "maxHits", 0,
		NormalBullet::MovementMode::ReturnToPlayer }) {}
};

} // namespace DirectXGame
