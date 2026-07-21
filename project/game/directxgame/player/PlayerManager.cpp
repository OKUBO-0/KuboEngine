#include "PlayerManager.h"

#include "CsvReader.h"
#include "DataPaths.h"
#include "ResourcePaths.h"
#include "UILayoutIO.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace DirectXGame {

void PlayerManager::Initialize(Player* player)
{
	player_ = player;
	weapons_.Initialize(
		ResourcePaths::MakeDataPath(
			"weaponUpgradeSettings.csv"));
	weapons_.LoadVisualTuning(
		UILayoutIO::LoadOrDefault(DataPaths::kDebugTuning, {}));
	if (player_) {
		player_->SetVisible(visible_);
	}
}

void PlayerManager::LoadStatusFromCSV(
	const std::string& filePath)
{
	const CsvReader::CsvTable rows =
		CsvReader::LoadRows(filePath);
	for (const CsvReader::CsvRow& row : rows) {
		if (row.size() < 2) {
			continue;
		}

		const std::string& key = row[0];
		const std::string& value = row[1];
		if (progression_.LoadStatusValue(key, value) ||
			weapons_.LoadStatusValue(key, value)) {
			continue;
		}
		if (key == "invincibilityDuration") {
			invincibilityDuration_ =
				CsvReader::ParseFloat(
					value,
					"playerStatus.invincibilityDuration");
			if (invincibilityDuration_ < 0.0f) {
				throw std::runtime_error(
					"playerStatus.invincibilityDuration must be non-negative");
			}
		}
	}
	progression_.ValidateLoadedStatus();
	progression_.ApplyCurrentMovementSpeed(player_);
}

void PlayerManager::LoadWeaponUpgradeSettings(
	const std::string& filePath)
{
	weapons_.LoadUpgradeSettings(filePath);
}

void PlayerManager::Update(float deltaTime)
{
	UpdateInvincibility(deltaTime);
	hpRegenAccumulator_ +=
		progression_.GetStats().GetHpRegenPerSecond() * deltaTime;
	const int32_t regenPoints = static_cast<int32_t>(hpRegenAccumulator_);
	if (regenPoints > 0) {
		progression_.RecoverHP(regenPoints);
		hpRegenAccumulator_ -= static_cast<float>(regenPoints);
	}
	weapons_.Update(
		deltaTime,
		player_,
		enemyManager_,
		progression_.GetStats());
}

void PlayerManager::Draw()
{
	weapons_.Draw();
}

bool PlayerManager::TakeDamage(int32_t damage)
{
	if (invincible_) {
		return false;
	}
	const PlayerStats& stats = progression_.GetStats();
	const float evasionRoll = std::uniform_real_distribution<float>(0.0f, 1.0f)(
		damageRandomEngine_);
	if (evasionRoll < stats.GetEvasionChance()) {
		return false;
	}
	const int32_t mitigatedDamage = (std::max)(1, static_cast<int32_t>(
		std::ceil(static_cast<float>(damage) *
			(1.0f - stats.GetArmorReduction()))));

	progression_.TakeDamage(mitigatedDamage);
	invincible_ = true;
	invincibleTimer_ = invincibilityDuration_;
	visible_ = false;
	if (player_) {
		player_->SetVisible(false);
	}
	return true;
}

int32_t PlayerManager::RecoverHP(int32_t amount)
{
	return progression_.RecoverHP(amount);
}

int32_t PlayerManager::ApplyLifeStealOnHit()
{
	const float chance = progression_.GetStats().GetLifeStealChance();
	int32_t heal = static_cast<int32_t>(chance);
	const float fractionalChance = chance - static_cast<float>(heal);
	if (std::uniform_real_distribution<float>(0.0f, 1.0f)(damageRandomEngine_) <
		fractionalChance) {
		++heal;
	}
	return progression_.RecoverHP(heal);
}

int32_t PlayerManager::AddEXP(int32_t amount)
{
	const int32_t adjustedAmount = (std::max)(0, static_cast<int32_t>(
		std::lround(static_cast<float>(amount) *
			progression_.GetStats().GetExpGainMultiplier())));
	progression_.AddEXP(adjustedAmount);
	return adjustedAmount;
}

DamageResult PlayerManager::RollDamage(int32_t baseDamage)
{
	const float randomUnit = std::uniform_real_distribution<float>(0.0f, 1.0f)(
		damageRandomEngine_);
	return progression_.GetStats().ResolveHit(baseDamage, randomUnit);
}

bool PlayerManager::CanAcquirePassiveItem(PassiveItemType type) const
{
	const size_t index = PassiveItemIndex(type);
	if (index >= passiveItemLevels_.size()) {
		return false;
	}
	const int32_t level = passiveItemLevels_[index];
	return level < kPassiveItemMaxLevel &&
		(level > 0 || passiveItemAcquisitionOrder_.size() <
			kMaxEquippedPassiveItems);
}

