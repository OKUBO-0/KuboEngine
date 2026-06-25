#include "game/directxgame/enemy/EnemyCollisionSystem.h"
#include "game/directxgame/core/GameplayRules.h"

#include "MyMath.h"
#include "game/directxgame/core/GameAudioCache.h"
#include "game/directxgame/enemy/Enemy.h"
#include "game/directxgame/player/Player.h"
#include "game/directxgame/player/PlayerManager.h"
#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <functional>
#include <unordered_map>

namespace {

using EnemyCellMap =
	DirectXGame::EnemyCollisionContext::EnemyCellMap;

constexpr char kHitSePath[] = "audio/se/se_hit.wav";
constexpr char kPlayerDamageSePath[] = "audio/se/se_hit.wav";
constexpr char kAudioEnemyHit[] = "combat.enemyHit";
constexpr char kAudioPlayerDamage[] = "combat.playerDamage";
constexpr float kEnemySeparationStrength = 1.1f;
constexpr float kSpatialCellSize = 8.0f;
constexpr float kEnemyQueryPadding = 2.0f;

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
	std::vector<Vector3>& hitEffectPositions)
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

	enemy.TakeDamage(damage, knockDirection, knockStrength);
	hitEffectPositions.push_back(enemyPosition);
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
	std::vector<Vector3>& hitEffectPositions)
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
			if (!Engine::Math::MyMath::IsCollision(
					bulletObb,
					enemy->GetCollisionObb()) ||
				!bullet->CanHitEnemy(enemy)) {
				continue;
			}

			bullet->RegisterHit(enemy);
			ApplyEnemyHit(
				*enemy,
				bulletPosition,
				damage,
				0.8f + static_cast<float>(damage) * 0.18f,
				hitEffectPositions);
			if (!bullet->ConsumeHit()) {
				break;
			}
		}
	}
}

void CheckOrbitBulletCollisions(
	DirectXGame::PlayerManager& playerManager,
	const EnemyCellMap& spatialMap,
	std::vector<DirectXGame::Enemy*>& nearbyEnemies,
	std::vector<Vector3>& hitEffectPositions)
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
				0.7f + static_cast<float>(damage) * 0.12f,
				hitEffectPositions);
		}
	}
}

void CheckDroneBulletCollisions(
	DirectXGame::PlayerManager& playerManager,
	const EnemyCellMap& spatialMap,
	std::vector<DirectXGame::Enemy*>& nearbyEnemies,
	std::vector<Vector3>& hitEffectPositions)
{
	if (!playerManager.HasDrone() || !playerManager.GetDrone()) {
		return;
	}

	const int32_t damage = playerManager.GetDroneDamage();
	for (const std::unique_ptr<DirectXGame::NormalBullet>& bullet :
		playerManager.GetDrone()->GetBullets()) {
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
				0.65f,
				hitEffectPositions);
			if (!bullet->ConsumeHit()) {
				break;
			}
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
				playerManager.TakeDamage();
				PlayPlayerDamageSound();
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
			playerManager.TakeDamage();
			PlayPlayerDamageSound();
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
	std::vector<Vector3>& deathEffectPositions)
{
	EnemyCollisionContext context;
	RebuildContext(enemies, context);
	CheckCollisions(
		player,
		playerManager,
		context,
		hitEffectPositions,
		deathEffectPositions);
}

void EnemyCollisionSystem::CheckCollisions(
	Player& player,
	PlayerManager& playerManager,
	EnemyCollisionContext& context,
	std::vector<Vector3>& hitEffectPositions,
	std::vector<Vector3>& deathEffectPositions)
{
	CheckNormalBulletCollisions(
		playerManager,
		context.spatialMap,
		context.nearbyEnemies,
		hitEffectPositions);
	CheckOrbitBulletCollisions(
		playerManager,
		context.spatialMap,
		context.nearbyEnemies,
		hitEffectPositions);
	CheckDroneBulletCollisions(
		playerManager,
		context.spatialMap,
		context.nearbyEnemies,
		hitEffectPositions);
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
	std::vector<Vector3>& hitEffectPositions)
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
				0.9f + static_cast<float>(damage) * 0.1f,
				hitEffectPositions);
		}
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
