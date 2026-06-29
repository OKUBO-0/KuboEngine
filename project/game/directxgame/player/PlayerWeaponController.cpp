#include "PlayerWeaponController.h"
#include "WeaponUpgradeData.h"

#include "CsvReader.h"
#include "GameplayRules.h"
#include "EnemyManager.h"
#include "Player.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace {

constexpr float kMinWeaponInterval =
	DirectXGame::GameplayRules::kMinimumWeaponInterval;
constexpr int32_t kMaxCatchUpAttacksPerFrame = 4;

float PositiveFiniteOr(float value, float fallback)
{
	return std::isfinite(value) && value > 0.0f
		? value
		: fallback;
}

}

namespace DirectXGame {

void PlayerWeaponController::Initialize(
	const std::string& upgradeSettingsPath)
{
	LoadUpgradeSettings(upgradeSettingsPath);
}

bool PlayerWeaponController::LoadStatusValue(
	const std::string& key,
	const std::string& value)
{
	if (key == "normalBulletInterval") {
		normalBulletInterval_ = CsvReader::ParseFloat(
			value,
			"playerStatus.normalBulletInterval");
	} else if (key == "normalBulletMinInterval") {
		normalBulletMinInterval_ = CsvReader::ParseFloat(
			value,
			"playerStatus.normalBulletMinInterval");
	} else if (key == "droneInterval") {
		droneInterval_ = CsvReader::ParseFloat(
			value,
			"playerStatus.droneInterval");
	} else if (key == "explosiveBulletInterval") {
		explosiveBulletInterval_ = CsvReader::ParseFloat(
			value,
			"playerStatus.explosiveBulletInterval");
	} else {
		return false;
	}
	if (normalBulletInterval_ <= 0.0f ||
		normalBulletMinInterval_ <= 0.0f ||
		droneInterval_ <= 0.0f ||
		explosiveBulletInterval_ <= 0.0f) {
		throw std::runtime_error(
			"playerStatus contains an invalid weapon interval value");
	}
	normalBulletInterval_ = GameplayRules::NormalizeWeaponInterval(
		normalBulletInterval_,
		normalBulletMinInterval_);
	normalBulletMinInterval_ = (std::max)(
		kMinWeaponInterval,
		normalBulletMinInterval_);
	droneInterval_ = GameplayRules::NormalizeWeaponInterval(
		droneInterval_,
		kMinWeaponInterval);
	explosiveBulletInterval_ = GameplayRules::NormalizeWeaponInterval(
		explosiveBulletInterval_,
		kMinWeaponInterval);
	return true;
}

void PlayerWeaponController::LoadUpgradeSettings(
	const std::string& filePath)
{
	upgradeSettings_.clear();
	const CsvReader::CsvTable rows = CsvReader::LoadRows(filePath);
	for (const CsvReader::CsvRow& row : rows) {
		if (row.size() < 2 || row[0].empty() || row[1].empty()) {
			continue;
		}
		const float setting = CsvReader::ParseFloat(
			row[1],
			"weaponUpgradeSettings." + row[0]);
		const bool zeroAllowed =
			row[0].find("damageBonus") != std::string::npos;
		if (setting < 0.0f || (!zeroAllowed && setting == 0.0f)) {
			throw std::runtime_error(
				"weaponUpgradeSettings." + row[0] +
				" must be positive");
		}
		upgradeSettings_[row[0]] = setting;
	}
}

void PlayerWeaponController::Update(
	float deltaTime,
	Player* player,
	EnemyManager* enemyManager,
	int32_t attackPower)
{
	UpdateNormalBullets(deltaTime, player);
	UpdateOrbitBullets(deltaTime, player);
	UpdateDrone(deltaTime, player, enemyManager);
	UpdateLightning(deltaTime, enemyManager, attackPower);
	UpdateExplosiveBullets(deltaTime, player);
}

void PlayerWeaponController::Draw()
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
	for (std::unique_ptr<NormalBullet>& bullet : explosiveBullets_) {
		bullet->Draw();
	}
}

