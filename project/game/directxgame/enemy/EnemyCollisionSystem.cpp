#include "EnemyCollisionSystem.h"
#include "GameplayRules.h"

#include "MyMath.h"
#include "GameAudioCache.h"
#include "Enemy.h"
#include "Player.h"
#include "PlayerManager.h"
#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <functional>
#include <unordered_map>

namespace {

constexpr float kFixedKnockbackStrength = 0.8f;

using EnemyCellMap =
	DirectXGame::EnemyCollisionContext::EnemyCellMap;

constexpr char kHitSePath[] = "audio/se/se_hit.wav";
constexpr char kPlayerDamageSePath[] = "audio/se/se_hit.wav";
constexpr char kAudioEnemyHit[] = "combat.enemyHit";
constexpr char kAudioPlayerDamage[] = "combat.playerDamage";
constexpr float kEnemySeparationStrength = 1.1f;
constexpr float kSpatialCellSize = 8.0f;
constexpr float kEnemyQueryPadding = 2.0f;

bool IsSweptBulletCollision(
	const DirectXGame::NormalBullet& bullet,
	const DirectXGame::Enemy& enemy)
{
	Engine::Math::AABB expanded = enemy.GetCollisionAabb();
	const float radius = bullet.GetCollisionRadius();
	expanded.min.x -= radius;
	expanded.min.y -= radius;
	expanded.min.z -= radius;
	expanded.max.x += radius;
	expanded.max.y += radius;
	expanded.max.z += radius;
	const Vector3& previous = bullet.GetPreviousPosition();
	const Vector3& current = bullet.GetPosition();
	const Engine::Math::Segment movement{
		previous,
		current - previous,
	};
	return Engine::Math::MyMath::IsCollision(expanded, movement);
}

std::array<Vector3, 4> GetObbCornersXZ(
	const Engine::Math::OBB& obb)
{
	const Vector3 center{ obb.center.x, 0.0f, obb.center.z };
	const Vector3 axisX{
		obb.orientations[0].x * obb.size.x,
		0.0f,
		obb.orientations[0].z * obb.size.x,
	};
	const Vector3 axisZ{
		obb.orientations[2].x * obb.size.z,
		0.0f,
		obb.orientations[2].z * obb.size.z,
	};
	return {
		center - axisX - axisZ,
		center + axisX - axisZ,
		center + axisX + axisZ,
		center - axisX + axisZ,
	};
}

bool NormalizeAxisXZ(Vector3& axis)
{
	axis.y = 0.0f;
	const float length =
		std::sqrt(axis.x * axis.x + axis.z * axis.z);
	if (length <= 0.0001f) {
		return false;
	}
	axis.x /= length;
	axis.z /= length;
	return true;
}

void ProjectCorners(
	const std::array<Vector3, 4>& corners,
	const Vector3& axis,
	float& outMin,
	float& outMax)
{
	outMin = Engine::Math::MyMath::Dot(corners[0], axis);
	outMax = outMin;
	for (size_t index = 1; index < corners.size(); ++index) {
		const float projected =
			Engine::Math::MyMath::Dot(corners[index], axis);
		outMin = (std::min)(outMin, projected);
		outMax = (std::max)(outMax, projected);
	}
}

bool TryGetObbSeparationXZ(
	const Engine::Math::OBB& a,
	const Engine::Math::OBB& b,
	Vector3& outAxis,
	float& outOverlap)
{
	const std::array<Vector3, 4> cornersA = GetObbCornersXZ(a);
	const std::array<Vector3, 4> cornersB = GetObbCornersXZ(b);
	std::array<Vector3, 4> axes{
		a.orientations[0],
		a.orientations[2],
		b.orientations[0],
		b.orientations[2],
	};

	outOverlap = FLT_MAX;
	outAxis = { 1.0f, 0.0f, 0.0f };
	for (Vector3 axis : axes) {
		if (!NormalizeAxisXZ(axis)) {
			continue;
		}
		float minA = 0.0f;
		float maxA = 0.0f;
		float minB = 0.0f;
		float maxB = 0.0f;
		ProjectCorners(cornersA, axis, minA, maxA);
		ProjectCorners(cornersB, axis, minB, maxB);
		const float overlap =
			(std::min)(maxA, maxB) - (std::max)(minA, minB);
		if (overlap <= 0.0f) {
			return false;
		}
		if (overlap < outOverlap) {
			const Vector3 centerDiff{
				b.center.x - a.center.x,
				0.0f,
				b.center.z - a.center.z,
			};
			if (Engine::Math::MyMath::Dot(
					centerDiff,
					axis) < 0.0f) {
				axis *= -1.0f;
			}
			outOverlap = overlap;
			outAxis = axis;
		}
	}
	return outOverlap < FLT_MAX;
}

int32_t ToCellCoord(float value)
{
	return static_cast<int32_t>(
		std::floor(value / kSpatialCellSize));
}

uint64_t MakeCellKey(int32_t cellX, int32_t cellZ)
{
	return DirectXGame::GameplayRules::MakeCellKey(cellX, cellZ);
}

void RegisterEnemyInOverlappingCells(
	EnemyCellMap& outMap,
	DirectXGame::Enemy& enemy)
{
	const Vector3 position = enemy.GetPosition();
	const float radius = (std::max)(0.0f, enemy.GetCollisionRadius());
	const int32_t minCellX = ToCellCoord(position.x - radius);
	const int32_t maxCellX = ToCellCoord(position.x + radius);
	const int32_t minCellZ = ToCellCoord(position.z - radius);
	const int32_t maxCellZ = ToCellCoord(position.z + radius);
	for (int32_t z = minCellZ; z <= maxCellZ; ++z) {
		for (int32_t x = minCellX; x <= maxCellX; ++x) {
			outMap[MakeCellKey(x, z)].push_back(&enemy);
		}
	}
}

void BuildActiveEnemySpatialMap(
	const std::vector<std::unique_ptr<DirectXGame::Enemy>>& enemies,
	EnemyCellMap& outMap,
	std::vector<DirectXGame::Enemy*>& activeEnemies)
{
	outMap.clear();
	activeEnemies.clear();
	for (const std::unique_ptr<DirectXGame::Enemy>& enemy :
		enemies) {
		if (!enemy || !enemy->IsActive()) {
			continue;
		}
		DirectXGame::Enemy* enemyPtr = enemy.get();
		activeEnemies.push_back(enemyPtr);
		RegisterEnemyInOverlappingCells(outMap, *enemyPtr);
	}
}

void CollectNearbyEnemies(
	const EnemyCellMap& spatialMap,
	const Vector3& center,
	float radius,
	std::vector<DirectXGame::Enemy*>& outEnemies)
{
	outEnemies.clear();
	const int32_t centerCellX = ToCellCoord(center.x);
	const int32_t centerCellZ = ToCellCoord(center.z);
	const int32_t cellRange = (std::max)(
		1,
		static_cast<int32_t>(
			std::ceil(radius / kSpatialCellSize)));
	for (int32_t z = centerCellZ - cellRange;
		z <= centerCellZ + cellRange;
		++z) {
		for (int32_t x = centerCellX - cellRange;
			x <= centerCellX + cellRange;
			++x) {
			const auto it =
				spatialMap.find(MakeCellKey(x, z));
			if (it != spatialMap.end()) {
				outEnemies.insert(
					outEnemies.end(),
					it->second.begin(),
					it->second.end());
			}
		}
	}
	std::sort(
		outEnemies.begin(),
		outEnemies.end(),
		std::less<DirectXGame::Enemy*>{});
	outEnemies.erase(
		std::unique(outEnemies.begin(), outEnemies.end()),
		outEnemies.end());
}

void ApplyEnemyHit(
	DirectXGame::Enemy& enemy,
	const Vector3& impactPosition,
	int32_t damage,
	float knockStrength,
	std::vector<Vector3>& hitEffectPositions,
	std::vector<DirectXGame::FloatingNumberEvent>& numberEvents,
	DirectXGame::PlayerManager* damageOwner)
{
	static DirectXGame::SoundHandle sharedHitSeHandle{};
	if (!sharedHitSeHandle) {
		sharedHitSeHandle =
			DirectXGame::GameAudioCache::LoadWave(kHitSePath);
	}
	if (sharedHitSeHandle) {
		DirectXGame::GameAudioCache::Play(sharedHitSeHandle);
		DirectXGame::GameAudioCache::SetVolumeFromTuning(
			sharedHitSeHandle,
			kAudioEnemyHit,
			0.5f);
	}

	const Vector3 enemyPosition = enemy.GetPosition();
	Vector3 knockDirection{
		enemyPosition.x - impactPosition.x,
		0.0f,
		enemyPosition.z - impactPosition.z,
	};
	const float length = std::sqrt(
		knockDirection.x * knockDirection.x +
		knockDirection.z * knockDirection.z);
	if (length > 0.001f) {
		knockDirection.x /= length;
		knockDirection.z /= length;
	}

	const DirectXGame::DamageResult result = damageOwner
		? damageOwner->RollDamage(damage)
		: DirectXGame::DamageResult{ damage, 0 };
	enemy.TakeDamage(result.damage, knockDirection, knockStrength);
	if (damageOwner) {
		damageOwner->ApplyLifeStealOnHit();
	}
	hitEffectPositions.push_back(enemyPosition);
	numberEvents.push_back({
		{ enemyPosition.x, enemyPosition.y + 1.2f, enemyPosition.z },
		result.damage,
		result.IsCritical()
			? Vector4{ 1.0f, 0.92f, 0.18f, 1.0f }
			: Vector4{ 1.0f, 0.28f, 0.18f, 1.0f },
		});
}

void PlayPlayerDamageSound()
{
	static DirectXGame::SoundHandle sharedPlayerDamageSeHandle{};
	if (!sharedPlayerDamageSeHandle) {
		sharedPlayerDamageSeHandle =
			DirectXGame::GameAudioCache::LoadWave(
				kPlayerDamageSePath);
	}
	if (sharedPlayerDamageSeHandle) {
		DirectXGame::GameAudioCache::Play(
			sharedPlayerDamageSeHandle);
		DirectXGame::GameAudioCache::SetVolumeFromTuning(
			sharedPlayerDamageSeHandle,
			kAudioPlayerDamage,
			0.8f);
	}
}

void CheckNormalBulletCollisions(
	DirectXGame::PlayerManager& playerManager,
	const EnemyCellMap& spatialMap,
	std::vector<DirectXGame::Enemy*>& nearbyEnemies,
	std::vector<Vector3>& hitEffectPositions,
	std::vector<DirectXGame::FloatingNumberEvent>& numberEvents)
{
	const int32_t damage =
		playerManager.GetNormalBulletDamage();
	for (const std::unique_ptr<DirectXGame::NormalBullet>& bullet :
		playerManager.GetNormalBullets()) {
		if (!bullet || !bullet->IsActive()) {
			continue;
		}
		const Vector3 bulletPosition = bullet->GetPosition();
		const float bulletRadius = bullet->GetCollisionRadius();
		const Engine::Math::OBB bulletObb =
			bullet->GetCollisionObb();
		CollectNearbyEnemies(
			spatialMap,
			bulletPosition,
			bulletRadius + kEnemyQueryPadding,
			nearbyEnemies);
		for (DirectXGame::Enemy* enemy : nearbyEnemies) {
			if (!enemy || !enemy->IsActive()) {
				continue;
			}
			if ((!Engine::Math::MyMath::IsCollision(
					bulletObb,
					enemy->GetCollisionObb()) &&
				!IsSweptBulletCollision(*bullet, *enemy)) ||
				!bullet->CanHitEnemy(enemy)) {
				continue;
			}

			bullet->RegisterHit(enemy);
			ApplyEnemyHit(
				*enemy,
				bulletPosition,
				damage,
				kFixedKnockbackStrength,
				hitEffectPositions,
				numberEvents,
				&playerManager);
			if (!bullet->ConsumeHit()) {
				break;
			}
		}
	}
}

void CheckExplosiveBulletCollisions(
	DirectXGame::PlayerManager& playerManager,
	const EnemyCellMap& spatialMap,
	std::vector<DirectXGame::Enemy*>& nearbyEnemies,
	std::vector<Vector3>& hitEffectPositions,
	std::vector<DirectXGame::FloatingNumberEvent>& numberEvents)
{
	if (!playerManager.HasExplosiveBullets()) {
		return;
	}

	const int32_t damage =
		playerManager.GetExplosiveBulletDamage();
	const float blastRadius =
		(std::max)(0.0f, playerManager.GetExplosiveBulletRadius());
	std::vector<DirectXGame::Enemy*> blastEnemies;
	for (const std::unique_ptr<DirectXGame::NormalBullet>& bullet :
		playerManager.GetExplosiveBullets()) {
		if (!bullet || !bullet->IsActive()) {
			continue;
		}
		const Vector3 bulletPosition = bullet->GetPosition();
		const Vector3 previousPosition = bullet->GetPreviousPosition();
		const float bulletRadius = bullet->GetCollisionRadius();
		const Engine::Math::OBB bulletObb =
			bullet->GetCollisionObb();
		const Vector3 queryCenter =
			(previousPosition + bulletPosition) * 0.5f;
		const float moveX = bulletPosition.x - previousPosition.x;
		const float moveZ = bulletPosition.z - previousPosition.z;
		const float queryRadius =
			0.5f * std::sqrt(moveX * moveX + moveZ * moveZ) +
			bulletRadius + kEnemyQueryPadding;
		CollectNearbyEnemies(
			spatialMap,
			queryCenter,
			queryRadius,
			nearbyEnemies);
		for (DirectXGame::Enemy* enemy : nearbyEnemies) {
			if (!enemy || !enemy->IsActive()) {
				continue;
			}
			if (!Engine::Math::MyMath::IsCollision(
					bulletObb,
					enemy->GetCollisionObb()) &&
				!IsSweptBulletCollision(*bullet, *enemy)) {
				continue;
			}

			const Vector3 impactPosition = enemy->GetPosition();
			CollectNearbyEnemies(
				spatialMap,
				impactPosition,
				blastRadius,
				blastEnemies);
			const float blastRadiusSq = blastRadius * blastRadius;
			for (DirectXGame::Enemy* blastEnemy : blastEnemies) {
				if (!blastEnemy || !blastEnemy->IsActive()) {
					continue;
				}
				const Vector3 enemyPosition = blastEnemy->GetPosition();
				const float dx = enemyPosition.x - impactPosition.x;
				const float dz = enemyPosition.z - impactPosition.z;
				if (dx * dx + dz * dz > blastRadiusSq) {
					continue;
				}
				ApplyEnemyHit(
					*blastEnemy,
					impactPosition,
					damage,
					kFixedKnockbackStrength,
					hitEffectPositions,
					numberEvents,
					&playerManager);
			}
			bullet->Deactivate();
			break;
		}
	}
}

void CheckOrbitBulletCollisions(
	DirectXGame::PlayerManager& playerManager,
	const EnemyCellMap& spatialMap,
	std::vector<DirectXGame::Enemy*>& nearbyEnemies,
	std::vector<Vector3>& hitEffectPositions,
	std::vector<DirectXGame::FloatingNumberEvent>& numberEvents)
{
	const int32_t damage =
		playerManager.GetOrbitBulletDamage();
	for (const std::unique_ptr<DirectXGame::OrbitBullet>& bullet :
		playerManager.GetOrbitBullets()) {
		if (!bullet || !bullet->IsActive()) {
			continue;
		}
		const Vector3 position = bullet->GetPosition();
		const float radius = bullet->GetCollisionRadius();
		const Engine::Math::OBB obb = bullet->GetCollisionObb();
		CollectNearbyEnemies(
			spatialMap,
			position,
			radius + kEnemyQueryPadding,
			nearbyEnemies);
		for (DirectXGame::Enemy* enemy : nearbyEnemies) {
			if (!enemy || !enemy->IsActive()) {
				continue;
			}
			if (!Engine::Math::MyMath::IsCollision(
					obb,
					enemy->GetCollisionObb()) ||
				!bullet->CanHitEnemy(enemy)) {
				continue;
			}
			bullet->RegisterHit(enemy);
			ApplyEnemyHit(
				*enemy,
				position,
				damage,
				kFixedKnockbackStrength,
				hitEffectPositions,
				numberEvents,
				&playerManager);
		}
	}
}

void CheckRicochetProjectileCollisions(
	const std::vector<std::unique_ptr<DirectXGame::NormalBullet>>& bullets,
	int32_t damage,
	bool redirectAfterHit,
	const EnemyCellMap& spatialMap,
	const std::vector<DirectXGame::Enemy*>& activeEnemies,
	std::vector<DirectXGame::Enemy*>& nearbyEnemies,
	std::vector<Vector3>& hitEffectPositions,
	std::vector<DirectXGame::FloatingNumberEvent>& numberEvents,
	DirectXGame::PlayerManager& playerManager)
{
	for (const auto& bullet : bullets) {
		if (!bullet || !bullet->IsActive()) continue;
		const Vector3 position = bullet->GetPosition();
		CollectNearbyEnemies(spatialMap, position,
			bullet->GetCollisionRadius() + kEnemyQueryPadding, nearbyEnemies);
		for (DirectXGame::Enemy* enemy : nearbyEnemies) {
			if (!enemy || !enemy->IsActive() || !bullet->CanHitEnemy(enemy)) continue;
			if (!Engine::Math::MyMath::IsCollision(
					bullet->GetCollisionObb(), enemy->GetCollisionObb()) &&
				!IsSweptBulletCollision(*bullet, *enemy)) continue;
			bullet->RegisterHit(enemy);
			ApplyEnemyHit(*enemy, position, damage, kFixedKnockbackStrength,
				hitEffectPositions, numberEvents, &playerManager);
			if (!bullet->ConsumeHit()) break;
			if (redirectAfterHit) {
				DirectXGame::Enemy* next = nullptr;
				float bestDistanceSq = FLT_MAX;
				for (DirectXGame::Enemy* candidate : activeEnemies) {
					if (!candidate || candidate == enemy || !candidate->IsActive() ||
						!bullet->CanHitEnemy(candidate)) continue;
					const Vector3 offset = candidate->GetPosition() - position;
					const float distanceSq = offset.x * offset.x + offset.z * offset.z;
					if (distanceSq < bestDistanceSq) {
						bestDistanceSq = distanceSq;
						next = candidate;
					}
				}
				if (next) bullet->RedirectToward(next->GetPosition());
			}
			break;
		}
	}
}

void CheckPlayerCollisions(
	DirectXGame::Player& player,
	DirectXGame::PlayerManager& playerManager,
	const EnemyCellMap& spatialMap,
	std::vector<DirectXGame::Enemy*>& nearbyEnemies,
	std::vector<Vector3>& deathEffectPositions)
{
	const Vector3 playerPosition = player.GetWorldPosition();
	const float playerRadius = player.GetCollisionRadius();
	const Engine::Math::OBB playerObb =
		player.GetCollisionObb();
	CollectNearbyEnemies(
		spatialMap,
		playerPosition,
		playerRadius + kEnemyQueryPadding,
		nearbyEnemies);

	for (DirectXGame::Enemy* enemy : nearbyEnemies) {
		if (!enemy || !enemy->IsActive()) {
			continue;
		}
		const Engine::Math::OBB enemyObb =
			enemy->GetCollisionObb();
		if (!Engine::Math::MyMath::IsCollision(
				playerObb,
				enemyObb)) {
			continue;
		}

		if (enemy->IsSuicideType()) {
			const Vector3 impactPosition = enemy->GetPosition();
			if (!player.IsDodging() &&
				!playerManager.IsInvincible()) {
				if (playerManager.TakeDamage(enemy->GetAttackPower())) {
					PlayPlayerDamageSound();
				}
			}
			deathEffectPositions.push_back(impactPosition);
			enemy->Deactivate();
			continue;
		}

		Vector3 enemyPosition = enemy->GetPosition();
		Vector3 separationAxis{};
		float overlap = 0.0f;
		if (TryGetObbSeparationXZ(
				playerObb,
				enemyObb,
				separationAxis,
				overlap)) {
			const float push =
				overlap * kEnemySeparationStrength;
			enemyPosition.x += separationAxis.x * push;
			enemyPosition.z += separationAxis.z * push;
			enemy->SetPosition(enemyPosition);
		}

		if (!player.IsDodging() &&
			!playerManager.IsInvincible()) {
			if (playerManager.TakeDamage(enemy->GetAttackPower())) {
				PlayPlayerDamageSound();
			}
		}
	}
}

}

