#pragma once

#include "game/directxgame/player/WeaponType.h"
#include <array>

namespace DirectXGame {

inline constexpr std::array<WeaponType, 4> kUpgradeableWeaponTypes = {
	WeaponType::NormalBullet,
	WeaponType::OrbitBullet,
	WeaponType::Drone,
	WeaponType::Lightning,
};

}
