#include "game/directxgame/enemy/EnemyManager.h"
#include "game/directxgame/core/CsvReader.h"
#include "game/directxgame/core/DirectXGameDataPaths.h"
#include "game/directxgame/core/GameAudioCache.h"
#include "game/directxgame/player/Player.h"
#include "game/directxgame/player/PlayerManager.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {

constexpr char kHitSePath[] = "audio/se/se_hit.wav";
constexpr char kPlayerDamageSePath[] = "audio/se/se_hit.wav";
constexpr char kAudioEnemyHit[] = "combat.enemyHit";
constexpr char kAudioPlayerDamage[] = "combat.playerDamage";

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

void EnemyManager::Initialize(const std::string& enemyTypesPath, Player* player, PlayerManager* playerManager)
{
	player_ = player;
	playerManager_ = playerManager;
	if (playerManager_) {
		playerManager_->SetEnemyManager(this);
	}
	LoadEnemyTypes(enemyTypesPath);
	LoadSpawnSettings(DataPaths::Resolve(DataPaths::kEnemySpawnSettings));
	spawnTimer_ = spawnInterval_;
}

void EnemyManager::LoadEnemyTypes(const std::string& filePath)
{
	enemyTypes_.clear();
	const CsvReader::CsvTable rows = CsvReader::LoadRows(filePath);
	for (const CsvReader::CsvRow& row : rows) {
		if (row.size() < 5) {
			continue;
		}

		EnemyTypeData data{};
		data.type = ToIntOr(row[0], data.type);
		data.baseHP = ToIntOr(row[1], data.baseHP);
		data.baseSpeed = ToFloatOr(row[2], data.baseSpeed);
		data.baseEXP = ToIntOr(row[3], data.baseEXP);
		data.spawnCount = ToIntOr(row[4], data.spawnCount);
		enemyTypes_.push_back(data);
	}
}

void EnemyManager::LoadSpawnSettings(const std::string& filePath)
{
	const CsvReader::KeyValueMap values = CsvReader::LoadKeyValueMap(filePath);
	for (const auto& [key, value] : values) {
		if (key == "maxActiveEnemies") { maxActiveEnemies_ = static_cast<size_t>((std::max)(1, ToIntOr(value, static_cast<int32_t>(maxActiveEnemies_)))); }
		else if (key == "spawnUnlockInterval") { spawnUnlockInterval_ = ToFloatOr(value, spawnUnlockInterval_); }
		else if (key == "spawnDistance") { spawnDistance_ = ToFloatOr(value, spawnDistance_); }
		else if (key == "respawnDistance") { respawnDistance_ = ToFloatOr(value, respawnDistance_); }
		else if (key == "respawnRadius") { respawnRadius_ = ToFloatOr(value, respawnRadius_); }
		else if (key == "minSpawnInterval") { minSpawnInterval_ = ToFloatOr(value, minSpawnInterval_); }
		else if (key == "baseSpawnInterval") {
			baseSpawnInterval_ = ToFloatOr(value, baseSpawnInterval_);
			spawnInterval_ = baseSpawnInterval_;
		} else if (key == "spawnAcceleration") {
			spawnAcceleration_ = ToFloatOr(value, spawnAcceleration_);
		}
	}
}

void EnemyManager::Update(float deltaTime)
{
	UpdateSpawnState(deltaTime);
	UpdateEnemies(deltaTime);
	RemoveInactiveEnemies();
	RelocateFarEnemies();
	UpdateExpOrbs(deltaTime);
	ResolveEnemySeparation();
}

void EnemyManager::Draw()
{
	for (std::unique_ptr<Enemy>& enemy : enemies_) {
		if (enemy && enemy->IsActive()) {
			enemy->Draw();
		}
	}
	for (std::unique_ptr<ExpOrb>& orb : expOrbs_) {
		orb->Draw();
	}
}

