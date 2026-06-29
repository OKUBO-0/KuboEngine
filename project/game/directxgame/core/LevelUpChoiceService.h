#pragma once

#include <cstddef>
#include <string>
#include <vector>
#include "PassiveItemType.h"
#include "WeaponType.h"

namespace DirectXGame {

class PlayerManager;

enum class LevelUpUpgrade {
	Normal,
	Orbit,
	Drone,
	Lightning,
	Explosive,
	Attack,
	MaxHp,
	MoveSpeed,
	Heal,
};

enum class LevelUpChoiceCategory {
	Weapon,
	PassiveItem,
};

struct LevelUpChoice {
	LevelUpUpgrade upgrade = LevelUpUpgrade::Attack;
	LevelUpChoiceCategory category = LevelUpChoiceCategory::PassiveItem;
	WeaponType weaponType = WeaponType::NormalBullet;
	PassiveItemType passiveItemType = PassiveItemType::Attack;
	std::string texturePath;
	std::string iconPath;
	std::string titleText;
	std::string detailText;
};

class LevelUpChoiceService final {
public:
	static std::vector<LevelUpChoice> Build(
		const PlayerManager& playerManager,
		size_t maxChoices);
	static void Apply(PlayerManager& playerManager, LevelUpUpgrade upgrade);
};

}
