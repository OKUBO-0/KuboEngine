#pragma once

#include <cstdint>
#include <string>

namespace DirectXGame {

class Player;

class PlayerProgression final {
public:
	bool LoadStatusValue(
		const std::string& key,
		const std::string& value);
	void ValidateLoadedStatus() const;

	void TakeDamage(int32_t damage);
	void RecoverHP();
	void AddEXP(int32_t amount);
	void IncreaseMaxHP();
	void UpgradeAttackPower();
	void UpgradeMoveSpeed(Player* player);
	void UpgradeExpPickupRange();
	void ApplyPermanentBonuses(
		Player* player,
		int32_t maxHPLevel,
		int32_t attackLevel,
		int32_t moveSpeedLevel,
		int32_t expPickupRangeLevel);

#ifdef _DEBUG
	void ForceDebugDeath();
#endif
	void MakeDebugStrongest(Player* player);

	int32_t GetHP() const { return lifeStock_; }
	int32_t GetMaxHP() const { return maxLifeStock_; }
	bool IsMaxHPAtCap() const { return maxLifeStock_ >= maxLifeStockCap_; }
	bool IsDead() const { return lifeStock_ <= 0; }
	int32_t GetEXP() const { return exp_; }
	int32_t GetTotalEXP() const { return totalExp_; }
	int32_t GetLevel() const { return level_; }
	int32_t GetNextLevelEXP() const { return nextLevelExp_; }
	bool IsLevelUpRequested() const { return levelUpRequested_; }
	void ClearLevelUpRequest() { levelUpRequested_ = false; }
	int32_t GetAttackPower() const { return attackPower_; }
	int32_t GetMaxHPUpgradeLevel() const { return maxHPUpgradeLevel_; }
	int32_t GetAttackPowerUpgradeLevel() const { return attackPowerUpgradeLevel_; }
	int32_t GetMoveSpeedLevel() const { return moveSpeedLevel_; }
	bool IsMoveSpeedMaxLevel() const { return moveSpeedLevel_ >= moveSpeedUpgradeCap_; }
	int32_t GetExpPickupRangeLevel() const { return expPickupRangeLevel_; }
	float GetExpPickupRangeMultiplier() const { return expPickupRangeMultiplier_; }

private:
	int32_t level_ = 1;
	int32_t nextLevelExp_ = 10;
	int32_t maxLifeStock_ = 100;
	int32_t lifeStock_ = 100;
	int32_t exp_ = 0;
	int32_t totalExp_ = 0;
	int32_t attackPower_ = 10;
	int32_t maxHPUpgradeLevel_ = 0;
	int32_t attackPowerUpgradeLevel_ = 0;
	int32_t moveSpeedLevel_ = 0;
	int32_t expPickupRangeLevel_ = 0;
	bool levelUpRequested_ = false;

	int32_t maxLifeStockCap_ = 200;
	int32_t moveSpeedUpgradeCap_ = 5;
	int32_t expPickupRangeUpgradeCap_ = 3;
	float moveSpeedUpgradeStep_ = 3.0f;
	float moveSpeedMax_ = 45.0f;
	float expPickupRangeUpgradeStep_ = 0.25f;
	float expPickupRangeMultiplier_ = 1.0f;
	float permanentExpPickupRangeBonus_ = 0.0f;
};

}