void EnemyManager::SetLightSettings(const GameLightSettings& lightSettings)
{
	lightSettings_ = lightSettings;
	for (std::unique_ptr<Enemy>& enemy : enemies_) {
		if (enemy) {
			enemy->SetLightSettings(lightSettings_);
		}
	}
	for (std::unique_ptr<ExpOrb>& orb : expOrbs_) {
		if (orb) {
			orb->SetLightSettings(lightSettings_);
		}
	}
}

size_t EnemyManager::GetActiveEnemyCount() const
{
	return static_cast<size_t>(std::count_if(enemies_.begin(), enemies_.end(), [](const std::unique_ptr<Enemy>& enemy) {
		return enemy && enemy->IsActive();
	}));
}

void EnemyManager::ClearRecentEffectPositions()
{
	recentHitEffectPositions_.clear();
	recentDeathEffectPositions_.clear();
}

void EnemyManager::DamageAllEnemies(int32_t damage)
{
	for (std::unique_ptr<Enemy>& enemy : enemies_) {
		if (enemy && enemy->IsActive()) {
			recentHitEffectPositions_.push_back(enemy->GetPosition());
			enemy->TakeDamage(damage);
		}
	}
}

void EnemyManager::CheckCollisions(Player* player, PlayerManager* playerManager)
{
	if (!player || !playerManager) {
		return;
	}

	EnemyCellMap spatialMap;
	std::vector<Enemy*> activeEnemies;
	BuildActiveEnemySpatialMap(spatialMap, activeEnemies);
	CheckNormalBulletCollisions(*playerManager, spatialMap);
	CheckOrbitBulletCollisions(*playerManager, spatialMap);
	CheckDroneBulletCollisions(*playerManager, spatialMap);
	CheckPlayerCollisions(*player, *playerManager, spatialMap);
}

bool EnemyManager::FindNearestEnemyPosition(const Vector3& origin, float maxDistance, Vector3& outPosition) const
{
	const float maxDistanceSq = maxDistance * maxDistance;
	float nearestDistanceSq = maxDistanceSq;
	bool found = false;

	for (const std::unique_ptr<Enemy>& enemy : enemies_) {
		if (!enemy || !enemy->IsActive()) {
			continue;
		}
		const Vector3 position = enemy->GetPosition();
		const float dx = position.x - origin.x;
		const float dz = position.z - origin.z;
		const float distanceSq = dx * dx + dz * dz;
		if (distanceSq > nearestDistanceSq) {
			continue;
		}

		nearestDistanceSq = distanceSq;
		outPosition = position;
		found = true;
	}

	return found;
}

std::vector<Vector3> EnemyManager::PickLightningTargets(int32_t count) const
{
	std::vector<Vector3> candidates;
	candidates.reserve(enemies_.size());
	for (const std::unique_ptr<Enemy>& enemy : enemies_) {
		if (enemy && enemy->IsActive()) {
			candidates.push_back(enemy->GetPosition());
		}
	}

	std::vector<Vector3> targets;
	if (candidates.empty() || count <= 0) {
		return targets;
	}

	targets.reserve(static_cast<size_t>(count));
	for (int32_t i = 0; i < count && !candidates.empty(); ++i) {
		const size_t pickedIndex = static_cast<size_t>(std::rand() % static_cast<int32_t>(candidates.size()));
		targets.push_back(candidates[pickedIndex]);
		candidates.erase(candidates.begin() + static_cast<std::ptrdiff_t>(pickedIndex));
	}

	return targets;
}

void EnemyManager::ApplyLightningDamage(const Vector3& center, float radius, int32_t damage)
{
	const float radiusSq = radius * radius;
	EnemyCellMap spatialMap;
	std::vector<Enemy*> activeEnemies;
	std::vector<Enemy*> nearbyEnemies;
	BuildActiveEnemySpatialMap(spatialMap, activeEnemies);
	CollectNearbyEnemies(spatialMap, center, radius, nearbyEnemies);

	for (Enemy* enemy : nearbyEnemies) {
		if (!enemy || !enemy->IsActive()) {
			continue;
		}
		const Vector3 enemyPosition = enemy->GetPosition();
		const float dx = enemyPosition.x - center.x;
		const float dz = enemyPosition.z - center.z;
		if (dx * dx + dz * dz <= radiusSq) {
			TryHandleBulletHit(*enemy, center, damage, 0.9f + static_cast<float>(damage) * 0.1f);
		}
	}
}