namespace DirectXGame {

void EnemyCollisionSystem::RebuildContext(
	std::vector<std::unique_ptr<Enemy>>& enemies,
	EnemyCollisionContext& context)
{
	BuildActiveEnemySpatialMap(
		enemies,
		context.spatialMap,
		context.activeEnemies);
	context.nearbyEnemies.clear();
}

void EnemyCollisionSystem::CheckCollisions(
	Player& player,
	PlayerManager& playerManager,
	std::vector<std::unique_ptr<Enemy>>& enemies,
	std::vector<Vector3>& hitEffectPositions,
	std::vector<Vector3>& deathEffectPositions,
	std::vector<FloatingNumberEvent>& numberEvents)
{
	EnemyCollisionContext context;
	RebuildContext(enemies, context);
	CheckCollisions(
		player,
		playerManager,
		context,
		hitEffectPositions,
		deathEffectPositions,
		numberEvents);
}

void EnemyCollisionSystem::CheckCollisions(
	Player& player,
	PlayerManager& playerManager,
	EnemyCollisionContext& context,
	std::vector<Vector3>& hitEffectPositions,
	std::vector<Vector3>& deathEffectPositions,
	std::vector<FloatingNumberEvent>& numberEvents)
{
	CheckNormalBulletCollisions(
		playerManager,
		context.spatialMap,
		context.nearbyEnemies,
		hitEffectPositions,
		numberEvents);
	CheckOrbitBulletCollisions(
		playerManager,
		context.spatialMap,
		context.nearbyEnemies,
		hitEffectPositions,
		numberEvents);
	CheckExplosiveBulletCollisions(
		playerManager,
		context.spatialMap,
		context.nearbyEnemies,
		hitEffectPositions,
		numberEvents);
	CheckRicochetProjectileCollisions(
		playerManager.GetBoneBullets(), playerManager.GetBoneDamage(), true,
		context.spatialMap, context.activeEnemies, context.nearbyEnemies,
		hitEffectPositions, numberEvents, playerManager);
	CheckRicochetProjectileCollisions(
		playerManager.GetHandgunBullets(), playerManager.GetHandgunDamage(), true,
		context.spatialMap, context.activeEnemies, context.nearbyEnemies,
		hitEffectPositions, numberEvents, playerManager);
	CheckRicochetProjectileCollisions(
		playerManager.GetBoomerangBullets(), playerManager.GetBoomerangDamage(), false,
		context.spatialMap, context.activeEnemies, context.nearbyEnemies,
		hitEffectPositions, numberEvents, playerManager);
	CheckPlayerCollisions(
		player,
		playerManager,
		context.spatialMap,
		context.nearbyEnemies,
		deathEffectPositions);
}

void EnemyCollisionSystem::ApplyAreaDamage(
	const Vector3& center,
	float radius,
	int32_t damage,
	std::vector<std::unique_ptr<Enemy>>& enemies,
	std::vector<Vector3>& hitEffectPositions,
	std::vector<FloatingNumberEvent>& numberEvents,
	PlayerManager* damageOwner)
{
	const float radiusSq = radius * radius;
	EnemyCollisionContext context;
	RebuildContext(enemies, context);
	CollectNearbyEnemies(
		context.spatialMap,
		center,
		radius,
		context.nearbyEnemies);
	for (Enemy* enemy : context.nearbyEnemies) {
		if (!enemy || !enemy->IsActive()) {
			continue;
		}
		const Vector3 enemyPosition = enemy->GetPosition();
		const float dx = enemyPosition.x - center.x;
		const float dz = enemyPosition.z - center.z;
		if (dx * dx + dz * dz <= radiusSq) {
			ApplyEnemyHit(
				*enemy,
				center,
				damage,
				kFixedKnockbackStrength,
				hitEffectPositions,
				numberEvents,
				damageOwner);
		}
	}
}

void EnemyCollisionSystem::ApplyArcDamage(
	const Vector3& center,
	const Vector3& forward,
	float radius,
	float halfAngleRadians,
	int32_t damage,
	std::vector<std::unique_ptr<Enemy>>& enemies,
	std::vector<Vector3>& hitEffectPositions,
	std::vector<FloatingNumberEvent>& numberEvents,
	PlayerManager* damageOwner)
{
	const float radiusSq = radius * radius;
	const float minimumDot = std::cos(halfAngleRadians);
	for (const std::unique_ptr<Enemy>& enemyOwner : enemies) {
		Enemy* enemy = enemyOwner.get();
		if (!enemy || !enemy->IsActive()) {
			continue;
		}
		const Vector3 offset = enemy->GetPosition() - center;
		const float distanceSq = offset.x * offset.x + offset.z * offset.z;
		if (distanceSq > radiusSq) {
			continue;
		}
		const float distance = std::sqrt(distanceSq);
		const float directionDot = distance <= 0.0001f
			? 1.0f
			: (offset.x * forward.x + offset.z * forward.z) / distance;
		if (directionDot < minimumDot) {
			continue;
		}
		ApplyEnemyHit(
			*enemy,
			center,
			damage,
			kFixedKnockbackStrength,
			hitEffectPositions,
			numberEvents,
			damageOwner);
	}
}

void EnemyCollisionSystem::ResolveEnemySeparation(
	std::vector<std::unique_ptr<Enemy>>& enemies)
{
	EnemyCollisionContext context;
	RebuildContext(enemies, context);
	ResolveEnemySeparation(context);
}

void EnemyCollisionSystem::ResolveEnemySeparation(
	EnemyCollisionContext& context)
{
	for (Enemy* a : context.activeEnemies) {
		if (!a || !a->IsActive()) {
			continue;
		}
		CollectNearbyEnemies(
			context.spatialMap,
			a->GetPosition(),
			a->GetCollisionRadius() + kEnemyQueryPadding,
			context.nearbyEnemies);
		for (Enemy* b : context.nearbyEnemies) {
			if (!b || !b->IsActive() || !std::less<Enemy*>{}(a, b) ||
				a->IsSuicideType() || b->IsSuicideType()) {
				continue;
			}
			const Engine::Math::OBB obbA =
				a->GetCollisionObb();
			const Engine::Math::OBB obbB =
				b->GetCollisionObb();
			if (!Engine::Math::MyMath::IsCollision(
				obbA,
				obbB)) {
				continue;
			}

			Vector3 separationAxis{};
			float overlap = 0.0f;
			if (!TryGetObbSeparationXZ(
				obbA,
				obbB,
				separationAxis,
				overlap)) {
				continue;
			}

			Vector3 posA = a->GetPosition();
			Vector3 posB = b->GetPosition();
			const float push =
				overlap * 0.5f * kEnemySeparationStrength;
			posA.x -= separationAxis.x * push;
			posA.z -= separationAxis.z * push;
			posB.x += separationAxis.x * push;
			posB.z += separationAxis.z * push;
			a->SetPosition(posA);
			b->SetPosition(posB);
		}
	}
}

}
