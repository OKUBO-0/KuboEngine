#pragma once

#include "WeaponType.h"
#include <array>

namespace DirectXGame {

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

}
