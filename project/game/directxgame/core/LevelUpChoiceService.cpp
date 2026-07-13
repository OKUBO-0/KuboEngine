#include "LevelUpChoiceService.h"
#include "PlayerManager.h"
#include "PassiveItemData.h"
#include "WeaponUpgradeData.h"
#include <algorithm>
#include <random>
#include <utility>

namespace DirectXGame {

namespace {

std::string WeaponTitle(const char* name, int32_t nextLevel)
{
	return std::string(name) + " LV " + std::to_string(nextLevel);
}

std::string WeaponDetail(
	WeaponType weaponType,
	int32_t nextLevel,
	const std::string& effectText)
{
	const WeaponUpgradeLevelMetadata metadata =
		GetWeaponUpgradeMetadata(weaponType, nextLevel);
	if (metadata.statTags.empty()) {
		return effectText;
	}
	return "[" + std::string(ToString(metadata.rarity)) + "] " +
		std::string(metadata.statTags) + " - " + effectText;
}

std::string BowArrowEffect(int32_t level)
{
	switch (level) {
	case 1: return "最寄りの敵へ自動照準 貫通矢";
	case 2: return "貫通数3";
	case 3: return "ダメージ+3";
	case 4: return "矢数2本";
	case 5: return "弾速+25% サイズ+15%";
	case 6: return "ダメージ+4";
	case 7: return "矢数3本 貫通数5";
	case 8: return "ダメージ+5 弾速+20%";
	default: return "UPGRADE";
	}
}

std::string RockEffect(int32_t level)
{
	switch (level) {
	case 1: return "岩2個 周回攻撃";
	case 2: return "岩3個";
	case 3: return "回転速度+35% サイズ+20%";
	case 4: return "ダメージ+3";
	case 5: return "岩4個";
	case 6: return "回転速度+30% サイズ+20%";
	case 7: return "ヒット間隔-20%";
	case 8: return "岩5個 ダメージ+4";
	default: return "UPGRADE";
	}
}

std::string ThunderStaffEffect(int32_t level)
{
	switch (level) {
	case 1: return "雷撃1回 範囲6";
	case 2: return "ダメージ+3";
	case 3: return "雷撃2回";
	case 4: return "範囲+20%";
	case 5: return "ダメージ+4";
	case 6: return "雷撃3回";
	case 7: return "攻撃間隔-18%";
	case 8: return "雷撃4回 範囲+25%";
	default: return "UPGRADE";
	}
}

std::string FlameStaffEffect(int32_t level)
{
	switch (level) {
	case 1: return "自動照準 火球1個 爆発範囲4.2";
	case 2: return "ダメージ+3";
	case 3: return "爆発範囲+20%";
	case 4: return "火球2個を連続発射";
	case 5: return "ダメージ+4";
	case 6: return "爆発範囲+25%";
	case 7: return "弾速+20%";
	case 8: return "火球3個を連続発射 ダメージ+5";
	default: return "UPGRADE";
	}
}

std::string SwordEffect(int32_t level)
{
	switch (level) {
	case 1: return "最寄りの敵へ前方範囲斬撃";
	case 2: return "ダメージ+3";
	case 3: return "攻撃範囲+1";
	case 4: return "攻撃間隔-12%";
	case 5: return "ダメージ+4";
	case 6: return "斬撃角度拡大";
	case 7: return "攻撃範囲+1.5";
	case 8: return "ダメージ+6";
	default: return "UPGRADE";
	}
}

std::string AuraEffect(int32_t level)
{
	switch (level) {
	case 1: return "周囲へ一定間隔の範囲攻撃";
	case 2: return "ダメージ+2";
	case 3: return "攻撃範囲+1";
	case 4: return "攻撃間隔-12%";
	case 5: return "ダメージ+3";
	case 6: return "攻撃範囲+1.5";
	case 7: return "攻撃間隔-15%";
	case 8: return "ダメージ+5";
	default: return "UPGRADE";
	}
}

std::string FlameShoesEffect(int32_t level)
{
	switch (level) {
	case 1: return "移動した場所に炎床を残す";
	case 2: return "ダメージ+2";
	case 3: return "炎床範囲+0.8";
	case 4: return "持続時間+1秒";
	case 5: return "ダメージ+3";
	case 6: return "炎床生成間隔-15%";
	case 7: return "ダメージ間隔-15%";
	case 8: return "ダメージ+5 範囲+1";
	default: return "UPGRADE";
	}
}

std::string BoneEffect(int32_t level)
{
	if (level == 1) return "敵の間を跳ねる骨";
	if (level == 3 || level == 6) return "跳弾回数+1";
	if (level == 8) return "骨2個 ダメージ+4";
	return level == 2 ? "弾速+20%" : "ダメージ+3";
}

std::string HandgunEffect(int32_t level)
{
	if (level == 1) return "最寄りの敵へ3連射";
	if (level == 3 || level == 6) return "跳弾回数+1";
	if (level == 8) return "6連射 ダメージ+4";
	return level == 2 ? "攻撃間隔-15%" : "ダメージ+3";
}

std::string BoomerangEffect(int32_t level)
{
	if (level == 1) return "往路と復路で攻撃";
	if (level == 3 || level == 6) return "命中上限+2";
	if (level == 8) return "2個発射 ダメージ+4";
	return level == 2 ? "サイズ+20%" : "ダメージ+3";
}

}

std::vector<LevelUpChoice> LevelUpChoiceService::Build(
	const PlayerManager& playerManager,
	size_t maxChoices)
{
	std::vector<LevelUpChoice> weaponChoices;
	std::vector<LevelUpChoice> itemChoices;
	weaponChoices.reserve(10);
	itemChoices.reserve(kPassiveItemDefinitions.size());
	const auto addWeaponChoice = [&weaponChoices](
		LevelUpUpgrade upgrade,
		WeaponType weaponType,
		int32_t nextLevel,
		std::string texturePath,
		std::string iconPath,
		std::string titleText,
		std::string detailText) {
			const WeaponUpgradeLevelMetadata metadata =
				GetWeaponUpgradeMetadata(weaponType, nextLevel);
			weaponChoices.push_back({
				upgrade,
				LevelUpChoiceCategory::Weapon,
				weaponType,
				PassiveItemType::Damage,
				std::move(texturePath),
				std::move(iconPath),
				std::move(titleText),
				WeaponDetail(weaponType, nextLevel, detailText),
				std::string(ToString(metadata.rarity)),
				std::string(metadata.statTags),
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
				LevelUpChoiceCategory::Item,
				WeaponType::BowArrow,
				itemType,
				std::move(texturePath),
				std::move(iconPath),
				std::move(titleText),
				std::move(detailText),
				{},
				{},
			});
		};

	if (!playerManager.IsNormalBulletMaxLevel()) {
		const int32_t nextLevel = playerManager.GetNormalBulletLevel() + 1;
		addWeaponChoice(
			LevelUpUpgrade::BowArrow,
			WeaponType::BowArrow,
			nextLevel,
			"ui/game/lvup/levelup_frame.png",
			"ui/game/lvup/icon_weapon_bow_arrow.png",
			WeaponTitle("弓矢", nextLevel),
			BowArrowEffect(nextLevel));
	}
	if (!playerManager.IsOrbitBulletMaxLevel() &&
		playerManager.CanAcquireWeapon(WeaponType::Rock)) {
		const int32_t nextLevel = playerManager.HasOrbitBullets()
			? playerManager.GetOrbitBulletLevel() + 1
			: 1;
		addWeaponChoice(
			LevelUpUpgrade::Rock,
			WeaponType::Rock,
			nextLevel,
			"ui/game/lvup/levelup_frame.png",
			"ui/game/lvup/icon_weapon_rock.png",
			playerManager.HasOrbitBullets() ?
				WeaponTitle("岩石", nextLevel) :
				"岩石 追加",
			RockEffect(nextLevel));
	}
	if (!playerManager.IsLightningMaxLevel() &&
		playerManager.CanAcquireWeapon(WeaponType::ThunderStaff)) {
		const int32_t nextLevel = playerManager.HasLightning()
			? playerManager.GetLightningLevel() + 1
			: 1;
		addWeaponChoice(
			LevelUpUpgrade::ThunderStaff,
			WeaponType::ThunderStaff,
			nextLevel,
			"ui/game/lvup/levelup_frame.png",
			"ui/game/lvup/icon_weapon_thunder_staff.png",
			playerManager.HasLightning() ?
				WeaponTitle("雷の杖", nextLevel) :
				"雷の杖 追加",
			ThunderStaffEffect(nextLevel));
	}
	if (!playerManager.IsExplosiveBulletMaxLevel() &&
		playerManager.CanAcquireWeapon(WeaponType::FlameStaff)) {
		const int32_t nextLevel = playerManager.HasExplosiveBullets()
			? playerManager.GetExplosiveBulletLevel() + 1
			: 1;
		addWeaponChoice(
			LevelUpUpgrade::FlameStaff,
			WeaponType::FlameStaff,
			nextLevel,
			"ui/game/lvup/levelup_frame.png",
			"ui/game/lvup/icon_weapon_flame_staff.png",
			playerManager.HasExplosiveBullets() ?
				WeaponTitle("炎の杖", nextLevel) :
				"炎の杖 追加",
			FlameStaffEffect(nextLevel));
	}
	if (!playerManager.IsSwordMaxLevel() &&
		playerManager.CanAcquireWeapon(WeaponType::Sword)) {
		const int32_t nextLevel = playerManager.HasSword()
			? playerManager.GetSwordLevel() + 1
			: 1;
		addWeaponChoice(
			LevelUpUpgrade::Sword,
			WeaponType::Sword,
			nextLevel,
			"ui/game/lvup/levelup_frame.png",
			"ui/game/lvup/icon_weapon_sword.png",
			playerManager.HasSword()
				? WeaponTitle("ソード", nextLevel)
				: "ソード 追加",
			SwordEffect(nextLevel));
	}
	if (!playerManager.IsAuraMaxLevel() &&
		playerManager.CanAcquireWeapon(WeaponType::Aura)) {
		const int32_t nextLevel = playerManager.HasAura()
			? playerManager.GetAuraLevel() + 1 : 1;
		addWeaponChoice(
			LevelUpUpgrade::Aura, WeaponType::Aura,
			nextLevel,
			"ui/game/lvup/levelup_frame.png",
			"ui/game/lvup/icon_common_unknown.png",
			playerManager.HasAura()
				? WeaponTitle("オーラ", nextLevel) : "オーラ 追加",
			AuraEffect(nextLevel));
	}
	if (!playerManager.IsFlameShoesMaxLevel() &&
		playerManager.CanAcquireWeapon(WeaponType::FlameShoes)) {
		const int32_t nextLevel = playerManager.HasFlameShoes()
			? playerManager.GetFlameShoesLevel() + 1 : 1;
		addWeaponChoice(
			LevelUpUpgrade::FlameShoes, WeaponType::FlameShoes,
			nextLevel,
			"ui/game/lvup/levelup_frame.png",
			"ui/game/lvup/icon_common_unknown.png",
			playerManager.HasFlameShoes()
				? WeaponTitle("炎の靴", nextLevel)
				: "炎の靴 追加",
			FlameShoesEffect(nextLevel));
	}
	if (!playerManager.IsBoneMaxLevel() && playerManager.CanAcquireWeapon(WeaponType::Bone)) {
		const int32_t nextLevel = playerManager.HasBone() ? playerManager.GetBoneLevel() + 1 : 1;
		addWeaponChoice(LevelUpUpgrade::Bone, WeaponType::Bone,
			nextLevel,
			"ui/game/lvup/levelup_frame.png", "ui/game/lvup/icon_weapon_bone.png",
			playerManager.HasBone() ? WeaponTitle("ボーン", nextLevel) : "ボーン 追加",
			BoneEffect(nextLevel));
	}
	if (!playerManager.IsHandgunMaxLevel() && playerManager.CanAcquireWeapon(WeaponType::Handgun)) {
		const int32_t nextLevel = playerManager.HasHandgun() ? playerManager.GetHandgunLevel() + 1 : 1;
		addWeaponChoice(LevelUpUpgrade::Handgun, WeaponType::Handgun,
			nextLevel,
			"ui/game/lvup/levelup_frame.png", "ui/game/lvup/icon_weapon_handgun.png",
			playerManager.HasHandgun() ? WeaponTitle("拳銃", nextLevel) : "拳銃 追加",
			HandgunEffect(nextLevel));
	}
	if (!playerManager.IsBoomerangMaxLevel() && playerManager.CanAcquireWeapon(WeaponType::Boomerang)) {
		const int32_t nextLevel = playerManager.HasBoomerang() ? playerManager.GetBoomerangLevel() + 1 : 1;
		addWeaponChoice(LevelUpUpgrade::Boomerang, WeaponType::Boomerang,
			nextLevel,
			"ui/game/lvup/levelup_frame.png", "ui/game/lvup/icon_weapon_boomerang.png",
			playerManager.HasBoomerang() ? WeaponTitle("ブーメラン", nextLevel) : "ブーメラン 追加",
			BoomerangEffect(nextLevel));
	}
	for (const PassiveItemDefinition& definition : kPassiveItemDefinitions) {
		if (!playerManager.CanAcquirePassiveItem(definition.type)) {
			continue;
		}
		const int32_t nextLevel =
			playerManager.GetPassiveItemLevel(definition.type) + 1;
		addItemChoice(
			LevelUpUpgrade::PassiveItem,
			definition.type,
			"ui/game/lvup/levelup_frame.png",
			std::string(definition.iconPath),
			WeaponTitle(definition.name.data(), nextLevel),
			std::string(definition.effect));
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
	const LevelUpChoice& choice)
{
	switch (choice.upgrade) {
	case LevelUpUpgrade::BowArrow:
		playerManager.UpgradeNormalBullets();
		break;
	case LevelUpUpgrade::Rock:
		playerManager.UpgradeOrbitBullets();
		break;
	case LevelUpUpgrade::ThunderStaff:
		playerManager.UpgradeLightning();
		break;
	case LevelUpUpgrade::FlameStaff:
		playerManager.UpgradeExplosiveBullets();
		break;
	case LevelUpUpgrade::Sword:
		playerManager.UpgradeSword();
		break;
	case LevelUpUpgrade::Aura:
		playerManager.UpgradeAura();
		break;
	case LevelUpUpgrade::FlameShoes:
		playerManager.UpgradeFlameShoes();
		break;
	case LevelUpUpgrade::Bone:
		playerManager.UpgradeBone();
		break;
	case LevelUpUpgrade::Handgun:
		playerManager.UpgradeHandgun();
		break;
	case LevelUpUpgrade::Boomerang:
		playerManager.UpgradeBoomerang();
		break;
	case LevelUpUpgrade::PassiveItem:
		playerManager.UpgradePassiveItem(choice.passiveItemType);
		break;
	}
}

}