void PlayerWeaponController::UpgradeNormalBullets(Player*)
{
	if (IsNormalBulletMaxLevel()) {
		return;
	}
	++normalBulletLevel_;
	ApplyNormalBulletUpgradeLevel(normalBulletLevel_);
	normalBulletInterval_ = (std::max)(
		normalBulletMinInterval_,
		PositiveFiniteOr(
			normalBulletInterval_,
			normalBulletMinInterval_));
}

void PlayerWeaponController::AddOrbitBullets(Player* player)
{
	hasOrbitBullets_ = true;
	orbitBulletLevel_ = 1;
	ApplyOrbitUpgradeLevel(orbitBulletLevel_);
	RebuildOrbitBullets(player);
}

void PlayerWeaponController::UpgradeOrbitBullets(Player* player)
{
	if (!hasOrbitBullets_) {
		AddOrbitBullets(player);
		return;
	}
	if (IsOrbitBulletMaxLevel()) {
		return;
	}
	++orbitBulletLevel_;
	ApplyOrbitUpgradeLevel(orbitBulletLevel_);
	RebuildOrbitBullets(player);
}

void PlayerWeaponController::AddDrone()
{
	hasDrone_ = true;
	droneLevel_ = 1;
	ApplyDroneUpgradeLevel(droneLevel_);
	drone_ = std::make_unique<Drone>();
	drone_->Initialize({ 3.0f, 2.0f, 0.0f });
}

void PlayerWeaponController::UpgradeDrone()
{
	if (!hasDrone_) {
		AddDrone();
		return;
	}
	if (IsDroneMaxLevel()) {
		return;
	}
	++droneLevel_;
	ApplyDroneUpgradeLevel(droneLevel_);
	droneInterval_ = (std::max)(
		kMinWeaponInterval,
		PositiveFiniteOr(droneInterval_, kMinWeaponInterval));
}

void PlayerWeaponController::AddLightning()
{
	hasLightning_ = true;
	lightningLevel_ = 1;
	ApplyLightningUpgradeLevel(lightningLevel_);
}

void PlayerWeaponController::UpgradeLightning()
{
	if (!hasLightning_) {
		AddLightning();
		return;
	}
	if (IsLightningMaxLevel()) {
		return;
	}
	++lightningLevel_;
	ApplyLightningUpgradeLevel(lightningLevel_);
	lightningInterval_ = (std::max)(
		kMinWeaponInterval,
		PositiveFiniteOr(lightningInterval_, kMinWeaponInterval));
}

void PlayerWeaponController::AddExplosiveBullets()
{
	hasExplosiveBullets_ = true;
	explosiveBulletLevel_ = 1;
	ApplyExplosiveBulletUpgradeLevel(explosiveBulletLevel_);
}

void PlayerWeaponController::UpgradeExplosiveBullets()
{
	if (!hasExplosiveBullets_) {
		AddExplosiveBullets();
		return;
	}
	if (IsExplosiveBulletMaxLevel()) {
		return;
	}
	++explosiveBulletLevel_;
	ApplyExplosiveBulletUpgradeLevel(explosiveBulletLevel_);
	explosiveBulletInterval_ = (std::max)(
		kMinWeaponInterval,
		PositiveFiniteOr(explosiveBulletInterval_, kMinWeaponInterval));
}

void PlayerWeaponController::UpgradeWeapon(WeaponType type, Player* player)
{
	switch (type) {
	case WeaponType::NormalBullet:
		UpgradeNormalBullets(player);
		break;
	case WeaponType::OrbitBullet:
		UpgradeOrbitBullets(player);
		break;
	case WeaponType::Drone:
		UpgradeDrone();
		break;
	case WeaponType::Lightning:
		UpgradeLightning();
		break;
	case WeaponType::ExplosiveBullet:
		UpgradeExplosiveBullets();
		break;
	}
}

bool PlayerWeaponController::IsWeaponMaxLevel(WeaponType type) const
{
	switch (type) {
	case WeaponType::NormalBullet:
		return IsNormalBulletMaxLevel();
	case WeaponType::OrbitBullet:
		return IsOrbitBulletMaxLevel();
	case WeaponType::Drone:
		return IsDroneMaxLevel();
	case WeaponType::Lightning:
		return IsLightningMaxLevel();
	case WeaponType::ExplosiveBullet:
		return IsExplosiveBulletMaxLevel();
	}
	return true;
}