void EnemyManager::UpdateSpawnState(float deltaTime)
{
	elapsedTime_ += deltaTime;
	spawnTimer_ += deltaTime;
	spawnInterval_ = (std::max)(minSpawnInterval_, baseSpawnInterval_ - elapsedTime_ * spawnAcceleration_);
	while (spawnTimer_ >= spawnInterval_) {
		SpawnEnemies();
		spawnTimer_ -= spawnInterval_;
	}
}

void EnemyManager::SpawnEnemies()
{
	if (enemyTypes_.empty() || !player_) {
		return;
	}
	if (GetActiveEnemyCount() >= maxActiveEnemies_) {
		return;
	}

	int32_t maxIndex = static_cast<int32_t>(elapsedTime_ / spawnUnlockInterval_);
	maxIndex = std::clamp(maxIndex, 0, static_cast<int32_t>(enemyTypes_.size()) - 1);
	const EnemyTypeData& data = enemyTypes_[static_cast<size_t>(std::rand() % (maxIndex + 1))];

	for (int32_t i = 0; i < data.spawnCount; ++i) {
		if (GetActiveEnemyCount() >= maxActiveEnemies_) {
			break;
		}
		SpawnOneEnemy(data);
	}
}

void EnemyManager::SpawnOneEnemy(const EnemyTypeData& data)
{
	const Vector3 playerPosition = player_->GetWorldPosition();
	const float angle = (static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX)) * 6.283185307f;
	const Vector3 position{
		playerPosition.x + std::cos(angle) * spawnDistance_,
		0.0f,
		playerPosition.z + std::sin(angle) * spawnDistance_
	};

	auto enemy = std::make_unique<Enemy>();
	enemy->SetLightSettings(lightSettings_);
	enemy->Initialize();
	enemy->SetPlayer(player_);
	enemy->SetPosition(position);
	enemy->SetModelByType(data.type);
	enemy->SetBehaviorByType(data.type);
	enemy->SetHP(data.baseHP + static_cast<int32_t>(elapsedTime_ / 45.0f));
	enemy->SetEXP(data.baseEXP + static_cast<int32_t>(elapsedTime_ / 35.0f));
	enemy->SetSpeed(data.baseSpeed + elapsedTime_ * 0.0015f);
	enemies_.push_back(std::move(enemy));
}

void EnemyManager::UpdateEnemies(float deltaTime)
{
	for (std::unique_ptr<Enemy>& enemy : enemies_) {
		if (!enemy) {
			continue;
		}
		if (enemy->IsActive()) {
			enemy->Update(deltaTime);
		} else if (enemy->GetHP() <= 0 && enemy->JustDied()) {
			SpawnDeathDrop(*enemy);
			enemy->ResetJustDied();
		}
	}
}

void EnemyManager::RemoveInactiveEnemies()
{
	enemies_.erase(
		std::remove_if(enemies_.begin(), enemies_.end(), [](const std::unique_ptr<Enemy>& enemy) {
			return enemy && !enemy->IsActive() && !enemy->JustDied();
		}),
		enemies_.end());
}

void EnemyManager::RelocateFarEnemies()
{
	if (!player_) {
		return;
	}

	const Vector3 playerPosition = player_->GetWorldPosition();
	const float respawnDistanceSq = respawnDistance_ * respawnDistance_;
	for (std::unique_ptr<Enemy>& enemy : enemies_) {
		if (!enemy || !enemy->IsActive()) {
			continue;
		}
		const Vector3 enemyPosition = enemy->GetPosition();
		const float dx = enemyPosition.x - playerPosition.x;
		const float dz = enemyPosition.z - playerPosition.z;
		if (dx * dx + dz * dz <= respawnDistanceSq) {
			continue;
		}

		const float angle = (static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX)) * 6.283185307f;
		enemy->SetPosition({
			playerPosition.x + std::cos(angle) * respawnRadius_,
			0.0f,
			playerPosition.z + std::sin(angle) * respawnRadius_
		});
	}
}

