#include "game/directxgame/player/PlayerWeaponController.h"
#include "game/directxgame/player/WeaponUpgradeData.h"

#include "game/directxgame/core/CsvReader.h"
#include "game/directxgame/core/GameplayRules.h"
#include "game/directxgame/enemy/EnemyManager.h"
#include "game/directxgame/player/Player.h"
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
	} else if (key == "normalBulletUpgradeMultiplier") {
		normalBulletUpgradeMultiplier_ = CsvReader::ParseFloat(
			value,
			"playerStatus.normalBulletUpgradeMultiplier");
	} else if (key == "normalBulletMinInterval") {
		normalBulletMinInterval_ = CsvReader::ParseFloat(
			value,
			"playerStatus.normalBulletMinInterval");
	} else if (key == "droneInterval") {
		droneInterval_ = CsvReader::ParseFloat(
			value,
			"playerStatus.droneInterval");
	} else if (key == "droneUpgradeMultiplier") {
		droneUpgradeMultiplier_ = CsvReader::ParseFloat(
			value,
			"playerStatus.droneUpgradeMultiplier");
	} else {
		return false;
	}
	if (normalBulletInterval_ <= 0.0f ||
		normalBulletUpgradeMultiplier_ <= 0.0f ||
		normalBulletMinInterval_ <= 0.0f ||
		droneInterval_ <= 0.0f ||
		droneUpgradeMultiplier_ <= 0.0f) {
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
}

void PlayerWeaponController::UpgradeNormalBullets(Player*)
{
	if (IsNormalBulletMaxLevel()) {
		return;
	}
	++normalBulletLevel_;
	switch (normalBulletLevel_) {
	case 2:
		normalBulletAmount_ = static_cast<int32_t>(
			GetUpgradeSetting("normal.lv2.amount", 2.0f));
		break;
	case 3:
		normalBulletSpeed_ *= GetUpgradeSetting(
			"normal.lv3.speedMultiplier",
			1.2f);
		normalBulletInterval_ *= GetUpgradeSetting(
			"normal.lv3.intervalMultiplier",
			normalBulletUpgradeMultiplier_);
		break;
	case 4:
		normalBulletAmount_ = static_cast<int32_t>(
			GetUpgradeSetting("normal.lv4.amount", 3.0f));
		break;
	case 5:
		normalBulletDamageBonus_ += static_cast<int32_t>(
			GetUpgradeSetting(
				"normal.lv5.damageBonusAdd",
				1.0f));
		break;
	case 6:
		normalBulletAmount_ = static_cast<int32_t>(
			GetUpgradeSetting("normal.lv6.amount", 4.0f));
		break;
	case 7:
		normalBulletPierceCount_ = static_cast<int32_t>(
			GetUpgradeSetting(
				"normal.lv7.pierceCount",
				2.0f));
		break;
	case 8:
		normalBulletDamageBonus_ += static_cast<int32_t>(
			GetUpgradeSetting(
				"normal.lv8.damageBonusAdd",
				1.0f));
		normalBulletSpeed_ *= GetUpgradeSetting(
			"normal.lv8.speedMultiplier",
			1.15f);
		normalBulletInterval_ *= GetUpgradeSetting(
			"normal.lv8.intervalMultiplier",
			normalBulletUpgradeMultiplier_);
		break;
	default:
		break;
	}
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
	orbitBulletCount_ = static_cast<int32_t>(
		GetUpgradeSetting("orbit.lv1.count", 1.0f));
	orbitBulletScale_ =
		GetUpgradeSetting("orbit.lv1.scale", 1.0f);
	orbitHitInterval_ =
		GetUpgradeSetting("orbit.lv1.hitInterval", 0.5f);
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
	switch (orbitBulletLevel_) {
	case 2:
		orbitBulletCount_ = static_cast<int32_t>(
			GetUpgradeSetting("orbit.lv2.count", 2.0f));
		break;
	case 3:
		orbitRadius_ += GetUpgradeSetting(
			"orbit.lv3.radiusAdd",
			orbitRadiusUpgradeStep_);
		orbitAngularSpeed_ += GetUpgradeSetting(
			"orbit.lv3.angularSpeedAdd",
			orbitAngularSpeedUpgradeStep_);
		orbitBulletScale_ += GetUpgradeSetting(
			"orbit.lv3.scaleAdd",
			orbitBulletScaleUpgradeStep_);
		break;
	case 4:
		orbitHitInterval_ *= GetUpgradeSetting(
			"orbit.lv4.hitIntervalMultiplier",
			orbitHitIntervalUpgradeMultiplier_);
		break;
	case 5:
		orbitBulletCount_ = static_cast<int32_t>(
			GetUpgradeSetting("orbit.lv5.count", 3.0f));
		break;
	case 6:
		orbitRadius_ += GetUpgradeSetting(
			"orbit.lv6.radiusAdd",
			orbitRadiusUpgradeStep_);
		orbitAngularSpeed_ += GetUpgradeSetting(
			"orbit.lv6.angularSpeedAdd",
			orbitAngularSpeedUpgradeStep_);
		orbitBulletScale_ += GetUpgradeSetting(
			"orbit.lv6.scaleAdd",
			orbitBulletScaleUpgradeStep_);
		break;
	case 7:
		orbitHitInterval_ *= GetUpgradeSetting(
			"orbit.lv7.hitIntervalMultiplier",
			orbitHitIntervalUpgradeMultiplier_);
		break;
	case 8:
		orbitBulletCount_ = static_cast<int32_t>(
			GetUpgradeSetting("orbit.lv8.count", 4.0f));
		break;
	default:
		break;
	}
	RebuildOrbitBullets(player);
}