void PlayerWeaponController::MaxAllWeapons(Player* player)
{
	for (WeaponType type : kUpgradeableWeaponTypes) {
		while (!IsWeaponMaxLevel(type)) {
			UpgradeWeapon(type, player);
		}
	}
}

int32_t PlayerWeaponController::GetDroneDamage(
	int32_t attackPower) const
{
	return (std::max)(
		1,
		attackPower / 2 + droneDamageBonus_);
}

void PlayerWeaponController::ResetBulletTelemetry()
{
	peakNormalBulletCount_ = normalBullets_.size();
	normalBulletPruneCount_ = 0;
	if (drone_) {
		drone_->ResetBulletTelemetry();
	}
}

void PlayerWeaponController::UpdateNormalBullets(
	float deltaTime,
	Player* player)
{
	if (hasNormalBullets_ && player) {
		normalBulletTimer_ += deltaTime;
		int32_t catchUpAttackCount = 0;
		while (normalBulletTimer_ >= normalBulletInterval_ &&
			catchUpAttackCount < kMaxCatchUpAttacksPerFrame) {
			const float angle = player->GetWorldRotationY();
			const Vector3 forward{
				std::sin(angle),
				0.0f,
				std::cos(angle),
			};
			const Vector3 right{
				forward.z,
				0.0f,
				-forward.x,
			};
			const float centerOffset =
				static_cast<float>(normalBulletAmount_ - 1) *
				0.5f;
			for (int32_t index = 0;
				index < normalBulletAmount_;
				++index) {
				Vector3 startPosition = player->GetWorldPosition();
				const float horizontalOffset =
					(static_cast<float>(index) - centerOffset) *
					1.25f;
				startPosition.x += right.x * horizontalOffset;
				startPosition.z += right.z * horizontalOffset;
				NormalBullet& bullet = AcquireNormalBullet();
				bullet.InitializeForward(
					startPosition,
					forward,
					normalBulletSpeed_,
					normalBulletRange_,
					normalBulletPierceCount_);
				peakNormalBulletCount_ = (std::max)(
					peakNormalBulletCount_,
					normalBullets_.size());
				if (normalBullets_.size() >
					kMaxActiveNormalBullets) {
					RecycleNormalBullet(0);
					++normalBulletPruneCount_;
				}
			}
			normalBulletTimer_ -= normalBulletInterval_;
			++catchUpAttackCount;
		}
		if (normalBulletTimer_ >= normalBulletInterval_) {
			normalBulletTimer_ = std::fmod(
				normalBulletTimer_,
				normalBulletInterval_);
		}
	}

	for (std::unique_ptr<NormalBullet>& bullet : normalBullets_) {
		bullet->Update(
			player ? player->GetWorldPosition() : Vector3{},
			deltaTime);
	}
	RecycleInactiveNormalBullets();
}

NormalBullet& PlayerWeaponController::AcquireNormalBullet()
{
	if (normalBulletPool_.empty()) {
		normalBullets_.push_back(std::make_unique<NormalBullet>());
		return *normalBullets_.back();
	}

	normalBullets_.push_back(std::move(normalBulletPool_.back()));
	normalBulletPool_.pop_back();
	return *normalBullets_.back();
}

void PlayerWeaponController::RecycleNormalBullet(size_t index)
{
	if (index >= normalBullets_.size()) {
		return;
	}

	if (normalBullets_[index]) {
		normalBullets_[index]->Deactivate();
		normalBulletPool_.push_back(std::move(normalBullets_[index]));
	}
	if (index != normalBullets_.size() - 1) {
		normalBullets_[index] = std::move(normalBullets_.back());
	}
	normalBullets_.pop_back();
}

void PlayerWeaponController::RecycleInactiveNormalBullets()
{
	for (size_t index = 0; index < normalBullets_.size();) {
		if (normalBullets_[index] && normalBullets_[index]->IsActive()) {
			++index;
			continue;
		}
		RecycleNormalBullet(index);
	}
}

