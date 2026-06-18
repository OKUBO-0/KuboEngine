#include "game/directxgame/player/PlayerManager.h"

#include "game/directxgame/core/CsvReader.h"
#include "game/directxgame/core/DirectXGameResourcePaths.h"
#include <stdexcept>

namespace DirectXGame {

void PlayerManager::Initialize(Player* player)
{
	player_ = player;
	weapons_.Initialize(
		ResourcePaths::MakeDataPath(
			"weaponUpgradeSettings.csv"));
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
}

void PlayerManager::LoadWeaponUpgradeSettings(
	const std::string& filePath)
{
	weapons_.LoadUpgradeSettings(filePath);
}

void PlayerManager::Update(float deltaTime)
{
	UpdateInvincibility(deltaTime);
	weapons_.Update(
		deltaTime,
		player_,
		enemyManager_,
		GetAttackPower());
}

void PlayerManager::Draw()
{
	weapons_.Draw();
}

void PlayerManager::TakeDamage()
{
	if (invincible_) {
		return;
	}

	progression_.TakeDamage();
	invincible_ = true;
	invincibleTimer_ = invincibilityDuration_;
	visible_ = false;
	if (player_) {
		player_->SetVisible(false);
	}
}

void PlayerManager::RecoverHP()
{
	progression_.RecoverHP();
}

void PlayerManager::AddEXP(int32_t amount)
{
	progression_.AddEXP(amount);
}

void PlayerManager::IncreaseMaxHP()
{
	progression_.IncreaseMaxHP();
}

void PlayerManager::UpgradeMoveSpeed()
{
	progression_.UpgradeMoveSpeed(player_);
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

void PlayerManager::AddDrone()
{
	weapons_.AddDrone();
}

void PlayerManager::UpgradeDrone()
{
	weapons_.UpgradeDrone();
}

void PlayerManager::AddLightning()
{
	weapons_.AddLightning();
}

void PlayerManager::UpgradeLightning()
{
	weapons_.UpgradeLightning();
}

void PlayerManager::MaxAllWeapons()
{
	weapons_.MaxAllWeapons(player_);
	ClearLevelUpRequest();
}

#ifdef _DEBUG
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
#endif

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
