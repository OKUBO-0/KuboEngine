#include "game/directxgame/player/PlayerManager.h"
#include "game/directxgame/core/CsvReader.h"
#include "game/directxgame/core/DirectXGameResourcePaths.h"
#include "game/directxgame/enemy/EnemyManager.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace {

float ToFloatOr(const std::string& value, float fallback)
{
	try {
		return std::stof(value);
	} catch (...) {
		return fallback;
	}
}

int32_t ToIntOr(const std::string& value, int32_t fallback)
{
	try {
		return std::stoi(value);
	} catch (...) {
		return fallback;
	}
}

}

namespace DirectXGame {

void PlayerManager::Initialize(Player* player)
{
	player_ = player;
	LoadWeaponUpgradeSettings(ResourcePaths::MakeDataPath("weaponUpgradeSettings.csv"));
	if (player_) {
		player_->SetVisible(visible_);
	}
}

void PlayerManager::LoadStatusFromCSV(const std::string& filePath)
{
	const CsvReader::CsvTable rows = CsvReader::LoadRows(filePath);
	for (const CsvReader::CsvRow& row : rows) {
		if (row.size() < 2) {
			continue;
		}

		const std::string& key = row[0];
		const std::string& value = row[1];
		if (key == "level") { level_ = ToIntOr(value, level_); }
		else if (key == "nextLevelExp") { nextLevelExp_ = ToIntOr(value, nextLevelExp_); }
		else if (key == "maxLifeStock") { maxLifeStock_ = ToIntOr(value, maxLifeStock_); }
		else if (key == "lifeStock") { lifeStock_ = ToIntOr(value, lifeStock_); }
		else if (key == "exp") { exp_ = ToIntOr(value, exp_); }
		else if (key == "totalExp") { totalExp_ = ToIntOr(value, totalExp_); }
		else if (key == "attackPower") { attackPower_ = ToIntOr(value, attackPower_); }
		else if (key == "invincibilityDuration") { invincibilityDuration_ = ToFloatOr(value, invincibilityDuration_); }
		else if (key == "normalBulletInterval") { normalBulletInterval_ = ToFloatOr(value, normalBulletInterval_); }
		else if (key == "normalBulletUpgradeMultiplier") { normalBulletUpgradeMultiplier_ = ToFloatOr(value, normalBulletUpgradeMultiplier_); }
		else if (key == "normalBulletMinInterval") { normalBulletMinInterval_ = ToFloatOr(value, normalBulletMinInterval_); }
		else if (key == "droneInterval") { droneInterval_ = ToFloatOr(value, droneInterval_); }
		else if (key == "droneUpgradeMultiplier") { droneUpgradeMultiplier_ = ToFloatOr(value, droneUpgradeMultiplier_); }
		else if (key == "maxLifeStockCap") { maxLifeStockCap_ = ToIntOr(value, maxLifeStockCap_); }
		else if (key == "moveSpeedUpgradeCap") { moveSpeedUpgradeCap_ = ToIntOr(value, moveSpeedUpgradeCap_); }
		else if (key == "moveSpeedUpgradeStep") { moveSpeedUpgradeStep_ = ToFloatOr(value, moveSpeedUpgradeStep_); }
		else if (key == "moveSpeedMax") { moveSpeedMax_ = ToFloatOr(value, moveSpeedMax_); }
	}
}

void PlayerManager::LoadWeaponUpgradeSettings(const std::string& filePath)
{
	weaponUpgradeSettings_.clear();
	const CsvReader::CsvTable rows = CsvReader::LoadRows(filePath);
	for (const CsvReader::CsvRow& row : rows) {
		if (row.size() < 2 || row[0].empty() || row[1].empty()) {
			continue;
		}
		weaponUpgradeSettings_[row[0]] = ToFloatOr(row[1], 0.0f);
	}
}

void PlayerManager::Update(float deltaTime)
{
	UpdateInvincibility(deltaTime);
	UpdateNormalBullets(deltaTime);
	UpdateOrbitBullets(deltaTime);
	UpdateDrone(deltaTime);
	UpdateLightning(deltaTime);
}

void PlayerManager::Draw()
{
	for (std::unique_ptr<NormalBullet>& bullet : normalBullets_) {
		bullet->Draw();
	}
	for (std::unique_ptr<OrbitBullet>& bullet : orbitBullets_) {
		bullet->Draw();
	}
	if (hasDrone_ && drone_) {
		drone_->Draw();
	}
}

void PlayerManager::TakeDamage()
{
	if (invincible_) {
		return;
	}

	--lifeStock_;
	invincible_ = true;
	invincibleTimer_ = invincibilityDuration_;
	visible_ = false;
	if (player_) {
		player_->SetVisible(false);
	}
}

void PlayerManager::RecoverHP()
{
	lifeStock_ = (std::min)(lifeStock_ + 1, maxLifeStock_);
}

void PlayerManager::AddEXP(int32_t amount)
{
	exp_ += amount;
	totalExp_ += amount;
	while (exp_ >= nextLevelExp_) {
		exp_ -= nextLevelExp_;
		++level_;
		nextLevelExp_ = static_cast<int32_t>(static_cast<float>(nextLevelExp_) * 1.5f);
		levelUpRequested_ = true;
	}
}

void PlayerManager::IncreaseMaxHP()
{
	if (maxLifeStock_ >= maxLifeStockCap_) {
		RecoverHP();
		return;
	}

	++maxLifeStock_;
	lifeStock_ = maxLifeStock_;
}

void PlayerManager::UpgradeMoveSpeed()
{
	if (!player_) {
		return;
	}

	if (moveSpeedLevel_ >= moveSpeedUpgradeCap_) {
		UpgradeAttackPower();
		return;
	}

	const float upgradedSpeed = (std::min)(moveSpeedMax_, player_->GetMoveSpeed() + moveSpeedUpgradeStep_);
	player_->SetMoveSpeed(upgradedSpeed);
	++moveSpeedLevel_;
}

void PlayerManager::UpgradeNormalBullets()
{
	if (normalBulletLevel_ >= kNormalBulletMaxLevel) {
		return;
	}

	++normalBulletLevel_;
	switch (normalBulletLevel_) {
	case 2:
		normalBulletAmount_ = static_cast<int32_t>(GetWeaponUpgradeSetting("normal.lv2.amount", 2.0f));
		break;
	case 3:
		normalBulletSpeed_ *= GetWeaponUpgradeSetting("normal.lv3.speedMultiplier", 1.2f);
		normalBulletInterval_ *= GetWeaponUpgradeSetting("normal.lv3.intervalMultiplier", normalBulletUpgradeMultiplier_);
		break;
	case 4:
		normalBulletAmount_ = static_cast<int32_t>(GetWeaponUpgradeSetting("normal.lv4.amount", 3.0f));
		break;
	case 5:
		normalBulletDamageBonus_ += static_cast<int32_t>(GetWeaponUpgradeSetting("normal.lv5.damageBonusAdd", 1.0f));
		break;
	case 6:
		normalBulletAmount_ = static_cast<int32_t>(GetWeaponUpgradeSetting("normal.lv6.amount", 4.0f));
		break;
	case 7:
		normalBulletPierceCount_ = static_cast<int32_t>(GetWeaponUpgradeSetting("normal.lv7.pierceCount", 2.0f));
		break;
	case 8:
		normalBulletDamageBonus_ += static_cast<int32_t>(GetWeaponUpgradeSetting("normal.lv8.damageBonusAdd", 1.0f));
		normalBulletSpeed_ *= GetWeaponUpgradeSetting("normal.lv8.speedMultiplier", 1.15f);
		normalBulletInterval_ *= GetWeaponUpgradeSetting("normal.lv8.intervalMultiplier", normalBulletUpgradeMultiplier_);
		break;
	default:
		break;
	}

	normalBulletInterval_ = (std::max)(normalBulletMinInterval_, normalBulletInterval_);
}

void PlayerManager::AddOrbitBullets()
{
	hasOrbitBullets_ = true;
	orbitBulletLevel_ = 1;
	orbitBulletCount_ = static_cast<int32_t>(GetWeaponUpgradeSetting("orbit.lv1.count", 1.0f));
	orbitBulletScale_ = GetWeaponUpgradeSetting("orbit.lv1.scale", 1.0f);
	orbitHitInterval_ = GetWeaponUpgradeSetting("orbit.lv1.hitInterval", 0.5f);
	RebuildOrbitBullets();
}

void PlayerManager::UpgradeOrbitBullets()
{
	if (!hasOrbitBullets_) {
		AddOrbitBullets();
		return;
	}
	if (orbitBulletLevel_ >= kOrbitBulletMaxLevel) {
		return;
	}

	++orbitBulletLevel_;
	switch (orbitBulletLevel_) {
	case 2:
		orbitBulletCount_ = static_cast<int32_t>(GetWeaponUpgradeSetting("orbit.lv2.count", 2.0f));
		break;
	case 3:
		orbitRadius_ += GetWeaponUpgradeSetting("orbit.lv3.radiusAdd", orbitRadiusUpgradeStep_);
		orbitAngularSpeed_ += GetWeaponUpgradeSetting("orbit.lv3.angularSpeedAdd", orbitAngularSpeedUpgradeStep_);
		orbitBulletScale_ += GetWeaponUpgradeSetting("orbit.lv3.scaleAdd", orbitBulletScaleUpgradeStep_);
		break;
	case 4:
		orbitHitInterval_ *= GetWeaponUpgradeSetting("orbit.lv4.hitIntervalMultiplier", orbitHitIntervalUpgradeMultiplier_);
		break;
	case 5:
		orbitBulletCount_ = static_cast<int32_t>(GetWeaponUpgradeSetting("orbit.lv5.count", 3.0f));
		break;
	case 6:
		orbitRadius_ += GetWeaponUpgradeSetting("orbit.lv6.radiusAdd", orbitRadiusUpgradeStep_);
		orbitAngularSpeed_ += GetWeaponUpgradeSetting("orbit.lv6.angularSpeedAdd", orbitAngularSpeedUpgradeStep_);
		orbitBulletScale_ += GetWeaponUpgradeSetting("orbit.lv6.scaleAdd", orbitBulletScaleUpgradeStep_);
		break;
	case 7:
		orbitHitInterval_ *= GetWeaponUpgradeSetting("orbit.lv7.hitIntervalMultiplier", orbitHitIntervalUpgradeMultiplier_);
		break;
	case 8:
		orbitBulletCount_ = static_cast<int32_t>(GetWeaponUpgradeSetting("orbit.lv8.count", 4.0f));
		break;
	default:
		break;
	}

	RebuildOrbitBullets();
}

void PlayerManager::AddDrone()
{
	hasDrone_ = true;
	droneLevel_ = 1;
	droneShotCount_ = static_cast<int32_t>(GetWeaponUpgradeSetting("drone.lv1.shotCount", 1.0f));
	droneDamageBonus_ = static_cast<int32_t>(GetWeaponUpgradeSetting("drone.lv1.damageBonus", 0.0f));
	dronePierceCount_ = static_cast<int32_t>(GetWeaponUpgradeSetting("drone.lv1.pierceCount", 1.0f));
	drone_ = std::make_unique<Drone>();
	drone_->SetLightSettings(lightSettings_);
	drone_->Initialize({ 3.0f, 2.0f, 0.0f });
}

void PlayerManager::UpgradeDrone()
{
	if (!hasDrone_) {
		AddDrone();
		return;
	}
	if (droneLevel_ >= kDroneMaxLevel) {
		return;
	}

	++droneLevel_;
	switch (droneLevel_) {
	case 2:
		droneShotCount_ = static_cast<int32_t>(GetWeaponUpgradeSetting("drone.lv2.shotCount", 2.0f));
		break;
	case 3:
		droneInterval_ *= GetWeaponUpgradeSetting("drone.lv3.intervalMultiplier", droneUpgradeMultiplier_);
		break;
	case 4:
		droneShotCount_ = static_cast<int32_t>(GetWeaponUpgradeSetting("drone.lv4.shotCount", 3.0f));
		break;
	case 5:
		droneDamageBonus_ += static_cast<int32_t>(GetWeaponUpgradeSetting("drone.lv5.damageBonusAdd", 1.0f));
		break;
	case 6:
		droneShotCount_ = static_cast<int32_t>(GetWeaponUpgradeSetting("drone.lv6.shotCount", 4.0f));
		break;
	case 7:
		dronePierceCount_ = static_cast<int32_t>(GetWeaponUpgradeSetting("drone.lv7.pierceCount", 2.0f));
		break;
	case 8:
		droneDamageBonus_ += static_cast<int32_t>(GetWeaponUpgradeSetting("drone.lv8.damageBonusAdd", 1.0f));
		droneInterval_ *= GetWeaponUpgradeSetting("drone.lv8.intervalMultiplier", droneUpgradeMultiplier_);
		break;
	default:
		break;
	}
}

void PlayerManager::AddLightning()
{
	hasLightning_ = true;
	lightningLevel_ = 1;
	lightningStrikeCount_ = static_cast<int32_t>(GetWeaponUpgradeSetting("lightning.lv1.strikeCount", 1.0f));
	lightningDamageBonus_ = static_cast<int32_t>(GetWeaponUpgradeSetting("lightning.lv1.damageBonus", 0.0f));
	lightningRadius_ = GetWeaponUpgradeSetting("lightning.lv1.radius", 6.0f);
	lightningInterval_ = GetWeaponUpgradeSetting("lightning.lv1.interval", 2.4f);
}

void PlayerManager::UpgradeLightning()
{
	if (!hasLightning_) {
		AddLightning();
		return;
	}
	if (lightningLevel_ >= kLightningMaxLevel) {
		return;
	}

	++lightningLevel_;
	switch (lightningLevel_) {
	case 2:
		lightningDamageBonus_ += static_cast<int32_t>(GetWeaponUpgradeSetting("lightning.lv2.damageBonusAdd", 1.0f));
		break;
	case 3:
		lightningStrikeCount_ = static_cast<int32_t>(GetWeaponUpgradeSetting("lightning.lv3.strikeCount", 2.0f));
		break;
	case 4:
		lightningRadius_ += GetWeaponUpgradeSetting("lightning.lv4.radiusAdd", 1.5f);
		break;
	case 5:
		lightningDamageBonus_ += static_cast<int32_t>(GetWeaponUpgradeSetting("lightning.lv5.damageBonusAdd", 1.0f));
		break;
	case 6:
		lightningStrikeCount_ = static_cast<int32_t>(GetWeaponUpgradeSetting("lightning.lv6.strikeCount", 3.0f));
		break;
	case 7:
		lightningInterval_ *= GetWeaponUpgradeSetting("lightning.lv7.intervalMultiplier", 0.82f);
		break;
	case 8:
		lightningStrikeCount_ = static_cast<int32_t>(GetWeaponUpgradeSetting("lightning.lv8.strikeCount", 4.0f));
		lightningRadius_ += GetWeaponUpgradeSetting("lightning.lv8.radiusAdd", 1.5f);
		break;
	default:
		break;
	}
}

void PlayerManager::MaxAllWeapons()
{
	while (!IsNormalBulletMaxLevel()) {
		UpgradeNormalBullets();
	}
	while (!IsOrbitBulletMaxLevel()) {
		UpgradeOrbitBullets();
	}
	while (!IsDroneMaxLevel()) {
		UpgradeDrone();
	}
	while (!IsLightningMaxLevel()) {
		UpgradeLightning();
	}
	ClearLevelUpRequest();
}

void PlayerManager::PlayLevelUpEffect()
{
	AddEXP(0);
}

void PlayerManager::SetLightSettings(const GameLightSettings& lightSettings)
{
	lightSettings_ = lightSettings;
	for (std::unique_ptr<NormalBullet>& bullet : normalBullets_) {
		bullet->SetLightSettings(lightSettings_);
	}
	for (std::unique_ptr<OrbitBullet>& bullet : orbitBullets_) {
		bullet->SetLightSettings(lightSettings_);
	}
	if (drone_) {
		drone_->SetLightSettings(lightSettings_);
	}
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

	const int blink = static_cast<int>(invincibleTimer_ * 10.0f);
	visible_ = (blink % 2 == 0);
	if (player_) {
		player_->SetVisible(visible_);
	}
}

void PlayerManager::UpdateNormalBullets(float deltaTime)
{
	if (hasNormalBullets_ && player_) {
		normalBulletTimer_ += deltaTime;
		while (normalBulletTimer_ >= normalBulletInterval_) {
			const float angle = player_->GetWorldRotationY();
			const Vector3 forward{ std::sin(angle), 0.0f, std::cos(angle) };
			const Vector3 right{ forward.z, 0.0f, -forward.x };
			const float centerOffset = static_cast<float>(normalBulletAmount_ - 1) * 0.5f;

			for (int32_t i = 0; i < normalBulletAmount_; ++i) {
				Vector3 startPosition = player_->GetWorldPosition();
				const float horizontalOffset = (static_cast<float>(i) - centerOffset) * 1.25f;
				startPosition.x += right.x * horizontalOffset;
				startPosition.z += right.z * horizontalOffset;

				auto bullet = std::make_unique<NormalBullet>();
				bullet->SetLightSettings(lightSettings_);
				bullet->InitializeForward(startPosition, forward, normalBulletSpeed_, normalBulletRange_, normalBulletPierceCount_);
				normalBullets_.push_back(std::move(bullet));
				peakNormalBulletCount_ = (std::max)(peakNormalBulletCount_, normalBullets_.size());
				if (normalBullets_.size() > PlayerManager::kMaxActiveNormalBullets) {
					normalBullets_.erase(normalBullets_.begin());
					++normalBulletPruneCount_;
				}
			}

			normalBulletTimer_ -= normalBulletInterval_;
		}
	}

	for (std::unique_ptr<NormalBullet>& bullet : normalBullets_) {
		bullet->Update(player_ ? player_->GetWorldPosition() : Vector3{}, deltaTime);
	}
	normalBullets_.erase(
		std::remove_if(
			normalBullets_.begin(),
			normalBullets_.end(),
			[](const std::unique_ptr<NormalBullet>& bullet) { return !bullet->IsActive(); }),
		normalBullets_.end());
}

void PlayerManager::UpdateOrbitBullets(float deltaTime)
{
	if (!hasOrbitBullets_ || !player_) {
		return;
	}

	for (std::unique_ptr<OrbitBullet>& bullet : orbitBullets_) {
		bullet->Update(player_->GetWorldPosition(), deltaTime);
	}
}

void PlayerManager::UpdateDrone(float deltaTime)
{
	if (!hasDrone_ || !drone_ || !player_) {
		return;
	}

	float fireAngleY = player_->GetWorldRotationY();
	if (enemyManager_) {
		Vector3 targetPosition{};
		if (enemyManager_->FindNearestEnemyPosition(player_->GetWorldPosition(), droneBulletRange_ * 2.0f, targetPosition)) {
			const Vector3 dronePosition = drone_->GetPosition();
			const float dx = targetPosition.x - dronePosition.x;
			const float dz = targetPosition.z - dronePosition.z;
			if (std::abs(dx) > 0.001f || std::abs(dz) > 0.001f) {
				fireAngleY = std::atan2(dx, dz);
			}
		}
	}

	drone_->Update(
		player_->GetWorldPosition(),
		player_->GetWorldRotationY(),
		fireAngleY,
		droneTimer_,
		droneInterval_,
		droneShotCount_,
		droneBulletSpeed_,
		droneBulletRange_,
		dronePierceCount_,
		deltaTime);
}

void PlayerManager::UpdateLightning(float deltaTime)
{
	if (lightningEffectTimer_ > 0.0f) {
		lightningEffectTimer_ = (std::max)(0.0f, lightningEffectTimer_ - deltaTime);
		if (lightningEffectTimer_ <= 0.0f) {
			lightningEffectTargets_.clear();
		}
	}

	if (!hasLightning_ || !enemyManager_) {
		return;
	}

	lightningTimer_ += deltaTime;
	while (lightningTimer_ >= lightningInterval_) {
		const std::vector<Vector3> targets = enemyManager_->PickLightningTargets(lightningStrikeCount_);
		if (!targets.empty()) {
			lightningEffectTargets_ = targets;
			lightningEffectTimer_ = 0.22f;
		}
		for (const Vector3& target : targets) {
			enemyManager_->ApplyLightningDamage(target, lightningRadius_, GetLightningDamage());
		}
		lightningTimer_ -= lightningInterval_;
		if (targets.empty()) {
			break;
		}
	}
}

void PlayerManager::RebuildOrbitBullets()
{
	if (!player_) {
		orbitBullets_.clear();
		return;
	}

	orbitBullets_.clear();
	orbitBullets_.reserve(orbitBulletCount_);
	for (int32_t i = 0; i < orbitBulletCount_; ++i) {
		const float angle = (2.0f * std::numbers::pi_v<float> * static_cast<float>(i)) / static_cast<float>(orbitBulletCount_);
		auto bullet = std::make_unique<OrbitBullet>();
		bullet->SetLightSettings(lightSettings_);
		bullet->Initialize(player_->GetWorldPosition(), orbitRadius_, angle, orbitAngularSpeed_, orbitBulletScale_, orbitHitInterval_);
		orbitBullets_.push_back(std::move(bullet));
	}
}

float PlayerManager::GetWeaponUpgradeSetting(const std::string& key, float fallback) const
{
	const auto it = weaponUpgradeSettings_.find(key);
	if (it == weaponUpgradeSettings_.end()) {
		return fallback;
	}
	return it->second;
}

} // namespace DirectXGame