void PlayerWeaponController::UpdateExplosiveBullets(
	float deltaTime,
	Player* player)
{
	if (hasExplosiveBullets_ && player) {
		explosiveBulletTimer_ += deltaTime;
		int32_t catchUpAttackCount = 0;
		while (explosiveBulletTimer_ >= explosiveBulletInterval_ &&
			catchUpAttackCount < kMaxCatchUpAttacksPerFrame) {
			const float angle = player->GetWorldRotationY();
			const Vector3 forward{
				std::sin(angle),
				0.0f,
				std::cos(angle),
			};
			NormalBullet& bullet = AcquireExplosiveBullet();
			bullet.InitializeForward(
				player->GetWorldPosition(),
				forward,
				explosiveBulletSpeed_,
				explosiveBulletRange_,
				1);
			explosiveBulletTimer_ -= explosiveBulletInterval_;
			++catchUpAttackCount;
		}
		if (explosiveBulletTimer_ >= explosiveBulletInterval_) {
			explosiveBulletTimer_ = std::fmod(
				explosiveBulletTimer_,
				explosiveBulletInterval_);
		}
	}

	for (std::unique_ptr<NormalBullet>& bullet : explosiveBullets_) {
		bullet->Update(
			player ? player->GetWorldPosition() : Vector3{},
			deltaTime);
	}
	RecycleInactiveExplosiveBullets();
}

NormalBullet& PlayerWeaponController::AcquireExplosiveBullet()
{
	if (explosiveBulletPool_.empty()) {
		explosiveBullets_.push_back(std::make_unique<NormalBullet>());
		return *explosiveBullets_.back();
	}

	explosiveBullets_.push_back(std::move(explosiveBulletPool_.back()));
	explosiveBulletPool_.pop_back();
	return *explosiveBullets_.back();
}

void PlayerWeaponController::RecycleExplosiveBullet(size_t index)
{
	if (index >= explosiveBullets_.size()) {
		return;
	}

	if (explosiveBullets_[index]) {
		explosiveBullets_[index]->Deactivate();
		explosiveBulletPool_.push_back(std::move(explosiveBullets_[index]));
	}
	if (index != explosiveBullets_.size() - 1) {
		explosiveBullets_[index] = std::move(explosiveBullets_.back());
	}
	explosiveBullets_.pop_back();
}

void PlayerWeaponController::RecycleInactiveExplosiveBullets()
{
	for (size_t index = 0; index < explosiveBullets_.size();) {
		if (explosiveBullets_[index] && explosiveBullets_[index]->IsActive()) {
			++index;
			continue;
		}
		RecycleExplosiveBullet(index);
	}
}

void PlayerWeaponController::UpdateOrbitBullets(
	float deltaTime,
	Player* player)
{
	if (!hasOrbitBullets_ || !player) {
		return;
	}
	for (std::unique_ptr<OrbitBullet>& bullet : orbitBullets_) {
		bullet->Update(player->GetWorldPosition(), deltaTime);
	}
}

void PlayerWeaponController::UpdateDrone(
	float deltaTime,
	Player* player,
	EnemyManager* enemyManager)
{
	if (!hasDrone_ || !drone_ || !player) {
		return;
	}

	float fireAngleY = player->GetWorldRotationY();
	if (enemyManager) {
		Vector3 targetPosition{};
		if (enemyManager->FindNearestEnemyPosition(
				player->GetWorldPosition(),
				droneBulletRange_ * 2.0f,
				targetPosition)) {
			const Vector3 dronePosition = drone_->GetPosition();
			const float dx =
				targetPosition.x - dronePosition.x;
			const float dz =
				targetPosition.z - dronePosition.z;
			if (std::abs(dx) > 0.001f ||
				std::abs(dz) > 0.001f) {
				fireAngleY = std::atan2(dx, dz);
			}
		}
	}
	drone_->Update(
		player->GetWorldPosition(),
		player->GetWorldRotationY(),
		fireAngleY,
		droneTimer_,
		droneInterval_,
		droneShotCount_,
		droneBulletSpeed_,
		droneBulletRange_,
		dronePierceCount_,
		deltaTime);
}