void EnemyManager::UpdateExpOrbs(float deltaTime)
{
	if (!player_) {
		return;
	}

	const Vector3 playerPosition = player_->GetWorldPosition();
	for (auto it = expOrbs_.begin(); it != expOrbs_.end();) {
		(*it)->Update(playerPosition, deltaTime);
		if (!(*it)->IsActive()) {
			if (playerManager_) {
				playerManager_->AddEXP((*it)->GetEXP());
			}
			it = expOrbs_.erase(it);
		} else {
			++it;
		}
	}
}

void EnemyManager::ResolveEnemySeparation()
{
	EnemyCellMap spatialMap;
	std::vector<Enemy*> activeEnemies;
	std::vector<Enemy*> nearbyEnemies;
	BuildActiveEnemySpatialMap(spatialMap, activeEnemies);

	for (Enemy* a : activeEnemies) {
		if (!a || !a->IsActive()) {
			continue;
		}
		CollectNearbyEnemies(spatialMap, a->GetPosition(), kEnemySeparationDistance, nearbyEnemies);
		for (Enemy* b : nearbyEnemies) {
			if (!b || !b->IsActive() || a >= b) {
				continue;
			}
			Vector3 posA = a->GetPosition();
			Vector3 posB = b->GetPosition();
			const float dx = posB.x - posA.x;
			const float dz = posB.z - posA.z;
			const float distanceSq = dx * dx + dz * dz;
			if (distanceSq >= kEnemySeparationDistance * kEnemySeparationDistance || distanceSq <= 0.0001f) {
				continue;
			}

			const float distance = std::sqrt(distanceSq);
			const float overlap = kEnemySeparationDistance - distance;
			const float nx = dx / distance;
			const float nz = dz / distance;
			posA.x -= nx * overlap * kEnemySeparationStrength;
			posA.z -= nz * overlap * kEnemySeparationStrength;
			posB.x += nx * overlap * kEnemySeparationStrength;
			posB.z += nz * overlap * kEnemySeparationStrength;
			a->SetPosition(posA);
			b->SetPosition(posB);
		}
	}
}

void EnemyManager::SpawnDeathDrop(const Enemy& enemy)
{
	++totalKillCount_;
	recentDeathEffectPositions_.push_back(enemy.GetPosition());
	auto orb = std::make_unique<ExpOrb>();
	orb->SetLightSettings(lightSettings_);
	orb->Initialize(enemy.GetPosition(), enemy.GetEXP());
	expOrbs_.push_back(std::move(orb));
	peakExpOrbCount_ = (std::max)(peakExpOrbCount_, expOrbs_.size());
	while (expOrbs_.size() > kMaxExpOrbs) {
		expOrbs_.pop_front();
		++expOrbPruneCount_;
	}
}

bool EnemyManager::TryHandleBulletHit(Enemy& enemy, const Vector3& impactPosition, int32_t damage, float knockStrength)
{
	static SoundHandle sharedHitSeHandle = 0;
	if (sharedHitSeHandle == 0) {
		sharedHitSeHandle = GameAudioCache::LoadWave(kHitSePath);
	}
	if (sharedHitSeHandle != 0) {
		GameAudioCache::Play(sharedHitSeHandle);
		GameAudioCache::SetVolumeFromTuning(sharedHitSeHandle, kAudioEnemyHit, 0.5f);
	}

	const Vector3 enemyPosition = enemy.GetPosition();
	Vector3 knockDirection{
		enemyPosition.x - impactPosition.x,
		0.0f,
		enemyPosition.z - impactPosition.z
	};
	const float length = std::sqrt(knockDirection.x * knockDirection.x + knockDirection.z * knockDirection.z);
	if (length > 0.001f) {
		knockDirection.x /= length;
		knockDirection.z /= length;
	}

	enemy.TakeDamage(damage, knockDirection, knockStrength);
	recentHitEffectPositions_.push_back(enemyPosition);
	return true;
}

