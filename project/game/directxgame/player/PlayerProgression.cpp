#include "PlayerProgression.h"

#include "Player.h"
#include <algorithm>

#include "CsvReader.h"
#include <stdexcept>

namespace DirectXGame {

void PlayerProgression::LoadCharacterStats(
	const std::string& filePath,
	const std::string& characterKey,
	Player* player)
{
	for (const CsvReader::CsvRow& row : CsvReader::LoadRows(filePath)) {
		if (row.size() < 3 || row[0] != characterKey) {
			continue;
		}
		const std::string& key = row[1];
		const std::string context = "characterStats." + characterKey + "." + key;
		if (key == "maxHP") {
			maxLifeStock_ = CsvReader::ParseInt32(row[2], context);
			lifeStock_ = maxLifeStock_;
		} else if (key == "maxHPCap") {
			maxLifeStockCap_ = CsvReader::ParseInt32(row[2], context);
		} else if (key == "baseMoveSpeed") {
			stats_.SetBaseMovementSpeed(CsvReader::ParseFloat(row[2], context));
		} else {
			const float value = CsvReader::ParseFloat(row[2], context);
			PlayerStatModifiers& stats = stats_.CharacterModifiers();
			if (key == "damage") stats.damage = value - 1.0f;
			else if (key == "attackSpeed") stats.attackSpeed = value - 1.0f;
			else if (key == "duration") stats.duration = value - 1.0f;
			else if (key == "movementSpeed") stats.movementSpeed = value - 1.0f;
			else if (key == "projectileSpeed") stats.projectileSpeed = value - 1.0f;
			else if (key == "areaSize") stats.areaSize = value - 1.0f;
			else if (key == "pickupRange") stats.pickupRange = value - 1.0f;
			else if (key == "expGain") stats.expGain = value - 1.0f;
			else if (key == "coinGain") stats.coinGain = value - 1.0f;
			else if (key == "critChance") stats.critChance = value;
			else if (key == "critDamage") stats.critDamage = value - 1.5f;
			else if (key == "armor") stats.armor = value;
			else if (key == "evasion") stats.evasion = value;
			else if (key == "hpRegen") stats.hpRegen = value;
			else if (key == "lifeSteal") stats.lifeSteal = value;
			else if (key == "projectileCount") stats.projectileCount =
				CsvReader::ParseInt32(row[2], context);
		}
	}
	ValidateLoadedStatus();
	ApplyCurrentMovementSpeed(player);
}

bool PlayerProgression::LoadStatusValue(
	const std::string& key,
	const std::string& value)
{
	if (key == "level") {
		level_ = CsvReader::ParseInt32(value, "playerStatus.level");
	} else if (key == "nextLevelExp") {
		nextLevelExp_ =
			CsvReader::ParseInt32(value, "playerStatus.nextLevelExp");
	} else if (key == "maxLifeStock") {
		maxLifeStock_ =
			CsvReader::ParseInt32(value, "playerStatus.maxLifeStock");
	} else if (key == "lifeStock") {
		lifeStock_ =
			CsvReader::ParseInt32(value, "playerStatus.lifeStock");
	} else if (key == "exp") {
		exp_ = CsvReader::ParseInt32(value, "playerStatus.exp");
	} else if (key == "totalExp") {
		totalExp_ =
			CsvReader::ParseInt32(value, "playerStatus.totalExp");
	} else if (key == "attackPower") {
		attackPower_ =
			CsvReader::ParseInt32(value, "playerStatus.attackPower");
	} else if (key == "damageMultiplier") {
		stats_.CharacterModifiers().damage =
			CsvReader::ParseFloat(value, "playerStatus.damageMultiplier") - 1.0f;
	} else if (key == "attackSpeedMultiplier") {
		stats_.CharacterModifiers().attackSpeed =
			CsvReader::ParseFloat(value, "playerStatus.attackSpeedMultiplier") - 1.0f;
	} else if (key == "durationMultiplier") {
		stats_.CharacterModifiers().duration =
			CsvReader::ParseFloat(value, "playerStatus.durationMultiplier") - 1.0f;
	} else if (key == "projectileSpeedMultiplier") {
		stats_.CharacterModifiers().projectileSpeed =
			CsvReader::ParseFloat(value, "playerStatus.projectileSpeedMultiplier") - 1.0f;
	} else if (key == "areaSizeMultiplier") {
		stats_.CharacterModifiers().areaSize =
			CsvReader::ParseFloat(value, "playerStatus.areaSizeMultiplier") - 1.0f;
	} else if (key == "movementSpeedMultiplier") {
		stats_.CharacterModifiers().movementSpeed =
			CsvReader::ParseFloat(value, "playerStatus.movementSpeedMultiplier") - 1.0f;
	} else if (key == "baseMoveSpeed") {
		stats_.SetBaseMovementSpeed(
			CsvReader::ParseFloat(value, "playerStatus.baseMoveSpeed"));
	} else if (key == "pickupRangeMultiplier") {
		stats_.CharacterModifiers().pickupRange =
			CsvReader::ParseFloat(value, "playerStatus.pickupRangeMultiplier") - 1.0f;
	} else if (key == "expGainMultiplier") {
		stats_.CharacterModifiers().expGain =
			CsvReader::ParseFloat(value, "playerStatus.expGainMultiplier") - 1.0f;
	} else if (key == "coinGainMultiplier") {
		stats_.CharacterModifiers().coinGain =
			CsvReader::ParseFloat(value, "playerStatus.coinGainMultiplier") - 1.0f;
	} else if (key == "critChance") {
		stats_.CharacterModifiers().critChance =
			CsvReader::ParseFloat(value, "playerStatus.critChance");
	} else if (key == "critDamageMultiplier") {
		stats_.CharacterModifiers().critDamage =
			CsvReader::ParseFloat(value, "playerStatus.critDamageMultiplier") - 1.5f;
	} else if (key == "projectileCountBonus") {
		stats_.CharacterModifiers().projectileCount =
			CsvReader::ParseInt32(value, "playerStatus.projectileCountBonus");
	} else if (key == "maxLifeStockCap") {
		maxLifeStockCap_ =
			CsvReader::ParseInt32(value, "playerStatus.maxLifeStockCap");
	} else if (key == "moveSpeedUpgradeCap") {
		moveSpeedUpgradeCap_ =
			CsvReader::ParseInt32(
				value,
				"playerStatus.moveSpeedUpgradeCap");
	} else if (key == "moveSpeedUpgradeStep") {
		moveSpeedUpgradeStep_ =
			CsvReader::ParseFloat(
				value,
				"playerStatus.moveSpeedUpgradeStep");
	} else if (key == "moveSpeedMax") {
		moveSpeedMax_ =
			CsvReader::ParseFloat(value, "playerStatus.moveSpeedMax");
	} else if (key == "expPickupRangeUpgradeCap") {
		expPickupRangeUpgradeCap_ =
			CsvReader::ParseInt32(
				value,
				"playerStatus.expPickupRangeUpgradeCap");
	} else if (key == "expPickupRangeUpgradeStep") {
		expPickupRangeUpgradeStep_ =
			CsvReader::ParseFloat(
				value,
				"playerStatus.expPickupRangeUpgradeStep");
	} else {
		return false;
	}

	return true;
}

void PlayerProgression::ValidateLoadedStatus() const
{
	if (level_ < 1 || nextLevelExp_ < 1 ||
		maxLifeStock_ < 1 || lifeStock_ < 0 ||
		lifeStock_ > maxLifeStock_ || exp_ < 0 ||
		totalExp_ < 0 || attackPower_ < 1 ||
		maxLifeStockCap_ < maxLifeStock_ ||
		moveSpeedUpgradeCap_ < 0 ||
		expPickupRangeUpgradeCap_ < 0 ||
		moveSpeedUpgradeStep_ < 0.0f ||
		moveSpeedMax_ <= 0.0f ||
		expPickupRangeUpgradeStep_ < 0.0f) {
		throw std::runtime_error(
			"playerStatus contains an invalid progression value");
	}
}

void PlayerProgression::TakeDamage(int32_t damage)
{
	lifeStock_ -= (std::max)(1, damage);
}

int32_t PlayerProgression::RecoverHP(int32_t amount)
{
	const int32_t before = lifeStock_;
	lifeStock_ = (std::min)(
		lifeStock_ + (std::max)(0, amount), maxLifeStock_);
	return lifeStock_ - before;
}

void PlayerProgression::AddEXP(int32_t amount)
{
	exp_ += amount;
	totalExp_ += amount;
	while (exp_ >= nextLevelExp_) {
		exp_ -= nextLevelExp_;
		++level_;
		nextLevelExp_ = static_cast<int32_t>(
			static_cast<float>(nextLevelExp_) * 1.5f);
		levelUpRequested_ = true;
	}
}

void PlayerProgression::IncreaseMaxHP()
{
	if (maxLifeStock_ >= maxLifeStockCap_) {
		RecoverHP();
		return;
	}
	maxLifeStock_ += 20;
	++maxHPUpgradeLevel_;
	if (maxLifeStock_ > maxLifeStockCap_) {
		maxLifeStock_ = maxLifeStockCap_;
	}
	lifeStock_ = maxLifeStock_;
}

void PlayerProgression::IncreaseMaxHPBy(int32_t amount)
{
	const int32_t before = maxLifeStock_;
	maxLifeStock_ = (std::min)(
		maxLifeStock_ + (std::max)(0, amount), maxLifeStockCap_);
	const int32_t gained = maxLifeStock_ - before;
	lifeStock_ = (std::min)(lifeStock_ + gained, maxLifeStock_);
}

void PlayerProgression::UpgradeAttackPower()
{
	attackPower_ += 5;
	stats_.RunModifiers().damage += 0.5f;
	++attackPowerUpgradeLevel_;
}

void PlayerProgression::UpgradeMoveSpeed(Player* player)
{
	if (!player) {
		return;
	}
	if (moveSpeedLevel_ >= moveSpeedUpgradeCap_) {
		UpgradeAttackPower();
		return;
	}

	stats_.RunModifiers().movementSpeed +=
		moveSpeedUpgradeStep_ / stats_.GetBaseMovementSpeed();
	ApplyCurrentMovementSpeed(player);
	++moveSpeedLevel_;
}

void PlayerProgression::ApplyCurrentMovementSpeed(Player* player) const
{
	if (player) {
		player->SetMoveSpeed((std::min)(
			moveSpeedMax_, stats_.GetMovementSpeed()));
	}
}

void PlayerProgression::UpgradeExpPickupRange()
{
	if (expPickupRangeLevel_ >= expPickupRangeUpgradeCap_) {
		UpgradeAttackPower();
		return;
	}
	++expPickupRangeLevel_;
	stats_.RunModifiers().pickupRange += expPickupRangeUpgradeStep_;
	expPickupRangeMultiplier_ =
		1.0f + permanentExpPickupRangeBonus_ + expPickupRangeUpgradeStep_ *
			static_cast<float>(expPickupRangeLevel_);
}

void PlayerProgression::ApplyPermanentBonuses(
	Player* player,
	int32_t maxHPLevel,
	int32_t attackLevel,
	int32_t moveSpeedLevel,
	int32_t expPickupRangeLevel,
	int32_t coinGainLevel)
{
	constexpr int32_t kMaxHPPerLevel = 20;
	constexpr int32_t kAttackPerLevel = 5;
	const int32_t maxHPBonus = kMaxHPPerLevel * (std::max)(0, maxHPLevel);
	maxLifeStockCap_ += maxHPBonus;
	maxLifeStock_ += maxHPBonus;
	lifeStock_ = maxLifeStock_;
	attackPower_ += kAttackPerLevel * (std::max)(0, attackLevel);
	stats_.PermanentModifiers().damage +=
		0.5f * static_cast<float>((std::max)(0, attackLevel));

	const float moveSpeedBonus = moveSpeedUpgradeStep_ *
		static_cast<float>((std::max)(0, moveSpeedLevel));
	stats_.PermanentModifiers().movementSpeed +=
		moveSpeedBonus / stats_.GetBaseMovementSpeed();
	moveSpeedMax_ += moveSpeedBonus;
	ApplyCurrentMovementSpeed(player);

	permanentExpPickupRangeBonus_ = expPickupRangeUpgradeStep_ *
		static_cast<float>((std::max)(0, expPickupRangeLevel));
	stats_.PermanentModifiers().pickupRange = permanentExpPickupRangeBonus_;
	stats_.PermanentModifiers().coinGain =
		0.25f * static_cast<float>((std::max)(0, coinGainLevel));
	expPickupRangeMultiplier_ = 1.0f + permanentExpPickupRangeBonus_ +
		expPickupRangeUpgradeStep_ * static_cast<float>(expPickupRangeLevel_);
}

#ifdef _DEBUG
void PlayerProgression::ForceDebugDeath()
{
	lifeStock_ = 0;
}
#endif

void PlayerProgression::MakeDebugStrongest(Player* player)
{
	maxLifeStock_ = maxLifeStockCap_;
	lifeStock_ = maxLifeStock_;
	attackPower_ = (std::max)(attackPower_, 99);
	moveSpeedLevel_ = moveSpeedUpgradeCap_;
	if (player) {
		player->SetMoveSpeed(moveSpeedMax_);
	}
	ClearLevelUpRequest();
}

}