void PlayerWeaponController::UpdateLightning(
	float deltaTime,
	EnemyManager* enemyManager,
	int32_t attackPower)
{
	if (lightningEffectTimer_ > 0.0f) {
		lightningEffectTimer_ = (std::max)(
			0.0f,
			lightningEffectTimer_ - deltaTime);
		if (lightningEffectTimer_ <= 0.0f) {
			lightningEffectTargets_.clear();
		}
	}
	if (!hasLightning_ || !enemyManager) {
		return;
	}

	lightningTimer_ += deltaTime;
	int32_t catchUpAttackCount = 0;
	while (lightningTimer_ >= lightningInterval_ &&
		catchUpAttackCount < kMaxCatchUpAttacksPerFrame) {
		const std::vector<Vector3> targets =
			enemyManager->PickLightningTargets(
				lightningStrikeCount_);
		if (!targets.empty()) {
			lightningEffectTargets_ = targets;
			lightningEffectTimer_ = 0.22f;
		}
		for (const Vector3& target : targets) {
			enemyManager->ApplyLightningDamage(
				target,
				lightningRadius_,
				GetLightningDamage(attackPower));
		}
		lightningTimer_ -= lightningInterval_;
		++catchUpAttackCount;
		if (targets.empty()) {
			break;
		}
	}
	if (lightningTimer_ >= lightningInterval_) {
		lightningTimer_ = std::fmod(
			lightningTimer_,
			lightningInterval_);
	}
}

void PlayerWeaponController::RebuildOrbitBullets(Player* player)
{
	if (!player) {
		orbitBullets_.clear();
		return;
	}
	orbitBullets_.clear();
	orbitBullets_.reserve(orbitBulletCount_);
	for (int32_t index = 0; index < orbitBulletCount_; ++index) {
		const float angle =
			(2.0f * std::numbers::pi_v<float> *
				static_cast<float>(index)) /
			static_cast<float>(orbitBulletCount_);
		auto bullet = std::make_unique<OrbitBullet>();
		bullet->Initialize(
			player->GetWorldPosition(),
			orbitRadius_,
			angle,
			orbitAngularSpeed_,
			orbitBulletScale_,
			orbitHitInterval_);
		orbitBullets_.push_back(std::move(bullet));
	}
}

void PlayerWeaponController::ApplyNormalBulletUpgradeLevel(int32_t level)
{
	normalBulletAmount_ = GetLevelUpgradeSettingInt(
		"normal",
		level,
		"amount",
		normalBulletAmount_);
	normalBulletDamageBonus_ += GetLevelUpgradeSettingInt(
		"normal",
		level,
		"damageBonusAdd",
		0);
	normalBulletPierceCount_ = GetLevelUpgradeSettingInt(
		"normal",
		level,
		"pierceCount",
		normalBulletPierceCount_);
	normalBulletSpeed_ *= GetLevelUpgradeSetting(
		"normal",
		level,
		"speedMultiplier",
		1.0f);
	normalBulletInterval_ *= GetLevelUpgradeSetting(
		"normal",
		level,
		"intervalMultiplier",
		1.0f);
}

void PlayerWeaponController::ApplyOrbitUpgradeLevel(int32_t level)
{
	orbitBulletCount_ = GetLevelUpgradeSettingInt(
		"orbit",
		level,
		"count",
		orbitBulletCount_);
	orbitRadius_ += GetLevelUpgradeSetting(
		"orbit",
		level,
		"radiusAdd",
		0.0f);
	orbitAngularSpeed_ += GetLevelUpgradeSetting(
		"orbit",
		level,
		"angularSpeedAdd",
		0.0f);
	orbitBulletScale_ += GetLevelUpgradeSetting(
		"orbit",
		level,
		"scaleAdd",
		0.0f);
	orbitBulletScale_ = GetLevelUpgradeSetting(
		"orbit",
		level,
		"scale",
		orbitBulletScale_);
	orbitHitInterval_ = GetLevelUpgradeSetting(
		"orbit",
		level,
		"hitInterval",
		orbitHitInterval_);
	orbitHitInterval_ *= GetLevelUpgradeSetting(
		"orbit",
		level,
		"hitIntervalMultiplier",
		1.0f);
}