void EnemyManager::CheckNormalBulletCollisions(PlayerManager& playerManager, const EnemyCellMap& spatialMap)
{
	std::vector<Enemy*> nearbyEnemies;
	const int32_t damage = playerManager.GetNormalBulletDamage();
	for (const std::unique_ptr<NormalBullet>& bullet : playerManager.GetNormalBullets()) {
		if (!bullet || !bullet->IsActive()) {
			continue;
		}
		const Vector3 bulletPosition = bullet->GetPosition();
		CollectNearbyEnemies(spatialMap, bulletPosition, std::sqrt(kNormalBulletHitDistanceSq), nearbyEnemies);
		for (Enemy* enemy : nearbyEnemies) {
			if (!enemy || !enemy->IsActive()) {
				continue;
			}
			const Vector3 enemyPosition = enemy->GetPosition();
			const float dx = bulletPosition.x - enemyPosition.x;
			const float dz = bulletPosition.z - enemyPosition.z;
			if (dx * dx + dz * dz >= kNormalBulletHitDistanceSq) {
				continue;
			}
			if (!bullet->CanHitEnemy(enemy)) {
				continue;
			}

			bullet->RegisterHit(enemy);
			TryHandleBulletHit(*enemy, bulletPosition, damage, 0.8f + static_cast<float>(damage) * 0.18f);
			if (!bullet->ConsumeHit()) {
				break;
			}
		}
	}
}

void EnemyManager::CheckOrbitBulletCollisions(PlayerManager& playerManager, const EnemyCellMap& spatialMap)
{
	std::vector<Enemy*> nearbyEnemies;
	const int32_t damage = playerManager.GetOrbitBulletDamage();
	for (const std::unique_ptr<OrbitBullet>& orbitBullet : playerManager.GetOrbitBullets()) {
		if (!orbitBullet || !orbitBullet->IsActive()) {
			continue;
		}
		const Vector3 orbitPosition = orbitBullet->GetPosition();
		CollectNearbyEnemies(spatialMap, orbitPosition, std::sqrt(kOrbitBulletHitDistanceSq), nearbyEnemies);
		for (Enemy* enemy : nearbyEnemies) {
			if (!enemy || !enemy->IsActive()) {
				continue;
			}
			const Vector3 enemyPosition = enemy->GetPosition();
			const float dx = orbitPosition.x - enemyPosition.x;
			const float dz = orbitPosition.z - enemyPosition.z;
			if (dx * dx + dz * dz >= kOrbitBulletHitDistanceSq) {
				continue;
			}
			if (!orbitBullet->CanHitEnemy(enemy)) {
				continue;
			}

			orbitBullet->RegisterHit(enemy);
			TryHandleBulletHit(*enemy, orbitPosition, damage, 0.7f + static_cast<float>(damage) * 0.12f);
		}
	}
}

void EnemyManager::CheckDroneBulletCollisions(PlayerManager& playerManager, const EnemyCellMap& spatialMap)
{
	if (!playerManager.HasDrone() || !playerManager.GetDrone()) {
		return;
	}

	std::vector<Enemy*> nearbyEnemies;
	const int32_t damage = playerManager.GetDroneDamage();
	for (const std::unique_ptr<NormalBullet>& bullet : playerManager.GetDrone()->GetBullets()) {
		if (!bullet || !bullet->IsActive()) {
			continue;
		}
		const Vector3 bulletPosition = bullet->GetPosition();
		CollectNearbyEnemies(spatialMap, bulletPosition, std::sqrt(kNormalBulletHitDistanceSq), nearbyEnemies);
		for (Enemy* enemy : nearbyEnemies) {
			if (!enemy || !enemy->IsActive()) {
				continue;
			}
			const Vector3 enemyPosition = enemy->GetPosition();
			const float dx = bulletPosition.x - enemyPosition.x;
			const float dz = bulletPosition.z - enemyPosition.z;
			if (dx * dx + dz * dz >= kNormalBulletHitDistanceSq) {
				continue;
			}
			if (!bullet->CanHitEnemy(enemy)) {
				continue;
			}

			bullet->RegisterHit(enemy);
			TryHandleBulletHit(*enemy, bulletPosition, damage, 0.65f);
			if (!bullet->ConsumeHit()) {
				break;
			}
		}
	}
}

