#include "game/directxgame/core/LevelUpChoiceService.h"
#include "game/directxgame/player/PlayerManager.h"
#include <algorithm>
#include <random>
#include <utility>

namespace DirectXGame {

namespace {

std::string WeaponLevelTexturePath(
	const char* weaponDirectory,
	int32_t nextLevel)
{
	return std::string("ui/game/") +
		weaponDirectory +
		"/lv" +
		std::to_string(nextLevel) +
		".png";
}

}

std::vector<LevelUpChoice> LevelUpChoiceService::Build(
	const PlayerManager& playerManager,
	size_t maxChoices)
{
	std::vector<LevelUpChoice> choices;
	choices.reserve(8);
	const auto addChoice = [&choices](
		LevelUpUpgrade upgrade,
		std::string texturePath,
		std::string iconPath) {
			choices.push_back({
				upgrade,
				std::move(texturePath),
				std::move(iconPath),
			});
		};

	if (!playerManager.IsNormalBulletMaxLevel()) {
		addChoice(
			LevelUpUpgrade::Normal,
			WeaponLevelTexturePath(
				"normal",
				playerManager.GetNormalBulletLevel() + 1),
			"ui/game/normal/icon.png");
	}
	if (!playerManager.IsOrbitBulletMaxLevel()) {
		addChoice(
			LevelUpUpgrade::Orbit,
			playerManager.HasOrbitBullets() ?
				WeaponLevelTexturePath(
					"orbit",
					playerManager.GetOrbitBulletLevel() + 1) :
				"ui/game/orbit/add.png",
			"ui/game/orbit/icon.png");
	}
	if (!playerManager.IsDroneMaxLevel()) {
		addChoice(
			LevelUpUpgrade::Drone,
			playerManager.HasDrone() ?
				WeaponLevelTexturePath(
					"drone",
					playerManager.GetDroneLevel() + 1) :
				"ui/game/drone/add.png",
			"ui/game/drone/icon.png");
	}
	if (!playerManager.IsLightningMaxLevel()) {
		addChoice(
			LevelUpUpgrade::Lightning,
			playerManager.HasLightning() ?
				WeaponLevelTexturePath(
					"lightning",
					playerManager.GetLightningLevel() + 1) :
				"ui/game/lightning/add.png",
			"ui/game/lightning/icon.png");
	}
	addChoice(
		LevelUpUpgrade::Attack,
		"ui/game/lvup_attack.png",
		"ui/game/lvup_attack_icon.png");
	addChoice(
		LevelUpUpgrade::MaxHp,
		"ui/game/lvup_maxhp.png",
		"ui/game/lvup_maxhp_icon.png");
	addChoice(
		LevelUpUpgrade::MoveSpeed,
		"ui/game/lvup_speed.png",
		"ui/game/lvup_speed_icon.png");
	if (playerManager.GetHP() < playerManager.GetMaxHP()) {
		addChoice(
			LevelUpUpgrade::Heal,
			"ui/game/lvup_heal.png",
			"ui/game/lvup_heal_icon.png");
	}

	static std::mt19937 rng{ std::random_device{}() };
	std::shuffle(choices.begin(), choices.end(), rng);
	if (choices.size() > maxChoices) {
		choices.resize(maxChoices);
	}
	return choices;
}

void LevelUpChoiceService::Apply(
	PlayerManager& playerManager,
	LevelUpUpgrade upgrade)
{
	switch (upgrade) {
	case LevelUpUpgrade::Normal:
		playerManager.UpgradeNormalBullets();
		break;
	case LevelUpUpgrade::Orbit:
		playerManager.UpgradeOrbitBullets();
		break;
	case LevelUpUpgrade::Drone:
		playerManager.UpgradeDrone();
		break;
	case LevelUpUpgrade::Lightning:
		playerManager.UpgradeLightning();
		break;
	case LevelUpUpgrade::Attack:
		playerManager.UpgradeAttackPower();
		break;
	case LevelUpUpgrade::MaxHp:
		playerManager.IncreaseMaxHP();
		break;
	case LevelUpUpgrade::MoveSpeed:
		playerManager.UpgradeMoveSpeed();
		break;
	case LevelUpUpgrade::Heal:
		playerManager.RecoverHP();
		break;
	}
}

}
