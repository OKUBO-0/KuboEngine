#include "game/directxgame/player/PlayerProgression.h"

#include "game/directxgame/player/Player.h"
#include <algorithm>

#include "game/directxgame/core/CsvReader.h"
#include <stdexcept>

namespace DirectXGame {

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
		moveSpeedUpgradeStep_ < 0.0f ||
		moveSpeedMax_ <= 0.0f) {
		throw std::runtime_error(
			"playerStatus contains an invalid progression value");
	}
}

void PlayerProgression::TakeDamage()
{
	--lifeStock_;
}

void PlayerProgression::RecoverHP()
{
	lifeStock_ = (std::min)(lifeStock_ + 1, maxLifeStock_);
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
	++maxLifeStock_;
	lifeStock_ = maxLifeStock_;
}

void PlayerProgression::UpgradeAttackPower()
{
	++attackPower_;
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

	player->SetMoveSpeed((std::min)(
		moveSpeedMax_,
		player->GetMoveSpeed() + moveSpeedUpgradeStep_));
	++moveSpeedLevel_;
}

#ifdef _DEBUG
void PlayerProgression::ForceDebugDeath()
{
	lifeStock_ = 0;
}

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
#endif

}
