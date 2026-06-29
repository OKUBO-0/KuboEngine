#include "LevelUpChoiceService.h"
#include "PlayerManager.h"
#include <algorithm>
#include <random>
#include <utility>

namespace DirectXGame {

namespace {

std::string WeaponTitle(const char* name, int32_t nextLevel)
{
	return std::string(name) + " LV " + std::to_string(nextLevel);
}

std::string NormalEffect(int32_t level)
{
	switch (level) {
	case 2: return "弾数2個";
	case 3: return "弾速+20% 間隔-8%";
	case 4: return "弾数3個";
	case 5: return "ダメージ+5";
	case 6: return "弾数4個";
	case 7: return "貫通数2";
	case 8: return "威力+5 弾速+15% 間隔-12%";
	default: return "UPGRADE";
	}
}

std::string OrbitEffect(int32_t level)
{
	switch (level) {
	case 1: return "周囲弾1個 間隔0.5s";
	case 2: return "周囲弾2個";
	case 3: return "半径+2 回転+0.01 サイズ+20%";
	case 4: return "攻撃間隔-20%";
	case 5: return "周囲弾3個";
	case 6: return "半径+2 回転+0.01 サイズ+20%";
	case 7: return "攻撃間隔-20%";
	case 8: return "周囲弾4個";
	default: return "UPGRADE";
	}
}

std::string LightningEffect(int32_t level)
{
	switch (level) {
	case 1: return "対象1体 半径6 間隔2.4s";
	case 2: return "ダメージ+5";
	case 3: return "対象2体";
	case 4: return "半径+1.5";
	case 5: return "ダメージ+5";
	case 6: return "対象3体";
	case 7: return "攻撃間隔-18%";
	case 8: return "対象4体 半径+1.5";
	default: return "UPGRADE";
	}
}

std::string ExplosiveEffect(int32_t level)
{
	switch (level) {
	case 1: return "威力+4 半径4.2 間隔2.0s";
	case 2: return "ダメージ+4";
	case 3: return "半径+0.8";
	case 4: return "攻撃間隔-14%";
	case 5: return "ダメージ+5";
	case 6: return "半径+1.0";
	case 7: return "弾速+12%";
	case 8: return "威力+6 間隔-18%";
	default: return "UPGRADE";
	}
}

}

std::vector<LevelUpChoice> LevelUpChoiceService::Build(
	const PlayerManager& playerManager,
	size_t maxChoices)
{
	std::vector<LevelUpChoice> weaponChoices;
	std::vector<LevelUpChoice> itemChoices;
	weaponChoices.reserve(4);
	itemChoices.reserve(4);
	const auto addWeaponChoice = [&weaponChoices](
		LevelUpUpgrade upgrade,
		WeaponType weaponType,
		std::string texturePath,
		std::string iconPath,
		std::string titleText,
		std::string detailText) {
			weaponChoices.push_back({
				upgrade,
				LevelUpChoiceCategory::Weapon,
				weaponType,
				PassiveItemType::Attack,
				std::move(texturePath),
				std::move(iconPath),
				std::move(titleText),
				std::move(detailText),
			});
		};
	const auto addItemChoice = [&itemChoices](
		LevelUpUpgrade upgrade,
		PassiveItemType itemType,
		std::string texturePath,
		std::string iconPath,
		std::string titleText,
		std::string detailText) {
			itemChoices.push_back({
				upgrade,
				LevelUpChoiceCategory::PassiveItem,
				WeaponType::NormalBullet,
				itemType,
				std::move(texturePath),
				std::move(iconPath),
				std::move(titleText),
				std::move(detailText),
			});
		};

	if (!playerManager.IsNormalBulletMaxLevel()) {
		const int32_t nextLevel = playerManager.GetNormalBulletLevel() + 1;
		addWeaponChoice(
			LevelUpUpgrade::Normal,
			WeaponType::NormalBullet,
			"ui/game/lvup/levelup_frame.png",
			"ui/game/lvup/normal_icon.png",
			WeaponTitle("通常弾", nextLevel),
			NormalEffect(nextLevel));
	}
	if (!playerManager.IsOrbitBulletMaxLevel()) {
		const int32_t nextLevel = playerManager.HasOrbitBullets()
			? playerManager.GetOrbitBulletLevel() + 1
			: 1;
		addWeaponChoice(
			LevelUpUpgrade::Orbit,
			WeaponType::OrbitBullet,
			"ui/game/lvup/levelup_frame.png",
			"ui/game/lvup/orbit_icon.png",
			playerManager.HasOrbitBullets() ?
				WeaponTitle("軌道弾", nextLevel) :
				"軌道弾 追加",
			OrbitEffect(nextLevel));
	}
	if (!playerManager.IsLightningMaxLevel()) {
		const int32_t nextLevel = playerManager.HasLightning()
			? playerManager.GetLightningLevel() + 1
			: 1;
		addWeaponChoice(
			LevelUpUpgrade::Lightning,
			WeaponType::Lightning,
			"ui/game/lvup/levelup_frame.png",
			"ui/game/lvup/lightning_icon.png",
			playerManager.HasLightning() ?
				WeaponTitle("雷撃", nextLevel) :
				"雷撃 追加",
			LightningEffect(nextLevel));
	}
	if (!playerManager.IsExplosiveBulletMaxLevel()) {
		const int32_t nextLevel = playerManager.HasExplosiveBullets()
			? playerManager.GetExplosiveBulletLevel() + 1
			: 1;
		addWeaponChoice(
			LevelUpUpgrade::Explosive,
			WeaponType::ExplosiveBullet,
			"ui/game/lvup/levelup_frame.png",
			"ui/game/lvup/normal_icon.png",
			playerManager.HasExplosiveBullets() ?
				WeaponTitle("爆発弾", nextLevel) :
				"爆発弾 追加",
			ExplosiveEffect(nextLevel));
	}
	addItemChoice(
		LevelUpUpgrade::Attack,
		PassiveItemType::Attack,
		"ui/game/lvup/levelup_frame.png",
		"ui/game/lvup/attack_icon.png",
		"攻撃力 UP",
		"基礎ダメージ+5");
	if (!playerManager.IsMaxHPAtCap()) {
		addItemChoice(
			LevelUpUpgrade::MaxHp,
			PassiveItemType::MaxHp,
			"ui/game/lvup/levelup_frame.png",
			"ui/game/lvup/maxhp_icon.png",
			"最大HP UP",
			"最大HP+20 HP全回復");
	}
	if (!playerManager.IsMoveSpeedMaxLevel()) {
		addItemChoice(
			LevelUpUpgrade::MoveSpeed,
			PassiveItemType::MoveSpeed,
			"ui/game/lvup/levelup_frame.png",
			"ui/game/lvup/speed_icon.png",
			"移動速度 UP",
			"移動速度+3");
	}
	if (playerManager.GetHP() < playerManager.GetMaxHP()) {
		addItemChoice(
			LevelUpUpgrade::Heal,
			PassiveItemType::Heal,
			"ui/game/lvup/levelup_frame.png",
			"ui/game/lvup/heal_icon.png",
			"HP 回復",
			"HP+1");
	}

	static std::mt19937 rng{ std::random_device{}() };
	std::shuffle(weaponChoices.begin(), weaponChoices.end(), rng);
	std::shuffle(itemChoices.begin(), itemChoices.end(), rng);
	std::vector<LevelUpChoice> choices;
	choices.reserve(maxChoices);
	const size_t weaponTarget =
		(std::min)(weaponChoices.size(), maxChoices > 1 ? size_t{ 2 } : maxChoices);
	const size_t itemTarget =
		(std::min)(itemChoices.size(), maxChoices - weaponTarget);
	choices.insert(
		choices.end(),
		weaponChoices.begin(),
		weaponChoices.begin() + static_cast<std::ptrdiff_t>(weaponTarget));
	choices.insert(
		choices.end(),
		itemChoices.begin(),
		itemChoices.begin() + static_cast<std::ptrdiff_t>(itemTarget));
	for (size_t index = weaponTarget;
		choices.size() < maxChoices && index < weaponChoices.size();
		++index) {
		choices.push_back(std::move(weaponChoices[index]));
	}
	for (size_t index = itemTarget;
		choices.size() < maxChoices && index < itemChoices.size();
		++index) {
		choices.push_back(std::move(itemChoices[index]));
	}
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
	case LevelUpUpgrade::Explosive:
		playerManager.UpgradeExplosiveBullets();
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