void PlayerWeaponController::ApplyDroneUpgradeLevel(int32_t level)
{
	droneShotCount_ = GetLevelUpgradeSettingInt(
		"drone",
		level,
		"shotCount",
		droneShotCount_);
	droneDamageBonus_ = GetLevelUpgradeSettingInt(
		"drone",
		level,
		"damageBonus",
		droneDamageBonus_);
	droneDamageBonus_ += GetLevelUpgradeSettingInt(
		"drone",
		level,
		"damageBonusAdd",
		0);
	dronePierceCount_ = GetLevelUpgradeSettingInt(
		"drone",
		level,
		"pierceCount",
		dronePierceCount_);
	droneInterval_ *= GetLevelUpgradeSetting(
		"drone",
		level,
		"intervalMultiplier",
		1.0f);
}

void PlayerWeaponController::ApplyLightningUpgradeLevel(int32_t level)
{
	lightningStrikeCount_ = GetLevelUpgradeSettingInt(
		"lightning",
		level,
		"strikeCount",
		lightningStrikeCount_);
	lightningDamageBonus_ = GetLevelUpgradeSettingInt(
		"lightning",
		level,
		"damageBonus",
		lightningDamageBonus_);
	lightningDamageBonus_ += GetLevelUpgradeSettingInt(
		"lightning",
		level,
		"damageBonusAdd",
		0);
	lightningRadius_ = GetLevelUpgradeSetting(
		"lightning",
		level,
		"radius",
		lightningRadius_);
	lightningRadius_ += GetLevelUpgradeSetting(
		"lightning",
		level,
		"radiusAdd",
		0.0f);
	lightningInterval_ = GetLevelUpgradeSetting(
		"lightning",
		level,
		"interval",
		lightningInterval_);
	lightningInterval_ *= GetLevelUpgradeSetting(
		"lightning",
		level,
		"intervalMultiplier",
		1.0f);
	lightningInterval_ = (std::max)(
		kMinWeaponInterval,
		PositiveFiniteOr(lightningInterval_, kMinWeaponInterval));
}

void PlayerWeaponController::ApplyExplosiveBulletUpgradeLevel(int32_t level)
{
	explosiveBulletDamageBonus_ = GetLevelUpgradeSettingInt(
		"explosive",
		level,
		"damageBonus",
		explosiveBulletDamageBonus_);
	explosiveBulletDamageBonus_ += GetLevelUpgradeSettingInt(
		"explosive",
		level,
		"damageBonusAdd",
		0);
	explosiveBulletRadius_ = GetLevelUpgradeSetting(
		"explosive",
		level,
		"radius",
		explosiveBulletRadius_);
	explosiveBulletRadius_ += GetLevelUpgradeSetting(
		"explosive",
		level,
		"radiusAdd",
		0.0f);
	explosiveBulletInterval_ = GetLevelUpgradeSetting(
		"explosive",
		level,
		"interval",
		explosiveBulletInterval_);
	explosiveBulletInterval_ *= GetLevelUpgradeSetting(
		"explosive",
		level,
		"intervalMultiplier",
		1.0f);
	explosiveBulletSpeed_ *= GetLevelUpgradeSetting(
		"explosive",
		level,
		"speedMultiplier",
		1.0f);
	explosiveBulletRange_ += GetLevelUpgradeSetting(
		"explosive",
		level,
		"rangeAdd",
		0.0f);
	explosiveBulletInterval_ = (std::max)(
		kMinWeaponInterval,
		PositiveFiniteOr(explosiveBulletInterval_, kMinWeaponInterval));
}

int32_t PlayerWeaponController::GetLevelUpgradeSettingInt(
	const std::string& prefix,
	int32_t level,
	const std::string& suffix,
	int32_t fallback) const
{
	return static_cast<int32_t>(GetLevelUpgradeSetting(
		prefix,
		level,
		suffix,
		static_cast<float>(fallback)));
}

float PlayerWeaponController::GetLevelUpgradeSetting(
	const std::string& prefix,
	int32_t level,
	const std::string& suffix,
	float fallback) const
{
	return GetUpgradeSetting(
		prefix + ".lv" + std::to_string(level) + "." + suffix,
		fallback);
}

float PlayerWeaponController::GetUpgradeSetting(
	const std::string& key,
	float fallback) const
{
	const auto it = upgradeSettings_.find(key);
	return it == upgradeSettings_.end() ? fallback : it->second;
}

}
