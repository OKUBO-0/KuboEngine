#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace DirectXGame {

enum class PlayerStatType {
	Damage,
	AttackSpeed,
	Duration,
	MovementSpeed,
	ProjectileSpeed,
	AreaSize,
	ProjectileCount,
	PickupRange,
	ExpGain,
	CoinGain,
	CritChance,
	CritDamage,
	Armor,
	Evasion,
	HpRegen,
	LifeSteal,
};

// Additive bonuses are kept by source so character, permanent shop upgrades,
// and upgrades acquired during a run never overwrite one another.
struct PlayerStatModifiers {
	float damage = 0.0f;
	float attackSpeed = 0.0f;
	float duration = 0.0f;
	float movementSpeed = 0.0f;
	float projectileSpeed = 0.0f;
	float areaSize = 0.0f;
	float pickupRange = 0.0f;
	float expGain = 0.0f;
	float coinGain = 0.0f;
	float critChance = 0.0f;
	float critDamage = 0.0f;
	float armor = 0.0f;
	float evasion = 0.0f;
	float hpRegen = 0.0f;
	float lifeSteal = 0.0f;
	int32_t projectileCount = 0;
};

struct WeaponBaseStats {
	float damage = 1.0f;
	float interval = 1.0f;
	float projectileSpeed = 1.0f;
	float duration = 1.0f;
	float areaSize = 1.0f;
	int32_t projectileCount = 1;
};

struct WeaponRuntimeStats {
	int32_t damage = 1;
	float interval = 1.0f;
	float projectileSpeed = 1.0f;
	float duration = 1.0f;
	float areaSize = 1.0f;
	int32_t projectileCount = 1;
};

struct DamageResult {
	int32_t damage = 1;
	int32_t criticalTier = 0;
	bool IsCritical() const { return criticalTier > 0; }
};

class PlayerStats final {
public:
	void SetBaseMovementSpeed(float value)
	{
		baseMovementSpeed_ = (std::max)(0.1f, value);
	}
	float GetMovementSpeed() const
	{
		return baseMovementSpeed_ * GetMovementSpeedMultiplier();
	}
	float GetBaseMovementSpeed() const { return baseMovementSpeed_; }
	void AddRunModifier(PlayerStatType type, float amount)
	{
		AddModifier(run_, type, amount);
	}
	void AddPermanentModifier(PlayerStatType type, float amount)
	{
		AddModifier(permanent_, type, amount);
	}
	const PlayerStatModifiers& GetCharacterModifiers() const { return character_; }
	const PlayerStatModifiers& GetPermanentModifiers() const { return permanent_; }
	const PlayerStatModifiers& GetRunModifiers() const { return run_; }
	PlayerStatModifiers& CharacterModifiers() { return character_; }
	PlayerStatModifiers& PermanentModifiers() { return permanent_; }
	PlayerStatModifiers& RunModifiers() { return run_; }

	float GetDamageMultiplier() const { return CappedMultiplier(&PlayerStatModifiers::damage, 10.0f, 100.0f); }
	float GetAttackSpeedMultiplier() const { return CappedMultiplier(&PlayerStatModifiers::attackSpeed, 3.0f, 10.0f); }
	float GetDurationMultiplier() const { return CappedMultiplier(&PlayerStatModifiers::duration, 3.0f, 10.0f); }
	float GetMovementSpeedMultiplier() const { return CappedMultiplier(&PlayerStatModifiers::movementSpeed, 2.0f, 3.0f); }
	float GetProjectileSpeedMultiplier() const { return CappedMultiplier(&PlayerStatModifiers::projectileSpeed, 3.0f, 8.0f); }
	float GetAreaSizeMultiplier() const { return CappedMultiplier(&PlayerStatModifiers::areaSize, 3.0f, 8.0f); }
	float GetPickupRangeMultiplier() const { return CappedMultiplier(&PlayerStatModifiers::pickupRange, 3.0f, 5.0f); }
	float GetExpGainMultiplier() const { return CappedMultiplier(&PlayerStatModifiers::expGain, 5.0f, 10.0f); }
	float GetCoinGainMultiplier() const { return CappedMultiplier(&PlayerStatModifiers::coinGain, 5.0f, 10.0f); }
	float GetCritChance() const { return std::clamp(Sum(&PlayerStatModifiers::critChance), 0.0f, 5.0f); }
	float GetCritDamageMultiplier() const
	{
		return (std::max)(1.0f, 1.5f + Sum(&PlayerStatModifiers::critDamage));
	}
	float GetArmorReduction() const
	{
		return (std::min)(0.8f,
			1.0f - std::exp(-(std::max)(0.0f, Sum(&PlayerStatModifiers::armor))));
	}
	float GetEvasionChance() const
	{
		return (std::min)(0.75f,
			1.0f - std::exp(-(std::max)(0.0f, Sum(&PlayerStatModifiers::evasion))));
	}
	float GetHpRegenPerSecond() const
	{
		return std::clamp(Sum(&PlayerStatModifiers::hpRegen), 0.0f, 20.0f);
	}
	float GetLifeStealChance() const
	{
		return std::clamp(Sum(&PlayerStatModifiers::lifeSteal), 0.0f, 3.0f);
	}
	int32_t GetProjectileCountBonus() const
	{
		return character_.projectileCount + permanent_.projectileCount +
			run_.projectileCount;
	}

