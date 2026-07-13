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
const DirectXGame::NormalBullet::VisualStyle kBowArrowVisual{
	"bullet.obj",
	{ 0.66f, 0.94f, 1.0f, 1.0f },
	{ 0.52f, 0.52f, 1.45f },
};
const DirectXGame::NormalBullet::VisualStyle kFlameStaffVisual{
	"fireball.obj",
	{ 1.0f, 0.28f, 0.03f, 1.0f },
	{ 1.26f, 1.26f, 1.26f },
};

float PositiveFiniteOr(float value, float fallback)
{
	return std::isfinite(value) && value > 0.0f
		? value
		: fallback;
}

Vector3 PlayerFacingDirection(const DirectXGame::Player& player)
{
	const float angle = player.GetWorldRotationY();
	return { std::sin(angle), 0.0f, std::cos(angle) };
}

}

namespace DirectXGame {

void PlayerWeaponController::Initialize(
	const std::string& upgradeSettingsPath)
{
	LoadUpgradeSettings(upgradeSettingsPath);
	explosiveBurstInterval_ = GetUpgradeSetting(
		"flameStaff.burstInterval",
		explosiveBurstInterval_);
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
	} else if (key == "explosiveBulletInterval") {
		explosiveBulletInterval_ = CsvReader::ParseFloat(
			value,
			"playerStatus.explosiveBulletInterval");
	} else {
		return false;
	}
	if (normalBulletInterval_ <= 0.0f ||
		normalBulletMinInterval_ <= 0.0f ||
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
		// damageBonus は 0 を「増加なし」として許可し、それ以外の間隔/半径/個数系は正値に限定する。
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
	const PlayerStats& playerStats)
{
	// 武器更新は固定順にする。弾生成、範囲ダメージ、演出イベントの発生順がフレーム間でぶれないようにする。
	UpdateNormalBullets(deltaTime, player, enemyManager, playerStats);
	UpdateOrbitBullets(deltaTime, player, playerStats);
	UpdateLightning(deltaTime, enemyManager, playerStats);
	UpdateExplosiveBullets(deltaTime, player, enemyManager, playerStats);
	UpdateSword(deltaTime, player, enemyManager, playerStats);
	UpdateAura(deltaTime, player, enemyManager, playerStats);
	UpdateFlameShoes(deltaTime, player, enemyManager, playerStats);
	UpdateBone(deltaTime, player, enemyManager, playerStats);
	UpdateHandgun(deltaTime, player, enemyManager, playerStats);
	UpdateBoomerang(deltaTime, player, enemyManager, playerStats);
}

void PlayerWeaponController::Draw()
{
	// Draw は所有中の弾・投射物だけを描画する。範囲攻撃系の見た目は Presentation 側がイベントから描く。
	for (std::unique_ptr<NormalBullet>& bullet : normalBullets_) {
		bullet->Draw();
	}
	for (std::unique_ptr<OrbitBullet>& bullet : orbitBullets_) {
		bullet->Draw();
	}
	for (std::unique_ptr<NormalBullet>& bullet : explosiveBullets_) {
		bullet->Draw();
	}
	boneWeapon_.Draw();
	handgunWeapon_.Draw();
	boomerangWeapon_.Draw();
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
	if (hasOrbitBullets_ || !CanAcquireWeapon(WeaponType::Rock)) {
		return;
	}
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

void PlayerWeaponController::AddLightning()
{
	if (hasLightning_ || !CanAcquireWeapon(WeaponType::ThunderStaff)) {
		return;
	}
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
	if (hasExplosiveBullets_ ||
		!CanAcquireWeapon(WeaponType::FlameStaff)) {
		return;
	}
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

void PlayerWeaponController::UpgradeSword()
{
	if (!hasSword_) {
		if (!CanAcquireWeapon(WeaponType::Sword)) {
			return;
		}
		hasSword_ = true;
		swordLevel_ = 1;
	} else if (IsSwordMaxLevel()) {
		return;
	} else {
		++swordLevel_;
	}
	ApplySwordUpgradeLevel(swordLevel_);
}

void PlayerWeaponController::UpgradeAura()
{
	if (!hasAura_) {
		if (!CanAcquireWeapon(WeaponType::Aura)) {
			return;
		}
		hasAura_ = true;
		auraLevel_ = 1;
	} else if (IsAuraMaxLevel()) {
		return;
	} else {
		++auraLevel_;
	}
	ApplyAuraUpgradeLevel(auraLevel_);
}

void PlayerWeaponController::UpgradeFlameShoes()
{
	if (!hasFlameShoes_) {
		if (!CanAcquireWeapon(WeaponType::FlameShoes)) {
			return;
		}
		hasFlameShoes_ = true;
		flameShoesLevel_ = 1;
		flameShoesPositionInitialized_ = false;
		flameShoesLastDirection_ = { 0.0f, 0.0f, 1.0f };
	} else if (IsFlameShoesMaxLevel()) {
		return;
	} else {
		++flameShoesLevel_;
	}
	ApplyFlameShoesUpgradeLevel(flameShoesLevel_);
}

void PlayerWeaponController::UpgradeBone()
{
	if (!CanAcquireWeapon(WeaponType::Bone) || IsBoneMaxLevel()) return;
	boneWeapon_.Upgrade(upgradeSettings_);
}

void PlayerWeaponController::UpgradeHandgun()
{
	if (!CanAcquireWeapon(WeaponType::Handgun) || IsHandgunMaxLevel()) return;
	handgunWeapon_.Upgrade(upgradeSettings_);
}

void PlayerWeaponController::UpgradeBoomerang()
{
	if (!CanAcquireWeapon(WeaponType::Boomerang) || IsBoomerangMaxLevel()) return;
	boomerangWeapon_.Upgrade(upgradeSettings_);
}

void PlayerWeaponController::UpgradeWeapon(WeaponType type, Player* player)
{
	switch (type) {
	case WeaponType::BowArrow:
		UpgradeNormalBullets(player);
		break;
	case WeaponType::Rock:
		UpgradeOrbitBullets(player);
		break;
	case WeaponType::ThunderStaff:
		UpgradeLightning();
		break;
	case WeaponType::FlameStaff:
		UpgradeExplosiveBullets();
		break;
	case WeaponType::Sword:
		UpgradeSword();
		break;
	case WeaponType::Aura:
		UpgradeAura();
		break;
	case WeaponType::FlameShoes:
		UpgradeFlameShoes();
		break;
	case WeaponType::Bone: UpgradeBone(); break;
	case WeaponType::Handgun: UpgradeHandgun(); break;
	case WeaponType::Boomerang: UpgradeBoomerang(); break;
	}
}

bool PlayerWeaponController::IsWeaponMaxLevel(WeaponType type) const
{
	switch (type) {
	case WeaponType::BowArrow:
		return IsNormalBulletMaxLevel();
	case WeaponType::Rock:
		return IsOrbitBulletMaxLevel();
	case WeaponType::ThunderStaff:
		return IsLightningMaxLevel();
	case WeaponType::FlameStaff:
		return IsExplosiveBulletMaxLevel();
	case WeaponType::Sword:
		return IsSwordMaxLevel();
	case WeaponType::Aura:
		return IsAuraMaxLevel();
	case WeaponType::FlameShoes:
		return IsFlameShoesMaxLevel();
	case WeaponType::Bone: return IsBoneMaxLevel();
	case WeaponType::Handgun: return IsHandgunMaxLevel();
	case WeaponType::Boomerang: return IsBoomerangMaxLevel();
	}
	return true;
}

bool PlayerWeaponController::HasWeapon(WeaponType type) const
{
	switch (type) {
	case WeaponType::BowArrow:
		return true;
	case WeaponType::Rock:
		return hasOrbitBullets_;
	case WeaponType::ThunderStaff:
		return hasLightning_;
	case WeaponType::FlameStaff:
		return hasExplosiveBullets_;
	case WeaponType::Sword:
		return hasSword_;
	case WeaponType::Aura:
		return hasAura_;
	case WeaponType::FlameShoes:
		return hasFlameShoes_;
	case WeaponType::Bone: return HasBone();
	case WeaponType::Handgun: return HasHandgun();
	case WeaponType::Boomerang: return HasBoomerang();
	}
	return false;
}

size_t PlayerWeaponController::GetEquippedWeaponCount() const
{
	return static_cast<size_t>(HasWeapon(WeaponType::BowArrow)) +
		static_cast<size_t>(hasOrbitBullets_) +
		static_cast<size_t>(hasLightning_) +
		static_cast<size_t>(hasExplosiveBullets_) +
		static_cast<size_t>(hasSword_) +
		static_cast<size_t>(hasAura_) +
		static_cast<size_t>(hasFlameShoes_) +
		static_cast<size_t>(HasBone()) +
		static_cast<size_t>(HasHandgun()) +
		static_cast<size_t>(HasBoomerang());
}

bool PlayerWeaponController::CanAcquireWeapon(WeaponType type) const
{
	return HasWeapon(type) ||
		GetEquippedWeaponCount() < kMaxEquippedWeaponTypes;
}

void PlayerWeaponController::MaxAllWeapons(Player* player)
{
	for (WeaponType type : kUpgradeableWeaponTypes) {
		if (!CanAcquireWeapon(type)) {
			continue;
		}
		while (!IsWeaponMaxLevel(type)) {
			UpgradeWeapon(type, player);
		}
	}
}

void PlayerWeaponController::ResetBulletTelemetry()
{
	peakNormalBulletCount_ = normalBullets_.size();
	normalBulletPruneCount_ = 0;
}

void PlayerWeaponController::UpdateNormalBullets(
	float deltaTime,
	Player* player,
	EnemyManager* enemyManager,
	const PlayerStats& stats)
{
	const WeaponRuntimeStats runtime = stats.Resolve({
		10.0f + normalBulletDamageBonus_,
		normalBulletInterval_,
		normalBulletSpeed_,
		normalBulletRange_,
		normalBulletScale_,
		normalBulletAmount_,
		}, GetWeaponStatApplicability(WeaponType::BowArrow));
	if (hasNormalBullets_ && player) {
		normalBulletTimer_ += deltaTime;
		int32_t catchUpAttackCount = 0;
		while (normalBulletTimer_ >= runtime.interval &&
			catchUpAttackCount < kMaxCatchUpAttacksPerFrame) {
			const Vector3 forward = ResolveAimDirection(
				*player, enemyManager);
			const Vector3 right{
				forward.z,
				0.0f,
				-forward.x,
			};
			const float centerOffset =
				static_cast<float>(runtime.projectileCount - 1) *
				0.5f;
			for (int32_t index = 0;
				index < runtime.projectileCount;
				++index) {
				const float spreadAngle =
					(static_cast<float>(index) - centerOffset) * 0.12f;
				const float cosine = std::cos(spreadAngle);
				const float sine = std::sin(spreadAngle);
				const Vector3 shotDirection{
					forward.x * cosine + right.x * sine,
					0.0f,
					forward.z * cosine + right.z * sine,
				};
				NormalBullet& bullet = AcquireNormalBullet();
				bullet.InitializeForward(
					player->GetWorldPosition(),
					shotDirection,
					runtime.projectileSpeed,
					runtime.duration,
					normalBulletPierceCount_,
					runtime.areaSize,
					NormalBullet::MovementMode::Straight,
					kBowArrowVisual);
				peakNormalBulletCount_ = (std::max)(
					peakNormalBulletCount_,
					normalBullets_.size());
				if (normalBullets_.size() >
					kMaxActiveNormalBullets) {
					RecycleNormalBullet(0);
					++normalBulletPruneCount_;
				}
			}
			normalBulletTimer_ -= runtime.interval;
			++catchUpAttackCount;
		}
		if (normalBulletTimer_ >= runtime.interval) {
			normalBulletTimer_ = std::fmod(
				normalBulletTimer_,
				runtime.interval);
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
	Player* player,
	EnemyManager* enemyManager,
	const PlayerStats& stats)
{
	const WeaponRuntimeStats runtime = stats.Resolve({
		10.0f + explosiveBulletDamageBonus_,
		explosiveBulletInterval_,
		explosiveBulletSpeed_,
		explosiveBulletRange_,
		1.0f,
		explosiveBulletCount_,
		}, GetWeaponStatApplicability(WeaponType::FlameStaff));
	if (hasExplosiveBullets_ && player) {
		explosiveBulletTimer_ += deltaTime;
		explosiveBurstTimer_ += deltaTime;
		int32_t catchUpAttackCount = 0;
		while (explosiveBulletTimer_ >= runtime.interval &&
			catchUpAttackCount < kMaxCatchUpAttacksPerFrame) {
			if (explosiveBurstShotsRemaining_ <= 0) {
				explosiveBurstShotsRemaining_ = runtime.projectileCount;
				explosiveBurstTimer_ = explosiveBurstInterval_;
			}
			explosiveBulletTimer_ -= runtime.interval;
			++catchUpAttackCount;
		}
		if (explosiveBulletTimer_ >= runtime.interval) {
			explosiveBulletTimer_ = std::fmod(
				explosiveBulletTimer_,
				runtime.interval);
		}

		const float effectiveBurstInterval =
			explosiveBurstInterval_ / stats.GetAttackSpeedMultiplier();
		if (explosiveBurstShotsRemaining_ > 0 &&
			explosiveBurstTimer_ >= effectiveBurstInterval) {
			const Vector3 direction = ResolveAimDirection(
				*player, enemyManager);
			NormalBullet& bullet = AcquireExplosiveBullet();
			bullet.InitializeForward(
				player->GetWorldPosition(),
				direction,
				runtime.projectileSpeed,
				runtime.duration,
				1,
				runtime.areaSize,
				NormalBullet::MovementMode::Straight,
				kFlameStaffVisual);
			--explosiveBurstShotsRemaining_;
			explosiveBurstTimer_ -= effectiveBurstInterval;
		}
	}

	for (std::unique_ptr<NormalBullet>& bullet : explosiveBullets_) {
		bullet->Update(
			player ? player->GetWorldPosition() : Vector3{},
			deltaTime);
	}
	RecycleInactiveExplosiveBullets();
}

void PlayerWeaponController::UpdateSword(
	float deltaTime,
	Player* player,
	EnemyManager* enemyManager,
	const PlayerStats& stats)
{
	recentSwordSlashes_.clear();
	if (!hasSword_ || !player || !enemyManager) {
		return;
	}
	swordTimer_ += deltaTime;
	const WeaponRuntimeStats runtime = stats.Resolve({
		14.0f + swordDamageBonus_,
		swordInterval_,
		1.0f,
		1.0f,
		swordRadius_,
		swordSlashCount_,
		}, GetWeaponStatApplicability(WeaponType::Sword));
	const float effectiveInterval =
		runtime.interval;
	if (swordTimer_ < effectiveInterval) {
		return;
	}
	const Vector3 direction = PlayerFacingDirection(*player);
	const Vector3 right{
		direction.z,
		0.0f,
		-direction.x,
	};
	const int32_t slashCount = runtime.projectileCount;
	const float centerOffset = static_cast<float>(slashCount - 1) * 0.5f;
	const float radius = runtime.areaSize;
	const int32_t damage = runtime.damage;
	for (int32_t index = 0; index < slashCount; ++index) {
		const int32_t directionSign = index % 2 == 0 ? 1 : -1;
		const int32_t sideStep = index / 2;
		const float spreadAngle =
			static_cast<float>(directionSign * sideStep) *
			(swordHalfAngle_ * 0.38f);
		const float cosine = std::cos(spreadAngle);
		const float sine = std::sin(spreadAngle);
		const Vector3 slashDirection{
			direction.x * cosine + right.x * sine,
			0.0f,
			direction.z * cosine + right.z * sine,
		};
		enemyManager->ApplyArcDamage(
			player->GetWorldPosition(),
			slashDirection,
			radius,
			swordHalfAngle_ * 0.55f,
			damage,
			swordKnockbackStrength_);
		recentSwordSlashes_.push_back({
			player->GetWorldPosition(),
			slashDirection,
			radius,
			swordHalfAngle_ * 0.55f,
			directionSign,
			});
	}
	swordTimer_ = std::fmod(swordTimer_, effectiveInterval);
}

void PlayerWeaponController::UpdateAura(
	float deltaTime,
	Player* player,
	EnemyManager* enemyManager,
	const PlayerStats& stats)
{
	auraPulseThisFrame_ = false;
	if (!hasAura_ || !player || !enemyManager) {
		return;
	}
	auraTimer_ += deltaTime;
	const WeaponRuntimeStats runtime = stats.Resolve({
		8.0f + auraDamageBonus_,
		auraInterval_,
		1.0f,
		1.0f,
		auraRadius_,
		1,
		}, GetWeaponStatApplicability(WeaponType::Aura));
	const float interval = runtime.interval;
	if (auraTimer_ < interval) {
		return;
	}
	enemyManager->ApplyAreaDamage(
		player->GetWorldPosition(),
		runtime.areaSize,
		runtime.damage);
	auraPulseThisFrame_ = true;
	auraTimer_ = std::fmod(auraTimer_, interval);
}

void PlayerWeaponController::UpdateFlameShoes(
	float deltaTime,
	Player* player,
	EnemyManager* enemyManager,
	const PlayerStats& stats)
{
	recentFlameZoneSpawns_.clear();
	flameZoneVisuals_.clear();
	if (!hasFlameShoes_ || !player || !enemyManager) {
		return;
	}
	const Vector3 playerPosition = player->GetWorldPosition();
	const WeaponRuntimeStats runtime = stats.Resolve({
		5.0f + flameShoesDamageBonus_,
		flameShoesDamageInterval_,
		1.0f,
		flameShoesZoneDuration_,
		flameShoesRadius_,
		flameShoesZoneCount_,
		}, GetWeaponStatApplicability(WeaponType::FlameShoes));
	if (!flameShoesPositionInitialized_) {
		flameShoesLastSpawnPosition_ = playerPosition;
		flameShoesPositionInitialized_ = true;
	}
	const float spawnDx = playerPosition.x - flameShoesLastSpawnPosition_.x;
	const float spawnDz = playerPosition.z - flameShoesLastSpawnPosition_.z;
	const float spawnDistanceSq = spawnDx * spawnDx + spawnDz * spawnDz;
	if (spawnDistanceSq >=
		flameShoesSpawnDistance_ * flameShoesSpawnDistance_) {
		Vector3 moveDirection{ spawnDx, 0.0f, spawnDz };
		const float moveLength = std::sqrt(spawnDistanceSq);
		if (moveLength > 0.0001f) {
			moveDirection.x /= moveLength;
			moveDirection.z /= moveLength;
			flameShoesLastDirection_ = moveDirection;
		} else {
			moveDirection = flameShoesLastDirection_;
		}
		const int32_t zoneCount = (std::max)(
			1,
			runtime.projectileCount);
		const float zoneDuration = runtime.duration;
		const float zoneSpacing =
			runtime.areaSize * 1.08f;
		const Vector3 trailCenter =
			playerPosition - moveDirection *
				(runtime.areaSize * 0.06f);
		for (int32_t index = 0; index < zoneCount; ++index) {
			if (flameZones_.size() >= kMaxFlameZones) {
				flameZones_.erase(flameZones_.begin());
			}
			const float backOffset =
				static_cast<float>(index) * zoneSpacing;
			const Vector3 zonePosition =
				trailCenter - moveDirection * backOffset;
			flameZones_.push_back({
				zonePosition,
				moveDirection,
				runtime.areaSize,
				zoneDuration,
				zoneDuration,
				runtime.interval,
				});
			recentFlameZoneSpawns_.push_back(zonePosition);
		}
		flameShoesLastSpawnPosition_ = playerPosition;
	}

	const float damageInterval = runtime.interval;
	for (FlameZone& zone : flameZones_) {
		zone.remainingDuration -= deltaTime;
		zone.damageTimer += deltaTime;
		const float lifeRatio = std::clamp(
			zone.remainingDuration / (std::max)(0.001f, zone.totalDuration),
			0.0f,
			1.0f);
		const float activeRadius = zone.radius *
			(0.55f + std::sqrt(lifeRatio) * 0.45f);
		flameZoneVisuals_.push_back({
			zone.position,
			zone.direction,
			activeRadius,
			zone.remainingDuration,
			zone.totalDuration,
			});
		if (zone.damageTimer < damageInterval) {
			continue;
		}
		enemyManager->ApplyAreaDamage(
			zone.position,
			activeRadius,
			runtime.damage);
		zone.damageTimer = std::fmod(zone.damageTimer, damageInterval);
	}
	std::erase_if(flameZones_, [](const FlameZone& zone) {
		return zone.remainingDuration <= 0.0f;
	});
}

void PlayerWeaponController::UpdateBone(
	float deltaTime, Player* player, EnemyManager* enemyManager,
	const PlayerStats& stats)
{
	if (!player) return;
	boneWeapon_.Update(deltaTime, player->GetWorldPosition(),
		ResolveAimDirection(*player, enemyManager), stats);
}

void PlayerWeaponController::UpdateHandgun(
	float deltaTime, Player* player, EnemyManager* enemyManager,
	const PlayerStats& stats)
{
	if (!player) return;
	handgunWeapon_.Update(deltaTime, player->GetWorldPosition(),
		ResolveAimDirection(*player, enemyManager), stats);
}

void PlayerWeaponController::UpdateBoomerang(
	float deltaTime, Player* player, EnemyManager* enemyManager,
	const PlayerStats& stats)
{
	if (!player) return;
	boomerangWeapon_.Update(deltaTime, player->GetWorldPosition(),
		ResolveAimDirection(*player, enemyManager), stats);
}

Vector3 PlayerWeaponController::ResolveAimDirection(
	const Player& player,
	const EnemyManager* enemyManager) const
{
	const Vector3 origin = player.GetWorldPosition();
	Vector3 target{};
	if (enemyManager &&
		enemyManager->FindNearestEnemyPosition(origin, 1000.0f, target)) {
		const float deltaX = target.x - origin.x;
		const float deltaZ = target.z - origin.z;
		const float length = std::sqrt(deltaX * deltaX + deltaZ * deltaZ);
		if (length > 0.0001f) {
			return { deltaX / length, 0.0f, deltaZ / length };
		}
	}
	const float angle = player.GetWorldRotationY();
	return { std::sin(angle), 0.0f, std::cos(angle) };
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
	Player* player,
	const PlayerStats& stats)
{
	if (!hasOrbitBullets_ || !player) {
		return;
	}
	const WeaponStatApplicability applicability =
		GetWeaponStatApplicability(WeaponType::Rock);
	const int32_t projectileCountBonus =
		applicability.projectileCount ? stats.GetProjectileCountBonus() : 0;
	const int32_t effectiveCount = (std::max)(
		1, orbitBulletCount_ + projectileCountBonus);
	if (orbitBullets_.size() != static_cast<size_t>(effectiveCount)) {
		RebuildOrbitBullets(player, projectileCountBonus);
	}
	for (std::unique_ptr<OrbitBullet>& bullet : orbitBullets_) {
		bullet->ApplyRuntimeModifiers(
			applicability.projectileSpeed
				? stats.GetProjectileSpeedMultiplier()
				: 1.0f,
			applicability.areaSize
				? stats.GetAreaSizeMultiplier()
				: 1.0f,
			applicability.attackSpeed
				? stats.GetAttackSpeedMultiplier()
				: 1.0f);
		bullet->Update(player->GetWorldPosition(), deltaTime);
	}
}

void PlayerWeaponController::UpdateLightning(
	float deltaTime,
	EnemyManager* enemyManager,
	const PlayerStats& stats)
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
	const WeaponStatApplicability applicability =
		GetWeaponStatApplicability(WeaponType::ThunderStaff);
	const float attackSpeedMultiplier =
		applicability.attackSpeed ? stats.GetAttackSpeedMultiplier() : 1.0f;
	const float areaSizeMultiplier =
		applicability.areaSize ? stats.GetAreaSizeMultiplier() : 1.0f;
	const float durationMultiplier =
		applicability.duration ? stats.GetDurationMultiplier() : 1.0f;
	const int32_t projectileCountBonus =
		applicability.projectileCount ? stats.GetProjectileCountBonus() : 0;
	const float effectiveInterval =
		lightningInterval_ / attackSpeedMultiplier;
	while (lightningTimer_ >= effectiveInterval &&
		catchUpAttackCount < kMaxCatchUpAttacksPerFrame) {
		const std::vector<Vector3> targets =
			enemyManager->PickLightningTargets(
				lightningStrikeCount_ + projectileCountBonus);
		if (!targets.empty()) {
			lightningEffectTargets_ = targets;
			lightningEffectTimer_ =
				0.22f * durationMultiplier;
		}
		for (const Vector3& target : targets) {
			enemyManager->ApplyLightningDamage(
				target,
				lightningRadius_ * areaSizeMultiplier,
				GetLightningDamage(stats));
		}
		lightningTimer_ -= effectiveInterval;
		++catchUpAttackCount;
		if (targets.empty()) {
			break;
		}
	}
	if (lightningTimer_ >= effectiveInterval) {
		lightningTimer_ = std::fmod(
			lightningTimer_,
			effectiveInterval);
	}
}

void PlayerWeaponController::RebuildOrbitBullets(
	Player* player,
	int32_t projectileCountBonus)
{
	if (!player) {
		orbitBullets_.clear();
		return;
	}
	orbitBullets_.clear();
	const int32_t effectiveCount = (std::max)(
		1, orbitBulletCount_ + projectileCountBonus);
	orbitBullets_.reserve(effectiveCount);
	for (int32_t index = 0; index < effectiveCount; ++index) {
		const float angle =
			(2.0f * std::numbers::pi_v<float> *
				static_cast<float>(index)) /
			static_cast<float>(effectiveCount);
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
		"bowArrow",
		level,
		"amount",
		normalBulletAmount_);
	normalBulletDamageBonus_ += GetLevelUpgradeSettingInt(
		"bowArrow",
		level,
		"damageBonusAdd",
		0);
	normalBulletPierceCount_ = GetLevelUpgradeSettingInt(
		"bowArrow",
		level,
		"pierceCount",
		normalBulletPierceCount_);
	normalBulletSpeed_ *= GetLevelUpgradeSetting(
		"bowArrow",
		level,
		"speedMultiplier",
		1.0f);
	normalBulletInterval_ *= GetLevelUpgradeSetting(
		"bowArrow",
		level,
		"intervalMultiplier",
		1.0f);
	normalBulletScale_ *= GetLevelUpgradeSetting(
		"bowArrow", level, "scaleMultiplier", 1.0f);
}

void PlayerWeaponController::ApplyOrbitUpgradeLevel(int32_t level)
{
	orbitBulletCount_ = GetLevelUpgradeSettingInt(
		"rock", level, "count", orbitBulletCount_);
	orbitDamageBonus_ += GetLevelUpgradeSettingInt(
		"rock", level, "damageBonusAdd", 0);
	orbitAngularSpeed_ *= GetLevelUpgradeSetting(
		"rock", level, "speedMultiplier", 1.0f);
	orbitBulletScale_ *= GetLevelUpgradeSetting(
		"rock", level, "scaleMultiplier", 1.0f);
	orbitHitInterval_ = GetLevelUpgradeSetting(
		"rock", level, "hitInterval", orbitHitInterval_);
	orbitHitInterval_ *= GetLevelUpgradeSetting(
		"rock", level, "hitIntervalMultiplier", 1.0f);
}

void PlayerWeaponController::ApplyLightningUpgradeLevel(int32_t level)
{
	lightningStrikeCount_ = GetLevelUpgradeSettingInt(
		"thunderStaff",
		level,
		"strikeCount",
		lightningStrikeCount_);
	lightningDamageBonus_ = GetLevelUpgradeSettingInt(
		"thunderStaff",
		level,
		"damageBonus",
		lightningDamageBonus_);
	lightningDamageBonus_ += GetLevelUpgradeSettingInt(
		"thunderStaff",
		level,
		"damageBonusAdd",
		0);
	lightningRadius_ = GetLevelUpgradeSetting(
		"thunderStaff",
		level,
		"radius",
		lightningRadius_);
	lightningRadius_ += GetLevelUpgradeSetting(
		"thunderStaff",
		level,
		"radiusAdd",
		0.0f);
	lightningInterval_ = GetLevelUpgradeSetting(
		"thunderStaff",
		level,
		"interval",
		lightningInterval_);
	lightningInterval_ *= GetLevelUpgradeSetting(
		"thunderStaff",
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
		"flameStaff",
		level,
		"damageBonus",
		explosiveBulletDamageBonus_);
	explosiveBulletDamageBonus_ += GetLevelUpgradeSettingInt(
		"flameStaff",
		level,
		"damageBonusAdd",
		0);
	explosiveBulletRadius_ = GetLevelUpgradeSetting(
		"flameStaff",
		level,
		"radius",
		explosiveBulletRadius_);
	explosiveBulletRadius_ += GetLevelUpgradeSetting(
		"flameStaff",
		level,
		"radiusAdd",
		0.0f);
	explosiveBulletInterval_ = GetLevelUpgradeSetting(
		"flameStaff",
		level,
		"interval",
		explosiveBulletInterval_);
	explosiveBulletInterval_ *= GetLevelUpgradeSetting(
		"flameStaff",
		level,
		"intervalMultiplier",
		1.0f);
	explosiveBulletSpeed_ *= GetLevelUpgradeSetting(
		"flameStaff",
		level,
		"speedMultiplier",
		1.0f);
	explosiveBulletRange_ += GetLevelUpgradeSetting(
		"flameStaff",
		level,
		"rangeAdd",
		0.0f);
	explosiveBulletInterval_ = (std::max)(
		kMinWeaponInterval,
		PositiveFiniteOr(explosiveBulletInterval_, kMinWeaponInterval));
	explosiveBulletCount_ = GetLevelUpgradeSettingInt(
		"flameStaff", level, "count", explosiveBulletCount_);
}

void PlayerWeaponController::ApplySwordUpgradeLevel(int32_t level)
{
	swordDamageBonus_ += GetLevelUpgradeSettingInt(
		"sword", level, "damageBonusAdd", 0);
	swordSlashCount_ = GetLevelUpgradeSettingInt(
		"sword", level, "count", swordSlashCount_);
	swordRadius_ += GetLevelUpgradeSetting(
		"sword", level, "radiusAdd", 0.0f);
	swordHalfAngle_ += GetLevelUpgradeSetting(
		"sword", level, "halfAngleAdd", 0.0f);
	swordKnockbackStrength_ += GetLevelUpgradeSetting(
		"sword", level, "knockbackAdd", 0.0f);
	swordInterval_ *= GetLevelUpgradeSetting(
		"sword", level, "intervalMultiplier", 1.0f);
	swordInterval_ = (std::max)(
		kMinWeaponInterval,
		PositiveFiniteOr(swordInterval_, kMinWeaponInterval));
}

void PlayerWeaponController::ApplyAuraUpgradeLevel(int32_t level)
{
	auraDamageBonus_ += GetLevelUpgradeSettingInt(
		"aura", level, "damageBonusAdd", 0);
	auraRadius_ += GetLevelUpgradeSetting(
		"aura", level, "radiusAdd", 0.0f);
	auraInterval_ *= GetLevelUpgradeSetting(
		"aura", level, "intervalMultiplier", 1.0f);
	auraInterval_ = (std::max)(
		kMinWeaponInterval,
		PositiveFiniteOr(auraInterval_, kMinWeaponInterval));
}

void PlayerWeaponController::ApplyFlameShoesUpgradeLevel(int32_t level)
{
	flameShoesDamageBonus_ += GetLevelUpgradeSettingInt(
		"flameShoes", level, "damageBonusAdd", 0);
	flameShoesZoneCount_ = GetLevelUpgradeSettingInt(
		"flameShoes", level, "count", flameShoesZoneCount_);
	flameShoesRadius_ += GetLevelUpgradeSetting(
		"flameShoes", level, "radiusAdd", 0.0f);
	flameShoesZoneDuration_ += GetLevelUpgradeSetting(
		"flameShoes", level, "durationAdd", 0.0f);
	flameShoesDamageInterval_ *= GetLevelUpgradeSetting(
		"flameShoes", level, "intervalMultiplier", 1.0f);
	flameShoesSpawnDistance_ *= GetLevelUpgradeSetting(
		"flameShoes", level, "spawnDistanceMultiplier", 1.0f);
	flameShoesDamageInterval_ = (std::max)(
		kMinWeaponInterval,
		PositiveFiniteOr(flameShoesDamageInterval_, kMinWeaponInterval));
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
