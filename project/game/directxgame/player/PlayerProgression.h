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

	void TakeDamage();
	void RecoverHP();
	void AddEXP(int32_t amount);
	void IncreaseMaxHP();
	void UpgradeAttackPower();
	void UpgradeMoveSpeed(Player* player);

#ifdef _DEBUG
	void ForceDebugDeath();
	void MakeDebugStrongest(Player* player);
#endif

	int32_t GetHP() const { return lifeStock_; }
	int32_t GetMaxHP() const { return maxLifeStock_; }
	bool IsDead() const { return lifeStock_ <= 0; }
	int32_t GetEXP() const { return exp_; }
	int32_t GetTotalEXP() const { return totalExp_; }
	int32_t GetLevel() const { return level_; }
	int32_t GetNextLevelEXP() const { return nextLevelExp_; }
	bool IsLevelUpRequested() const { return levelUpRequested_; }
	void ClearLevelUpRequest() { levelUpRequested_ = false; }
	int32_t GetAttackPower() const { return attackPower_; }
	int32_t GetMoveSpeedLevel() const { return moveSpeedLevel_; }

private:
	int32_t level_ = 1;
	int32_t nextLevelExp_ = 10;
	int32_t maxLifeStock_ = 3;
	int32_t lifeStock_ = 3;
	int32_t exp_ = 0;
	int32_t totalExp_ = 0;
	int32_t attackPower_ = 1;
	int32_t moveSpeedLevel_ = 0;
	bool levelUpRequested_ = false;

	int32_t maxLifeStockCap_ = 6;
	int32_t moveSpeedUpgradeCap_ = 5;
	float moveSpeedUpgradeStep_ = 3.0f;
	float moveSpeedMax_ = 45.0f;
};

}
