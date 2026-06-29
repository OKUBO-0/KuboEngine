#pragma once

#include "WeaponType.h"
#include <array>

namespace DirectXGame {

inline constexpr std::array<WeaponType, 4> kUpgradeableWeaponTypes = {
	WeaponType::NormalBullet,
	WeaponType::OrbitBullet,
	WeaponType::Lightning,
	WeaponType::ExplosiveBullet,
};

}