	WeaponRuntimeStats Resolve(const WeaponBaseStats& base) const
	{
		return {
			(std::max)(1, static_cast<int32_t>(std::lround(base.damage * GetDamageMultiplier()))),
			(std::max)(0.05f, base.interval / GetAttackSpeedMultiplier()),
			(std::max)(0.01f, base.projectileSpeed * GetProjectileSpeedMultiplier()),
			(std::max)(0.01f, base.duration * GetDurationMultiplier()),
			(std::max)(0.05f, base.areaSize * GetAreaSizeMultiplier()),
			(std::max)(1, base.projectileCount + GetProjectileCountBonus()),
		};
	}

	DamageResult ResolveHit(int32_t baseDamage, float randomUnit) const
	{
		const float chance = (std::max)(0.0f, GetCritChance());
		int32_t tier = static_cast<int32_t>(chance);
		const float remainder = chance - static_cast<float>(tier);
		if (std::clamp(randomUnit, 0.0f, 1.0f) < remainder) {
			++tier;
		}
		float damage = static_cast<float>((std::max)(1, baseDamage));
		for (int32_t index = 0; index < tier; ++index) {
			damage *= GetCritDamageMultiplier();
		}
		return {
			(std::max)(1, static_cast<int32_t>(std::lround(damage))), tier };
	}

private:
	static void AddModifier(
		PlayerStatModifiers& modifiers,
		PlayerStatType type,
		float amount)
	{
		switch (type) {
		case PlayerStatType::Damage: modifiers.damage += amount; break;
		case PlayerStatType::AttackSpeed: modifiers.attackSpeed += amount; break;
		case PlayerStatType::Duration: modifiers.duration += amount; break;
		case PlayerStatType::MovementSpeed: modifiers.movementSpeed += amount; break;
		case PlayerStatType::ProjectileSpeed: modifiers.projectileSpeed += amount; break;
		case PlayerStatType::AreaSize: modifiers.areaSize += amount; break;
		case PlayerStatType::ProjectileCount:
			modifiers.projectileCount += static_cast<int32_t>(std::lround(amount));
			break;
		case PlayerStatType::PickupRange: modifiers.pickupRange += amount; break;
		case PlayerStatType::ExpGain: modifiers.expGain += amount; break;
		case PlayerStatType::CoinGain: modifiers.coinGain += amount; break;
		case PlayerStatType::CritChance: modifiers.critChance += amount; break;
		case PlayerStatType::CritDamage: modifiers.critDamage += amount; break;
		case PlayerStatType::Armor: modifiers.armor += amount; break;
		case PlayerStatType::Evasion: modifiers.evasion += amount; break;
		case PlayerStatType::HpRegen: modifiers.hpRegen += amount; break;
		case PlayerStatType::LifeSteal: modifiers.lifeSteal += amount; break;
		}
	}
	using FloatMember = float PlayerStatModifiers::*;
	float Sum(FloatMember member) const
	{
		return character_.*member + permanent_.*member + run_.*member;
	}
	float Multiplier(FloatMember member) const
	{
		return (std::max)(0.1f, 1.0f + Sum(member));
	}
	float CappedMultiplier(
		FloatMember member,
		float softCap,
		float hardCap) const
	{
		float value = Multiplier(member);
		if (value > softCap) {
			value = softCap + (value - softCap) * 0.35f;
		}
		return std::clamp(value, 0.1f, hardCap);
	}

	PlayerStatModifiers character_{};
	PlayerStatModifiers permanent_{};
	PlayerStatModifiers run_{};
	float baseMovementSpeed_ = 30.0f;
};

} // namespace DirectXGame
