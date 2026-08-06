#pragma once

#include "PlayerStats.h"
#include "WeaponType.h"
#include <array>
#include <cstddef>
#include <string_view>

namespace DirectXGame {

enum class WeaponUpgradeRarity {
	Common,
	Rare,
	Epic,
};

struct WeaponUpgradeLevelMetadata {
	WeaponUpgradeRarity rarity = WeaponUpgradeRarity::Common;
	std::string_view statTags;
};

inline constexpr std::array<WeaponType, 10> kUpgradeableWeaponTypes = {
	WeaponType::BowArrow,
	WeaponType::Rock,
	WeaponType::ThunderStaff,
	WeaponType::FlameStaff,
	WeaponType::Sword,
	WeaponType::Aura,
	WeaponType::FlameShoes,
	WeaponType::Bone,
	WeaponType::Handgun,
	WeaponType::Boomerang,
};

inline constexpr std::array<WeaponUpgradeLevelMetadata, 9> kBowArrowUpgradeMetadata{ {
	{},
	{ WeaponUpgradeRarity::Common, "Core" },
	{ WeaponUpgradeRarity::Common, "Pierce" },
	{ WeaponUpgradeRarity::Common, "Damage / Cooldown" },
	{ WeaponUpgradeRarity::Rare, "Quantity" },
	{ WeaponUpgradeRarity::Rare, "Projectile Speed / Size" },
	{ WeaponUpgradeRarity::Common, "Damage / Cooldown" },
	{ WeaponUpgradeRarity::Epic, "Quantity / Pierce" },
	{ WeaponUpgradeRarity::Rare, "Damage / Projectile Speed / Cooldown" },
} };

inline constexpr std::array<WeaponUpgradeLevelMetadata, 9> kRockUpgradeMetadata{ {
	{},
	{ WeaponUpgradeRarity::Common, "Core" },
	{ WeaponUpgradeRarity::Rare, "Quantity" },
	{ WeaponUpgradeRarity::Rare, "Projectile Speed / Size" },
	{ WeaponUpgradeRarity::Common, "Damage / Hit Rate" },
	{ WeaponUpgradeRarity::Rare, "Quantity" },
	{ WeaponUpgradeRarity::Rare, "Projectile Speed / Size" },
	{ WeaponUpgradeRarity::Common, "Hit Rate" },
	{ WeaponUpgradeRarity::Epic, "Quantity / Damage" },
} };

inline constexpr std::array<WeaponUpgradeLevelMetadata, 9> kThunderStaffUpgradeMetadata{ {
	{},
	{ WeaponUpgradeRarity::Common, "Core" },
	{ WeaponUpgradeRarity::Common, "Damage" },
	{ WeaponUpgradeRarity::Rare, "Quantity" },
	{ WeaponUpgradeRarity::Common, "Size" },
	{ WeaponUpgradeRarity::Common, "Damage / Cooldown" },
	{ WeaponUpgradeRarity::Rare, "Quantity" },
	{ WeaponUpgradeRarity::Rare, "Cooldown" },
	{ WeaponUpgradeRarity::Epic, "Quantity / Size" },
} };

inline constexpr std::array<WeaponUpgradeLevelMetadata, 9> kFlameStaffUpgradeMetadata{ {
	{},
	{ WeaponUpgradeRarity::Common, "Core" },
	{ WeaponUpgradeRarity::Common, "Damage" },
	{ WeaponUpgradeRarity::Common, "Size" },
	{ WeaponUpgradeRarity::Rare, "Quantity" },
	{ WeaponUpgradeRarity::Common, "Damage / Cooldown" },
	{ WeaponUpgradeRarity::Rare, "Size" },
	{ WeaponUpgradeRarity::Common, "Projectile Speed / Fire Rate" },
	{ WeaponUpgradeRarity::Epic, "Quantity / Damage / Cooldown" },
} };

inline constexpr std::array<WeaponUpgradeLevelMetadata, 9> kSwordUpgradeMetadata{ {
	{},
	{ WeaponUpgradeRarity::Common, "Core" },
	{ WeaponUpgradeRarity::Common, "Damage / Cooldown" },
	{ WeaponUpgradeRarity::Common, "Size" },
	{ WeaponUpgradeRarity::Rare, "Cooldown / Quantity" },
	{ WeaponUpgradeRarity::Common, "Damage" },
	{ WeaponUpgradeRarity::Rare, "Size / Knockback" },
	{ WeaponUpgradeRarity::Common, "Size" },
	{ WeaponUpgradeRarity::Epic, "Damage / Quantity / Knockback" },
} };

inline constexpr std::array<WeaponUpgradeLevelMetadata, 9> kAuraUpgradeMetadata{ {
	{},
	{ WeaponUpgradeRarity::Common, "Core" },
	{ WeaponUpgradeRarity::Common, "Damage" },
	{ WeaponUpgradeRarity::Common, "Size" },
	{ WeaponUpgradeRarity::Rare, "Cooldown" },
	{ WeaponUpgradeRarity::Common, "Damage / Cooldown" },
	{ WeaponUpgradeRarity::Rare, "Size" },
	{ WeaponUpgradeRarity::Rare, "Cooldown" },
	{ WeaponUpgradeRarity::Epic, "Damage" },
} };

inline constexpr std::array<WeaponUpgradeLevelMetadata, 9> kFlameShoesUpgradeMetadata{ {
	{},
	{ WeaponUpgradeRarity::Common, "Core" },
	{ WeaponUpgradeRarity::Common, "Damage" },
	{ WeaponUpgradeRarity::Common, "Size" },
	{ WeaponUpgradeRarity::Rare, "Duration / Quantity" },
	{ WeaponUpgradeRarity::Common, "Damage / Spawn Rate" },
	{ WeaponUpgradeRarity::Rare, "Duration / Spawn Rate" },
	{ WeaponUpgradeRarity::Rare, "Cooldown" },
	{ WeaponUpgradeRarity::Epic, "Damage / Size / Quantity" },
} };

inline constexpr std::array<WeaponUpgradeLevelMetadata, 9> kBoneUpgradeMetadata{ {
	{},
	{ WeaponUpgradeRarity::Common, "Core" },
	{ WeaponUpgradeRarity::Common, "Projectile Speed" },
	{ WeaponUpgradeRarity::Rare, "Bounce" },
	{ WeaponUpgradeRarity::Common, "Damage" },
	{ WeaponUpgradeRarity::Rare, "Cooldown" },
	{ WeaponUpgradeRarity::Rare, "Bounce" },
	{ WeaponUpgradeRarity::Common, "Damage" },
	{ WeaponUpgradeRarity::Epic, "Quantity / Damage / Cooldown" },
} };

inline constexpr std::array<WeaponUpgradeLevelMetadata, 9> kHandgunUpgradeMetadata{ {
	{},
	{ WeaponUpgradeRarity::Common, "Core / Quantity" },
	{ WeaponUpgradeRarity::Common, "Cooldown" },
	{ WeaponUpgradeRarity::Rare, "Ricochet" },
	{ WeaponUpgradeRarity::Common, "Damage" },
	{ WeaponUpgradeRarity::Common, "Projectile Speed" },
	{ WeaponUpgradeRarity::Rare, "Ricochet" },
	{ WeaponUpgradeRarity::Common, "Damage / Cooldown" },
	{ WeaponUpgradeRarity::Epic, "Quantity / Damage" },
} };

inline constexpr std::array<WeaponUpgradeLevelMetadata, 9> kBoomerangUpgradeMetadata{ {
	{},
	{ WeaponUpgradeRarity::Common, "Core" },
	{ WeaponUpgradeRarity::Common, "Size" },
	{ WeaponUpgradeRarity::Rare, "Hits" },
	{ WeaponUpgradeRarity::Common, "Damage" },
	{ WeaponUpgradeRarity::Rare, "Cooldown" },
	{ WeaponUpgradeRarity::Rare, "Hits" },
	{ WeaponUpgradeRarity::Common, "Damage" },
	{ WeaponUpgradeRarity::Epic, "Quantity / Damage / Cooldown" },
} };

inline constexpr WeaponUpgradeLevelMetadata GetWeaponUpgradeMetadata(
	WeaponType type,
	int32_t level)
{
	const size_t index = static_cast<size_t>(level >= 1 && level <= 8 ? level : 0);
	switch (type) {
	case WeaponType::BowArrow: return kBowArrowUpgradeMetadata[index];
	case WeaponType::Rock: return kRockUpgradeMetadata[index];
	case WeaponType::ThunderStaff: return kThunderStaffUpgradeMetadata[index];
	case WeaponType::FlameStaff: return kFlameStaffUpgradeMetadata[index];
	case WeaponType::Sword: return kSwordUpgradeMetadata[index];
	case WeaponType::Aura: return kAuraUpgradeMetadata[index];
	case WeaponType::FlameShoes: return kFlameShoesUpgradeMetadata[index];
	case WeaponType::Bone: return kBoneUpgradeMetadata[index];
	case WeaponType::Handgun: return kHandgunUpgradeMetadata[index];
	case WeaponType::Boomerang: return kBoomerangUpgradeMetadata[index];
	}
	return {};
}

inline constexpr std::string_view ToString(WeaponUpgradeRarity rarity)
{
	switch (rarity) {
	case WeaponUpgradeRarity::Common: return "Common";
	case WeaponUpgradeRarity::Rare: return "Rare";
	case WeaponUpgradeRarity::Epic: return "Epic";
	}
	return "Common";
}

inline constexpr WeaponStatApplicability GetWeaponStatApplicability(
	WeaponType type)
{
	switch (type) {
	case WeaponType::BowArrow:
		return { true, true, false, true, true, true };
	case WeaponType::Rock:
		return { true, true, false, true, true, true };
	case WeaponType::ThunderStaff:
		return { true, true, true, false, true, true };
	case WeaponType::FlameStaff:
		return { true, true, true, true, true, true };
	case WeaponType::Sword:
		return { true, true, false, false, true, true };
	case WeaponType::Aura:
		return { true, true, false, false, true, false };
	case WeaponType::FlameShoes:
		return { true, true, true, false, true, true };
	case WeaponType::Bone:
		return { true, true, true, true, true, true };
	case WeaponType::Handgun:
		return { true, true, true, true, false, true };
	case WeaponType::Boomerang:
		return { true, true, true, true, true, true };
	}
	return {};
}

}