void EnemyManager::CheckPlayerCollisions(Player& player, PlayerManager& playerManager, const EnemyCellMap& spatialMap)
{
	const Vector3 playerPosition = player.GetWorldPosition();
	std::vector<Enemy*> nearbyEnemies;
	CollectNearbyEnemies(spatialMap, playerPosition, kPlayerContactDistance, nearbyEnemies);
	for (Enemy* enemy : nearbyEnemies) {
		if (!enemy || !enemy->IsActive()) {
			continue;
		}

		Vector3 enemyPosition = enemy->GetPosition();
		const float dx = enemyPosition.x - playerPosition.x;
		const float dz = enemyPosition.z - playerPosition.z;
		const float distanceSq = dx * dx + dz * dz;
		if (distanceSq >= kPlayerContactDistanceSq || distanceSq <= 0.0001f) {
			continue;
		}

		const float distance = std::sqrt(distanceSq);
		const float overlap = kPlayerContactDistance - distance;
		const float nx = dx / distance;
		const float nz = dz / distance;
		enemyPosition.x += nx * overlap;
		enemyPosition.z += nz * overlap;
		enemy->SetPosition(enemyPosition);

		if (!playerManager.IsInvincible()) {
			playerManager.TakeDamage();
			static SoundHandle sharedPlayerDamageSeHandle = 0;
			if (sharedPlayerDamageSeHandle == 0) {
				sharedPlayerDamageSeHandle = GameAudioCache::LoadWave(kPlayerDamageSePath);
			}
			if (sharedPlayerDamageSeHandle != 0) {
				GameAudioCache::Play(sharedPlayerDamageSeHandle);
				GameAudioCache::SetVolumeFromTuning(sharedPlayerDamageSeHandle, kAudioPlayerDamage, 0.8f);
			}
		}
	}
}

void EnemyManager::BuildActiveEnemySpatialMap(EnemyCellMap& outMap, std::vector<Enemy*>& activeEnemies) const
{
	outMap.clear();
	activeEnemies.clear();
	for (const std::unique_ptr<Enemy>& enemy : enemies_) {
		if (!enemy || !enemy->IsActive()) {
			continue;
		}
		Enemy* enemyPtr = enemy.get();
		activeEnemies.push_back(enemyPtr);
		const Vector3 position = enemyPtr->GetPosition();
		outMap[MakeCellKey(ToCellCoord(position.x), ToCellCoord(position.z))].push_back(enemyPtr);
	}
}

void EnemyManager::CollectNearbyEnemies(const EnemyCellMap& spatialMap, const Vector3& center, float radius, std::vector<Enemy*>& outEnemies) const
{
	outEnemies.clear();
	const int32_t centerCellX = ToCellCoord(center.x);
	const int32_t centerCellZ = ToCellCoord(center.z);
	const int32_t cellRange = (std::max)(1, static_cast<int32_t>(std::ceil(radius / kSpatialCellSize)));
	for (int32_t z = centerCellZ - cellRange; z <= centerCellZ + cellRange; ++z) {
		for (int32_t x = centerCellX - cellRange; x <= centerCellX + cellRange; ++x) {
			const auto it = spatialMap.find(MakeCellKey(x, z));
			if (it != spatialMap.end()) {
				outEnemies.insert(outEnemies.end(), it->second.begin(), it->second.end());
			}
		}
	}
}

int32_t EnemyManager::ToCellCoord(float value)
{
	return static_cast<int32_t>(std::floor(value / kSpatialCellSize));
}

int64_t EnemyManager::MakeCellKey(int32_t cellX, int32_t cellZ)
{
	return (static_cast<int64_t>(cellX) << 32) ^ static_cast<uint32_t>(cellZ);
}

} // namespace DirectXGame
