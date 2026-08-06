#pragma once

#include <cstddef>
#include <string>
#include <vector>
#include "PassiveItemType.h"
#include "WeaponType.h"

namespace DirectXGame {

class PlayerManager;

enum class LevelUpUpgrade {
	BowArrow,
	Rock,
	ThunderStaff,
	FlameStaff,
	Sword,
	Aura,
	FlameShoes,
	Bone,
	Handgun,
	Boomerang,
	PassiveItem,
};

enum class LevelUpChoiceCategory {
	Weapon,
	Item,
};

struct LevelUpChoice {
	LevelUpUpgrade upgrade = LevelUpUpgrade::PassiveItem;
	LevelUpChoiceCategory category = LevelUpChoiceCategory::Item;
	WeaponType weaponType = WeaponType::BowArrow;
	PassiveItemType passiveItemType = PassiveItemType::Damage;
	std::string texturePath;
	std::string iconPath;
	std::string subIconPath;
	std::string titleText;
	std::string detailText;
};

class LevelUpChoiceService final {
public:
	static std::vector<LevelUpChoice> Build(
		const PlayerManager& playerManager,
		size_t maxChoices);
	static void Apply(PlayerManager& playerManager, const LevelUpChoice& choice);
};

}