void PlayerWeaponController::AddDrone()
{
	hasDrone_ = true;
	droneLevel_ = 1;
	droneShotCount_ = static_cast<int32_t>(
		GetUpgradeSetting("drone.lv1.shotCount", 1.0f));
	droneDamageBonus_ = static_cast<int32_t>(
		GetUpgradeSetting("drone.lv1.damageBonus", 0.0f));
	dronePierceCount_ = static_cast<int32_t>(
		GetUpgradeSetting("drone.lv1.pierceCount", 1.0f));
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
	switch (droneLevel_) {
	case 2:
		droneShotCount_ = static_cast<int32_t>(
			GetUpgradeSetting("drone.lv2.shotCount", 2.0f));
		break;
	case 3:
		droneInterval_ *= GetUpgradeSetting(
			"drone.lv3.intervalMultiplier",
			droneUpgradeMultiplier_);
		break;
	case 4:
		droneShotCount_ = static_cast<int32_t>(
			GetUpgradeSetting("drone.lv4.shotCount", 3.0f));
		break;
	case 5:
		droneDamageBonus_ += static_cast<int32_t>(
			GetUpgradeSetting(
				"drone.lv5.damageBonusAdd",
				1.0f));
		break;
	case 6:
		droneShotCount_ = static_cast<int32_t>(
			GetUpgradeSetting("drone.lv6.shotCount", 4.0f));
		break;
	case 7:
		dronePierceCount_ = static_cast<int32_t>(
			GetUpgradeSetting("drone.lv7.pierceCount", 2.0f));
		break;
	case 8:
		droneDamageBonus_ += static_cast<int32_t>(
			GetUpgradeSetting(
				"drone.lv8.damageBonusAdd",
				1.0f));
		droneInterval_ *= GetUpgradeSetting(
			"drone.lv8.intervalMultiplier",
			droneUpgradeMultiplier_);
		break;
	default:
		break;
	}
	droneInterval_ = (std::max)(
		kMinWeaponInterval,
		PositiveFiniteOr(droneInterval_, kMinWeaponInterval));
}

void PlayerWeaponController::AddLightning()
{
	hasLightning_ = true;
	lightningLevel_ = 1;
	lightningStrikeCount_ = static_cast<int32_t>(
		GetUpgradeSetting(
			"lightning.lv1.strikeCount",
			1.0f));
	lightningDamageBonus_ = static_cast<int32_t>(
		GetUpgradeSetting(
			"lightning.lv1.damageBonus",
			0.0f));
	lightningRadius_ =
		GetUpgradeSetting("lightning.lv1.radius", 6.0f);
	lightningInterval_ = (std::max)(
		kMinWeaponInterval,
		PositiveFiniteOr(
			GetUpgradeSetting("lightning.lv1.interval", 2.4f),
			2.4f));
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
	switch (lightningLevel_) {
	case 2:
		lightningDamageBonus_ += static_cast<int32_t>(
			GetUpgradeSetting(
				"lightning.lv2.damageBonusAdd",
				1.0f));
		break;
	case 3:
		lightningStrikeCount_ = static_cast<int32_t>(
			GetUpgradeSetting(
				"lightning.lv3.strikeCount",
				2.0f));
		break;
	case 4:
		lightningRadius_ += GetUpgradeSetting(
			"lightning.lv4.radiusAdd",
			1.5f);
		break;
	case 5:
		lightningDamageBonus_ += static_cast<int32_t>(
			GetUpgradeSetting(
				"lightning.lv5.damageBonusAdd",
				1.0f));
		break;
	case 6:
		lightningStrikeCount_ = static_cast<int32_t>(
			GetUpgradeSetting(
				"lightning.lv6.strikeCount",
				3.0f));
		break;
	case 7:
		lightningInterval_ *= GetUpgradeSetting(
			"lightning.lv7.intervalMultiplier",
			0.82f);
		break;
	case 8:
		lightningStrikeCount_ = static_cast<int32_t>(
			GetUpgradeSetting(
				"lightning.lv8.strikeCount",
				4.0f));
		lightningRadius_ += GetUpgradeSetting(
			"lightning.lv8.radiusAdd",
			1.5f);
		break;
	default:
		break;
	}
	lightningInterval_ = (std::max)(
		kMinWeaponInterval,
		PositiveFiniteOr(lightningInterval_, kMinWeaponInterval));
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

float PlayerWeaponController::GetUpgradeSetting(
	const std::string& key,
	float fallback) const
{
	const auto it = upgradeSettings_.find(key);
	return it == upgradeSettings_.end() ? fallback : it->second;
}

}
