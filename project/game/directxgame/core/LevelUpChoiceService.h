#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace DirectXGame {

class PlayerManager;

enum class LevelUpUpgrade {
	Normal,
	Orbit,
	Drone,
	Lightning,
	Attack,
	MaxHp,
	MoveSpeed,
	Heal,
};

struct LevelUpChoice {
	LevelUpUpgrade upgrade = LevelUpUpgrade::Attack;
	std::string texturePath;
	std::string iconPath;
};

class LevelUpChoiceService final {
public:
	static std::vector<LevelUpChoice> Build(
		const PlayerManager& playerManager,
		size_t maxChoices);
	static void Apply(PlayerManager& playerManager, LevelUpUpgrade upgrade);
};

}