bool PlayerManager::UpgradePassiveItem(PassiveItemType type)
{
	if (!CanAcquirePassiveItem(type)) {
		return false;
	}
	const PassiveItemDefinition& definition = GetPassiveItemDefinition(type);
	int32_t& level = passiveItemLevels_[PassiveItemIndex(type)];
	if (level == 0) {
		passiveItemAcquisitionOrder_.push_back(type);
	}
	++level;
	if (type == PassiveItemType::MaxHp) {
		progression_.IncreaseMaxHPBy(static_cast<int32_t>(definition.amount));
	} else {
		progression_.UpgradeStat(definition.statType, definition.amount);
		if (type == PassiveItemType::MoveSpeed) {
			progression_.ApplyCurrentMovementSpeed(player_);
		}
	}
	return true;
}

void PlayerManager::DebugSetPassiveItemLevel(PassiveItemType type, int32_t level)
{
	const size_t index = PassiveItemIndex(type);
	if (index >= passiveItemLevels_.size()) {
		return;
	}

	const PassiveItemDefinition& definition = GetPassiveItemDefinition(type);
	const int32_t targetLevel = std::clamp(level, 0, kPassiveItemMaxLevel);
	int32_t& currentLevel = passiveItemLevels_[index];
	const int32_t deltaLevel = targetLevel - currentLevel;
	if (deltaLevel == 0) {
		return;
	}

	if (currentLevel == 0 && targetLevel > 0 &&
		std::find(
			passiveItemAcquisitionOrder_.begin(),
			passiveItemAcquisitionOrder_.end(),
			type) == passiveItemAcquisitionOrder_.end()) {
		passiveItemAcquisitionOrder_.push_back(type);
	}

	if (definition.type == PassiveItemType::MaxHp) {
#ifdef _DEBUG
		progression_.DebugAdjustMaxHP(
			static_cast<int32_t>(definition.amount) * deltaLevel);
#else
		if (deltaLevel > 0) {
			progression_.IncreaseMaxHPBy(
				static_cast<int32_t>(definition.amount) * deltaLevel);
		}
#endif
	} else {
		progression_.UpgradeStat(
			definition.statType,
			definition.amount * static_cast<float>(deltaLevel));
		if (definition.type == PassiveItemType::MoveSpeed) {
			progression_.ApplyCurrentMovementSpeed(player_);
		}
	}

	currentLevel = targetLevel;
	if (currentLevel == 0) {
		std::erase(
			passiveItemAcquisitionOrder_,
			type);
	}
}

void PlayerManager::IncreaseMaxHP()
{
	progression_.IncreaseMaxHP();
}

void PlayerManager::UpgradeMoveSpeed()
{
	progression_.UpgradeMoveSpeed(player_);
}

void PlayerManager::ApplyPermanentUpgrades(
	int32_t maxHPLevel,
	int32_t attackLevel,
	int32_t moveSpeedLevel,
	int32_t expPickupRangeLevel,
	int32_t coinGainLevel)
{
	progression_.ApplyPermanentBonuses(
		player_,
		maxHPLevel,
		attackLevel,
		moveSpeedLevel,
		expPickupRangeLevel,
		coinGainLevel);
}

void PlayerManager::UpgradeNormalBullets()
{
	weapons_.UpgradeNormalBullets(player_);
}

void PlayerManager::AddOrbitBullets()
{
	weapons_.AddOrbitBullets(player_);
}

void PlayerManager::UpgradeOrbitBullets()
{
	weapons_.UpgradeOrbitBullets(player_);
}

void PlayerManager::AddLightning()
{
	weapons_.AddLightning();
}

void PlayerManager::UpgradeLightning()
{
	weapons_.UpgradeLightning();
}

void PlayerManager::AddExplosiveBullets()
{
	weapons_.AddExplosiveBullets();
}

void PlayerManager::UpgradeExplosiveBullets()
{
	weapons_.UpgradeExplosiveBullets();
}

void PlayerManager::MaxAllWeapons()
{
	weapons_.MaxAllWeapons(player_);
	ClearLevelUpRequest();
}

void PlayerManager::MakeDebugStrongest()
{
	MaxAllWeapons();
	progression_.MakeDebugStrongest(player_);
	if (player_) {
		player_->SetVisible(true);
	}
	invincible_ = false;
	invincibleTimer_ = 0.0f;
	visible_ = true;
	ClearLevelUpRequest();
}

void PlayerManager::PlayLevelUpEffect()
{
	AddEXP(0);
}

void PlayerManager::UpdateInvincibility(float deltaTime)
{
	if (!invincible_) {
		return;
	}

	invincibleTimer_ -= deltaTime;
	if (invincibleTimer_ <= 0.0f) {
		invincible_ = false;
		visible_ = true;
		if (player_) {
			player_->SetVisible(true);
		}
		return;
	}

	const int blink =
		static_cast<int>(invincibleTimer_ * 10.0f);
	visible_ = (blink % 2 == 0);
	if (player_) {
		player_->SetVisible(visible_);
	}
}

}
