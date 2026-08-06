#include "EnemyManager.h"
#include "DataPaths.h"
#include "GameAudioCache.h"
#include "GameSession.h"
#include "Player.h"
#include "PlayerManager.h"
#include <algorithm>
#include <charconv>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <numbers>
#include <string_view>

namespace DirectXGame {
namespace {

constexpr char kCoinGainSePath[] = "se/coin_gain.wav";
constexpr char kAudioCoinGain[] = "combat.coinGain";
constexpr float kEnemyShadowLodDistanceSq = 42.0f * 42.0f;
constexpr float kEnemyAnimationLodNearDistanceSq = 28.0f * 28.0f;
constexpr float kEnemyAnimationLodMidDistanceSq = 52.0f * 52.0f;
constexpr float kEnemySimplifiedNearDistanceSq = 38.0f * 38.0f;
constexpr float kEnemySimplifiedFarDistanceSq = 62.0f * 62.0f;
constexpr size_t kEnemyFullModelBudget = 128;
constexpr float kNormalEnemyDeathPresentationDuration = 0.52f;
constexpr float kBossSummonCrystalOffset = 12.0f;

uint32_t ResolveEnemyAnimationStride(
	size_t activeEnemyCount,
	const Vector3& playerPosition,
	const Enemy& enemy)
{
	if (enemy.IsBoss()) {
		return 1u;
	}

	const Vector3 offset = enemy.GetPosition() - playerPosition;
	const float distanceSq =
		offset.x * offset.x + offset.y * offset.y + offset.z * offset.z;
	const uint32_t loadStride =
		activeEnemyCount >= 210 ? 4u :
		activeEnemyCount >= 145 ? 3u :
		activeEnemyCount >= 90 ? 2u :
		1u;
	const uint32_t distanceStride =
		distanceSq >= kEnemyAnimationLodMidDistanceSq ? 4u :
		distanceSq >= kEnemyAnimationLodNearDistanceSq ? 2u :
		1u;
	return (std::max)(loadStride, distanceStride);
}

bool ShouldUseSimplifiedEnemyRender(
	size_t activeEnemyCount,
	const Vector3& playerPosition,
	const Enemy& enemy)
{
	if (enemy.IsBoss() || enemy.IsDeathPresentationActive()) {
		return false;
	}
	if (activeEnemyCount <= kEnemyFullModelBudget) {
		return false;
	}
	const Vector3 offset = enemy.GetPosition() - playerPosition;
	const float distanceSq =
		offset.x * offset.x + offset.y * offset.y + offset.z * offset.z;
	if (distanceSq >= kEnemySimplifiedFarDistanceSq) {
		return true;
	}
	return activeEnemyCount >= 140 &&
		distanceSq >= kEnemySimplifiedNearDistanceSq;
}

Vector3 RotateDirectionXZ(const Vector3& direction, float radians)
{
	const float s = std::sin(radians);
	const float c = std::cos(radians);
	return {
		direction.x * c + direction.z * s,
		0.0f,
		-direction.x * s + direction.z * c,
	};
}

bool IsPlayerInBeam(
	const Vector3& playerPosition,
	const Vector3& origin,
	const Vector3& direction,
	float length,
	float halfWidth)
{
	const Vector3 offset = playerPosition - origin;
	const float projection = offset.x * direction.x + offset.z * direction.z;
	if (projection < 0.0f || projection > length) {
		return false;
	}
	const Vector3 closest{
		origin.x + direction.x * projection,
		0.0f,
		origin.z + direction.z * projection,
	};
	const Vector3 playerOffset = playerPosition - closest;
	return playerOffset.x * playerOffset.x + playerOffset.z * playerOffset.z <=
		halfWidth * halfWidth;
}

}

void EnemyManager::Initialize(const std::string& enemyTypesPath, Player* player, PlayerManager* playerManager)
{
	player_ = player;
	playerManager_ = playerManager;
	bossEnemy_ = nullptr;
	deathBombs_.clear();
	bossInkProjectiles_.clear();
	pendingBossInkWaves_ = 0;
	bossInkWaveTimer_ = 0.0f;
	bossInkNextWaveIndex_ = 0;
	bossBeamBursts_.clear();
	bossShockwaveRings_.clear();
	bossSummonCrystals_.clear();
	bossShockwaveDamageWaves_.clear();
	bossSlamCubes_.clear();
	bossPhase_ = false;
	bossDefeated_ = false;
	char* collisionMode = nullptr;
	size_t collisionModeLength = 0;
	if (_dupenv_s(&collisionMode, &collisionModeLength, "KUBO_COLLISION_MODE") == 0 &&
		collisionMode) {
		const std::string_view mode(collisionMode);
		collisionContext_.broadPhaseMode =
			(mode == "brute_force" || mode == "brute")
			? EnemyBroadPhaseMode::BruteForce
			: EnemyBroadPhaseMode::SpatialGrid;
	}
	std::free(collisionMode);
	char* telemetryEnabled = nullptr;
	size_t telemetryEnabledLength = 0;
	if (_dupenv_s(&telemetryEnabled, &telemetryEnabledLength,
		"KUBO_COLLISION_TELEMETRY") == 0 && telemetryEnabled) {
		collisionTelemetryEnabled_ = std::string_view(telemetryEnabled) == "1";
	}
	std::free(telemetryEnabled);
	if (playerManager_) {
		playerManager_->SetEnemyManager(this);
	}
	spawnController_.Initialize(
		enemyTypesPath,
		DataPaths::Resolve(DataPaths::kEnemySpawnSettings));
	char* benchmarkEnemies = nullptr;
	size_t benchmarkEnemiesLength = 0;
	if (_dupenv_s(&benchmarkEnemies, &benchmarkEnemiesLength,
		"KUBO_COLLISION_BENCHMARK_ENEMIES") == 0 && benchmarkEnemies) {
		size_t targetCount = 0;
		const char* end = benchmarkEnemies +
			std::char_traits<char>::length(benchmarkEnemies);
		const auto result = std::from_chars(benchmarkEnemies, end, targetCount);
		if (result.ec == std::errc{} && result.ptr == end && targetCount > 0) {
			spawnController_.SpawnBenchmarkEnemies(player_, enemies_, targetCount);
		}
	}
	std::free(benchmarkEnemies);
}

void EnemyManager::LoadEnemyTypes(const std::string& filePath)
{
	spawnController_.LoadEnemyTypes(filePath);
}

void EnemyManager::LoadSpawnSettings(const std::string& filePath)
{
	spawnController_.LoadSpawnSettings(filePath);
}

void EnemyManager::SetRandomSeed(uint32_t seed)
{
	randomEngine_.seed(seed);
	spawnController_.SetRandomSeed(seed ^ 0xA511E9B3u);
}

void EnemyManager::AppendCollisionTelemetryCsv(uint32_t frame) const
{
	if (!collisionTelemetryEnabled_) {
		return;
	}
	const std::string path = DataPaths::Resolve(DataPaths::kCollisionTelemetry);
	bool writeHeader = true;
	{
		std::ifstream existing(path);
		writeHeader = !existing.good() ||
			existing.peek() == std::ifstream::traits_type::eof();
	}
	std::ofstream file(path, std::ios::app);
	if (!file.is_open()) {
		return;
	}
	if (writeHeader) {
		file << "frame,mode,state,level,enemyCount,normalBulletCount,orbitBulletCount,"
			"queryCount,nearbyCandidateCount,bruteForceCandidateCount,"
			"candidateReductionPercent,spatialBuildMilliseconds,collisionMilliseconds,"
			"separationMilliseconds\n";
	}
	const auto& telemetry = collisionContext_.telemetry;
	file << frame << ','
		<< (collisionContext_.broadPhaseMode == EnemyBroadPhaseMode::BruteForce
			? "brute_force" : "spatial_grid") << ','
		<< "scene_stress,0,"
		<< GetActiveEnemyCount() << ','
		<< (playerManager_ ? playerManager_->GetNormalBullets().size() : 0) << ','
		<< (playerManager_ ? playerManager_->GetOrbitBullets().size() : 0) << ','
		<< telemetry.queryCount << ','
		<< telemetry.nearbyCandidateCount << ','
		<< telemetry.bruteForceCandidateCount << ','
		<< telemetry.CandidateReductionPercent() << ','
		<< telemetry.spatialBuildMilliseconds << ','
		<< telemetry.collisionMilliseconds << ','
		<< telemetry.separationMilliseconds << '\n';
}

void EnemyManager::Update(float deltaTime)
{
	collisionContext_.telemetry = {};
	spawnController_.Update(
		deltaTime,
		player_,
		enemies_,
		bossPhase_);
	UpdateEnemies(deltaTime);
	ProcessBossAttackEvents();
	UpdateBossInkProjectiles(deltaTime);
	UpdateBossAttackVisuals(deltaTime);
	UpdateDeathBombs(deltaTime);
	RemoveInactiveEnemies();
	spawnController_.RelocateFarEnemies(
		player_,
		enemies_,
		bossEnemy_,
		bossPhase_);
	UpdateExpOrbs(deltaTime);
	EnemyCollisionSystem::RebuildContext(enemies_, collisionContext_);
	EnemyCollisionSystem::ResolveEnemySeparation(collisionContext_);
}

void EnemyManager::Draw()
{
	for (std::unique_ptr<Enemy>& enemy : enemies_) {
		if (enemy && (enemy->IsActive() || enemy->IsDeathPresentationActive())) {
			enemy->DrawFloatingShadow();
			Engine::Graphics3D::Object3D* renderObject = enemy->GetRenderObject();
			Engine::Graphics3D::Object3D::SubmitForDraw(renderObject);
		}
	}
	std::vector<Engine::Graphics3D::Object3D*> staticObjects;
	staticObjects.reserve(
		expOrbs_.size() +
		deathBombs_.size() +
		bossInkProjectiles_.size() +
		bossBeamBursts_.size() * 160u +
		bossShockwaveRings_.size() * 96u +
		bossSummonCrystals_.size() +
		bossSlamCubes_.size() +
		10u);
	for (std::unique_ptr<ExpOrb>& orb : expOrbs_) {
		if (!orb) {
			continue;
		}
		if (Engine::Graphics3D::Object3D* object = orb->GetRenderObject()) {
			staticObjects.push_back(object);
		}
	}
	for (const std::unique_ptr<EnemyDeathBomb>& bomb : deathBombs_) {
		if (!bomb) {
			continue;
		}
		if (Engine::Graphics3D::Object3D* object = bomb->GetRenderObject()) {
			staticObjects.push_back(object);
		}
	}
	for (const std::unique_ptr<BossInkProjectile>& projectile :
		bossInkProjectiles_) {
		if (!projectile) {
			continue;
		}
		if (Engine::Graphics3D::Object3D* object = projectile->GetRenderObject()) {
			staticObjects.push_back(object);
		}
	}
	bossRushTelegraph_.AppendRenderObjects(staticObjects);
	bossAreaTelegraph_.AppendRenderObjects(staticObjects);
	for (const std::unique_ptr<BossBeamBurst>& beam : bossBeamBursts_) {
		if (beam) {
			beam->AppendRenderObjects(staticObjects);
		}
	}
	for (const std::unique_ptr<BossShockwaveRing>& ring : bossShockwaveRings_) {
		if (ring) {
			ring->AppendRenderObjects(staticObjects);
		}
	}
	for (const std::unique_ptr<BossSummonCrystal>& crystal : bossSummonCrystals_) {
		if (!crystal) {
			continue;
		}
		if (Engine::Graphics3D::Object3D* object = crystal->GetRenderObject()) {
			staticObjects.push_back(object);
		}
	}
	for (const std::unique_ptr<BossSlamCube>& cube : bossSlamCubes_) {
		if (!cube) {
			continue;
		}
		if (Engine::Graphics3D::Object3D* object = cube->GetRenderObject()) {
			staticObjects.push_back(object);
		}
	}
	for (Engine::Graphics3D::Object3D* object : staticObjects) {
		Engine::Graphics3D::Object3D::SubmitForDraw(object);
	}
}

void EnemyManager::DrawShadow()
{
	const Vector3 playerPosition =
		player_ ? player_->GetWorldPosition() : Vector3{};
	for (std::unique_ptr<Enemy>& enemy : enemies_) {
		if (enemy && (enemy->IsActive() || enemy->IsDeathPresentationActive())) {
			if (!enemy->IsBoss()) {
				const Vector3 enemyPosition = enemy->GetPosition();
				const float dx = enemyPosition.x - playerPosition.x;
				const float dz = enemyPosition.z - playerPosition.z;
				if (dx * dx + dz * dz > kEnemyShadowLodDistanceSq) {
					continue;
				}
			}
			enemy->DrawShadow();
		}
	}
	for (const std::unique_ptr<EnemyDeathBomb>& bomb : deathBombs_) {
		if (!bomb) {
			continue;
		}
		if (Engine::Graphics3D::Object3D* object = bomb->GetRenderObject()) {
			Engine::Graphics3D::Object3D::SubmitForShadow(object);
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
	recentExplosionEffectPositions_.clear();
	recentFloatingNumberEvents_.clear();
}

void EnemyManager::DamageAllEnemies(int32_t damage)
{
	for (std::unique_ptr<Enemy>& enemy : enemies_) {
		if (enemy && enemy->IsActive()) {
			recentHitEffectPositions_.push_back(enemy->GetPosition());
			recentFloatingNumberEvents_.push_back({
				{ enemy->GetPosition().x, enemy->GetPosition().y + 1.2f, enemy->GetPosition().z },
				damage,
				{ 1.0f, 0.28f, 0.18f, 1.0f },
				});
			enemy->TakeDamage(damage);
		}
	}
}

void EnemyManager::CheckCollisions(Player* player, PlayerManager* playerManager)
{
	if (!player || !playerManager) {
		return;
	}

	EnemyCollisionSystem::RebuildContext(enemies_, collisionContext_);
	EnemyCollisionSystem::CheckCollisions(
		*player,
		*playerManager,
		collisionContext_,
		recentHitEffectPositions_,
		recentDeathEffectPositions_,
		recentExplosionEffectPositions_,
		recentFloatingNumberEvents_);
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
		const size_t pickedIndex = std::uniform_int_distribution<size_t>(
			0,
			candidates.size() - 1)(randomEngine_);
		targets.push_back(candidates[pickedIndex]);
		candidates[pickedIndex] = candidates.back();
		candidates.pop_back();
	}

	return targets;
}

std::vector<Vector3> EnemyManager::PickLightningChainTargets(
	const Vector3& origin,
	int32_t count,
	float chainRange) const
{
	struct Candidate {
		Enemy* enemy = nullptr;
		Vector3 position{};
	};
	std::vector<Candidate> candidates;
	candidates.reserve(enemies_.size());
	for (const std::unique_ptr<Enemy>& enemy : enemies_) {
		if (enemy && enemy->IsActive()) {
			candidates.push_back({ enemy.get(), enemy->GetPosition() });
		}
	}

	std::vector<Vector3> targets;
	if (candidates.empty() || count <= 0) {
		return targets;
	}

	targets.reserve(static_cast<size_t>(count));
	Vector3 current = origin;
	const float chainRangeSq = chainRange * chainRange;
	for (int32_t chainIndex = 0; chainIndex < count && !candidates.empty(); ++chainIndex) {
		size_t bestIndex = candidates.size();
		float bestDistanceSq = FLT_MAX;
		for (size_t index = 0; index < candidates.size(); ++index) {
			const Vector3 offset = candidates[index].position - current;
			const float distanceSq = offset.x * offset.x + offset.z * offset.z;
			if (chainIndex > 0 && distanceSq > chainRangeSq) {
				continue;
			}
			if (distanceSq < bestDistanceSq) {
				bestDistanceSq = distanceSq;
				bestIndex = index;
			}
		}
		if (bestIndex >= candidates.size()) {
			break;
		}
		current = candidates[bestIndex].position;
		targets.push_back(current);
		candidates[bestIndex] = candidates.back();
		candidates.pop_back();
	}

	return targets;
}

void EnemyManager::ApplyLightningDamage(const Vector3& center, float radius, int32_t damage)
{
	ApplyAreaDamage(center, radius, damage);
}

void EnemyManager::ApplyAreaDamage(
	const Vector3& center,
	float radius,
	int32_t damage)
{
	EnemyCollisionSystem::ApplyAreaDamage(
		center,
		radius,
		damage,
		enemies_,
		recentHitEffectPositions_,
		recentFloatingNumberEvents_,
		playerManager_);
}

void EnemyManager::ApplyArcDamage(
	const Vector3& center,
	const Vector3& forward,
	float radius,
	float halfAngleRadians,
	int32_t damage,
	float knockStrength)
{
	EnemyCollisionSystem::ApplyArcDamage(
		center,
		forward,
		radius,
		halfAngleRadians,
		damage,
		enemies_,
		recentHitEffectPositions_,
		recentFloatingNumberEvents_,
		playerManager_,
		knockStrength);
}

void EnemyManager::StartBossPhase()
{
	if (bossPhase_ || !player_) {
		return;
	}

	bossPhase_ = true;
	bossDefeated_ = false;
	enemies_.clear();

	std::unique_ptr<Enemy> enemy =
		spawnController_.CreateBossEnemy(
			player_,
			playerManager_ ? playerManager_->GetLevel() : 1);
	if (!enemy) {
		bossPhase_ = false;
		return;
	}
	bossEnemy_ = enemy.get();
	bossEnemy_->SetBehaviorVisual({ 0.58f, 0.3f, 1.0f, 1.0f }, 2.2f);
	bossEnemy_->SetPosition(bossEnemy_->GetPosition());
	enemies_.push_back(std::move(enemy));
}

bool EnemyManager::ConsumeBossPhaseChanged(Vector3& outPosition, int32_t& outPhase)
{
	if (!bossEnemy_ || !bossEnemy_->ConsumeBossPhaseChanged()) {
		return false;
	}
	outPosition = bossEnemy_->GetPosition();
	outPhase = bossEnemy_->GetBossPhase();
	return true;
}

bool EnemyManager::HasActiveBoss() const
{
	return bossPhase_ && bossEnemy_ &&
		(bossEnemy_->IsActive() || bossEnemy_->IsDeathPresentationActive());
}

int32_t EnemyManager::GetBossHP() const
{
	return HasActiveBoss() ? bossEnemy_->GetHP() : 0;
}

int32_t EnemyManager::GetBossMaxHP() const
{
	return HasActiveBoss() ? bossEnemy_->GetMaxHP() : 1;
}

bool EnemyManager::GetBossPresentationPosition(Vector3& outPosition) const
{
	if (!bossEnemy_) {
		return false;
	}
	outPosition = bossEnemy_->GetPosition();
	return true;
}

void EnemyManager::UpdateBossDeathPresentation(float elapsedTime, float duration)
{
	if (bossEnemy_ && bossEnemy_->IsDeathPresentationActive()) {
		bossEnemy_->UpdateDeathPresentation(elapsedTime, duration);
	}
}

void EnemyManager::UpdateEnemies(float deltaTime)
{
	const size_t activeEnemyCount = GetActiveEnemyCount();
	const Vector3 playerPosition = player_
		? player_->GetWorldPosition()
		: Vector3{};
	animationLodStats_ = {};
	renderLodStats_ = {};
	for (std::unique_ptr<Enemy>& enemy : enemies_) {
		if (!enemy) {
			continue;
		}
		if (enemy->IsActive()) {
			const bool simplifiedRender = ShouldUseSimplifiedEnemyRender(
				activeEnemyCount,
				playerPosition,
				*enemy);
			enemy->SetSimplifiedRenderEnabled(simplifiedRender);
			if (simplifiedRender) {
				++renderLodStats_.simplifiedModelCount;
			} else {
				++renderLodStats_.fullModelCount;
			}
			const uint32_t animationStride = ResolveEnemyAnimationStride(
				activeEnemyCount,
				playerPosition,
				*enemy);
			switch (animationStride) {
			case 1:
				++animationLodStats_.stride1Count;
				break;
			case 2:
				++animationLodStats_.stride2Count;
				break;
			case 3:
				++animationLodStats_.stride3Count;
				break;
			default:
				++animationLodStats_.stride4Count;
				break;
			}
			enemy->SetAnimationUpdateStride(animationStride);
			enemy->Update(deltaTime);
			if (enemy->ConsumeGroundImpact()) {
				recentDeathEffectPositions_.push_back(enemy->GetPosition());
			}
		} else if (enemy->IsDeathPresentationActive() && enemy.get() != bossEnemy_) {
			if (enemy->UpdateDeathPresentationFrame(
					deltaTime,
					kNormalEnemyDeathPresentationDuration)) {
				enemy->FinishDeathPresentation();
			}
		} else if (enemy->GetHP() <= 0 && enemy->JustDied()) {
			if (enemy.get() == bossEnemy_) {
				bossDefeated_ = true;
			}
			enemy->StartDeathPresentation();
			SpawnDeathDrop(*enemy);
			SpawnDeathBomb(*enemy);
			enemy->ResetJustDied();
		}
	}
}

void EnemyManager::SetBossPresentationPosition(const Vector3& position)
{
	if (bossEnemy_) {
		bossEnemy_->SetPosition(position);
	}
}

void EnemyManager::SpawnDeathBomb(const Enemy& enemy)
{
	if (!enemy.IsDeathBombType()) {
		return;
	}
	auto bomb = std::make_unique<EnemyDeathBomb>();
	bomb->Initialize(
		enemy.GetPosition(),
		enemy.GetDeathBombDelay(),
		enemy.GetDeathBombRadius(),
		enemy.GetDeathBombDamage());
	deathBombs_.push_back(std::move(bomb));
}

void EnemyManager::UpdateDeathBombs(float deltaTime)
{
	for (auto it = deathBombs_.begin(); it != deathBombs_.end();) {
		EnemyDeathBomb* bomb = it->get();
		if (!bomb || !bomb->Update(deltaTime)) {
			++it;
			continue;
		}

		const Vector3 position = bomb->GetPosition();
		const float radius = bomb->GetRadius();
		const int32_t damage = bomb->GetDamage();
		EnemyCollisionSystem::ApplyAreaDamage(
			position,
			radius,
			damage,
			enemies_,
			recentHitEffectPositions_,
			recentFloatingNumberEvents_);

		if (player_ && playerManager_) {
			const Vector3 playerPosition = player_->GetWorldPosition();
			const float dx = playerPosition.x - position.x;
			const float dz = playerPosition.z - position.z;
			if (dx * dx + dz * dz <= radius * radius) {
				playerManager_->TakeDamage(damage);
			}
		}
		recentExplosionEffectPositions_.push_back(position);
		it = deathBombs_.erase(it);
	}
}

void EnemyManager::ProcessBossAttackEvents()
{
	if (!bossEnemy_ || !bossEnemy_->IsActive()) {
		return;
	}
	BossAttackEvent event{};
	while (bossEnemy_->ConsumeBossAttack(event)) {
		bossEnemy_->NotifyAttack();
		const int32_t bossDamage = bossEnemy_->GetAttackPower();
		const auto damagePlayerIfInRadius = [&](const Vector3& center, float radius, int32_t damage) {
			if (!player_ || !playerManager_ || player_->IsDodging() ||
				playerManager_->IsInvincible()) {
				return;
			}
			const Vector3 playerPosition = player_->GetWorldPosition();
			const float dx = playerPosition.x - center.x;
			const float dz = playerPosition.z - center.z;
			if (dx * dx + dz * dz <= radius * radius) {
				playerManager_->TakeDamage(damage);
				recentExplosionEffectPositions_.push_back(center);
			}
		};
		const auto spawnShockwaveImpact = [&](const Vector3& center, int32_t visualCount) {
			for (int32_t index = 0; index < visualCount; ++index) {
				auto cube = std::make_unique<BossSlamCube>();
				cube->Initialize(center, static_cast<float>(index) * 0.08f);
				bossSlamCubes_.push_back(std::move(cube));
			}
		};
		const auto addShockwaveDamage = [&](const Vector3& center, float radius, float delay, int32_t damage) {
			bossShockwaveDamageWaves_.push_back({
				center,
				radius,
				delay,
				0.0f,
				damage,
				false,
			});
		};

		if (event.type == BossAttackType::Beam ||
			event.type == BossAttackType::TripleBeam) {
			const Vector3 beamTarget = player_ ? player_->GetWorldPosition() : event.targetPosition;
			const Vector3 beamOffset = beamTarget - event.position;
			const float beamDistance = std::sqrt(
				beamOffset.x * beamOffset.x + beamOffset.z * beamOffset.z);
			const float beamLength = (std::max)(36.0f, beamDistance + 32.0f);
			auto beam = std::make_unique<BossBeamBurst>();
			beam->Initialize(
				event.position,
				event.direction,
				event.type == BossAttackType::TripleBeam ? 3 : 1,
				beamLength);
			bossBeamBursts_.push_back(std::move(beam));
			if (!player_ || !playerManager_ || player_->IsDodging() ||
				playerManager_->IsInvincible()) {
				continue;
			}
			const Vector3 playerPosition = player_->GetWorldPosition();
			const int32_t beamCount = event.type == BossAttackType::TripleBeam ? 3 : 1;
			for (int32_t index = 0; index < beamCount; ++index) {
				const float angleOffset = beamCount == 3
					? (static_cast<float>(index) - 1.0f) * 0.38f
					: 0.0f;
				const Vector3 direction = RotateDirectionXZ(event.direction, angleOffset);
				if (IsPlayerInBeam(playerPosition, event.position, direction, beamLength, 2.2f)) {
					playerManager_->TakeDamage(bossDamage);
					recentExplosionEffectPositions_.push_back(playerPosition);
					break;
				}
			}
		} else if (event.type == BossAttackType::LeapShockwave ||
			event.type == BossAttackType::SummonLeapShockwave) {
			const float radius = event.type == BossAttackType::SummonLeapShockwave ? 155.0f : 135.0f;
			spawnShockwaveImpact(event.targetPosition, 4);
			auto ring = std::make_unique<BossShockwaveRing>();
			ring->Initialize(
				event.targetPosition,
				radius,
				0.0f,
				{ 1.0f, 0.36f, 0.04f, 0.9f });
			bossShockwaveRings_.push_back(std::move(ring));
			addShockwaveDamage(event.targetPosition, radius, 0.0f, bossDamage);
			if (event.type == BossAttackType::SummonLeapShockwave) {
				for (int32_t index = 0; index < 4; ++index) {
					const float angle = 2.0f * std::numbers::pi_v<float> *
						static_cast<float>(index) / 4.0f + 0.785f;
					Vector3 summonPosition = event.targetPosition;
					summonPosition.x += std::sin(angle) * kBossSummonCrystalOffset;
					summonPosition.z += std::cos(angle) * kBossSummonCrystalOffset;
					auto crystal = std::make_unique<BossSummonCrystal>();
					crystal->Initialize(summonPosition, 0.0f);
					bossSummonCrystals_.push_back(std::move(crystal));
					spawnShockwaveImpact(summonPosition, 2);
					auto summonRing = std::make_unique<BossShockwaveRing>();
					summonRing->Initialize(
						summonPosition,
						120.0f,
						0.45f,
						{ 1.0f, 0.55f, 0.05f, 0.75f });
					bossShockwaveRings_.push_back(std::move(summonRing));
					addShockwaveDamage(summonPosition, 120.0f, 0.45f, bossDamage * 2 / 3);
				}
			}
		} else if (event.type == BossAttackType::BulletHell) {
			bossInkWavePosition_ = event.position;
			SpawnBossInkWave(bossInkWavePosition_, 0);
			pendingBossInkWaves_ = 5;
			bossInkNextWaveIndex_ = 1;
			bossInkWaveTimer_ = 0.24f;
		} else if (event.type == BossAttackType::ConvergingShockwave) {
			const Vector3 center = player_ ? player_->GetWorldPosition() : event.targetPosition;
			auto ring = std::make_unique<BossShockwaveRing>();
			ring->Initialize(
				center,
				24.0f,
				0.0f,
				{ 0.82f, 0.08f, 1.0f, 0.85f });
			bossShockwaveRings_.push_back(std::move(ring));
			for (int32_t index = 0; index < 24; ++index) {
				const float angle = 2.0f * std::numbers::pi_v<float> *
					static_cast<float>(index) / 24.0f;
				const Vector3 start{
					center.x + std::sin(angle) * 23.5f,
					0.0f,
					center.z + std::cos(angle) * 23.5f,
				};
				auto projectile = std::make_unique<BossInkProjectile>();
				projectile->Initialize(start, { center.x - start.x, 0.0f, center.z - start.z });
				bossInkProjectiles_.push_back(std::move(projectile));
			}
		} else if (event.type == BossAttackType::DomeBurst) {
			auto domeRing = std::make_unique<BossShockwaveRing>();
			domeRing->Initialize(
				event.position,
				38.0f,
				0.0f,
				{ 1.0f, 0.08f, 0.16f, 0.9f });
			bossShockwaveRings_.push_back(std::move(domeRing));
			damagePlayerIfInRadius(event.position, 38.0f, bossDamage + bossDamage / 2);
			recentExplosionEffectPositions_.push_back(event.position);
		}
	}
}

void EnemyManager::UpdateBossInkProjectiles(float deltaTime)
{
	if (pendingBossInkWaves_ > 0) {
		bossInkWaveTimer_ -= (std::max)(0.0f, deltaTime);
		while (pendingBossInkWaves_ > 0 && bossInkWaveTimer_ <= 0.0f) {
			SpawnBossInkWave(bossInkWavePosition_, bossInkNextWaveIndex_);
			++bossInkNextWaveIndex_;
			--pendingBossInkWaves_;
			bossInkWaveTimer_ += 0.5f;
		}
	}
	for (auto it = bossInkProjectiles_.begin();
		it != bossInkProjectiles_.end();) {
		BossInkProjectile* projectile = it->get();
		if (!projectile) {
			it = bossInkProjectiles_.erase(it);
			continue;
		}
		projectile->Update(deltaTime);
		if (projectile->IsActive() && player_ && playerManager_) {
			const Vector3 offset =
				player_->GetWorldPosition() - projectile->GetPosition();
			const float radius =
				player_->GetCollisionRadius() + projectile->GetCollisionRadius();
			if (offset.x * offset.x + offset.z * offset.z <= radius * radius) {
				if (!player_->IsDodging() && !playerManager_->IsInvincible()) {
					const int32_t damage = bossEnemy_
						? (std::max)(1, bossEnemy_->GetAttackPower() * 2 / 3)
						: 20;
					playerManager_->TakeDamage(damage);
				}
				projectile->Deactivate();
			}
		}
		if (!projectile->IsActive()) {
			it = bossInkProjectiles_.erase(it);
		} else {
			++it;
		}
	}
}

void EnemyManager::SpawnBossInkWave(
	const Vector3& position,
	int32_t waveIndex)
{
	constexpr int32_t kProjectileCount = 18;
	const float angleOffset =
		static_cast<float>(waveIndex) * std::numbers::pi_v<float> / 18.0f;
	for (int32_t index = 0; index < kProjectileCount; ++index) {
		const float angle = angleOffset +
			2.0f * std::numbers::pi_v<float> *
			static_cast<float>(index) /
			static_cast<float>(kProjectileCount);
		auto projectile = std::make_unique<BossInkProjectile>();
		projectile->Initialize(
			position,
			{ std::sin(angle), 0.0f, std::cos(angle) });
		bossInkProjectiles_.push_back(std::move(projectile));
	}
}

void EnemyManager::UpdateBossAttackVisuals(float deltaTime)
{
	const BossAttackTelegraph emptyTelegraph{};
	bossRushTelegraph_.Update(
		bossEnemy_ && bossEnemy_->IsActive()
			? bossEnemy_->GetBossAttackTelegraph()
			: emptyTelegraph);
	bossAreaTelegraph_.Update(
		bossEnemy_ && bossEnemy_->IsActive()
			? bossEnemy_->GetBossAttackTelegraph()
			: emptyTelegraph);
	for (auto it = bossBeamBursts_.begin(); it != bossBeamBursts_.end();) {
		if (!*it || !(*it)->Update(deltaTime)) {
			it = bossBeamBursts_.erase(it);
		} else {
			++it;
		}
	}
	for (auto it = bossShockwaveRings_.begin(); it != bossShockwaveRings_.end();) {
		if (!*it || !(*it)->Update(deltaTime)) {
			it = bossShockwaveRings_.erase(it);
		} else {
			++it;
		}
	}
	for (auto it = bossShockwaveDamageWaves_.begin(); it != bossShockwaveDamageWaves_.end();) {
		ActiveBossShockwaveDamage& wave = *it;
		wave.elapsedTime += (std::max)(0.0f, deltaTime);
		constexpr float kWaveDuration = 2.4f;
		if (wave.elapsedTime < wave.delay) {
			++it;
			continue;
		}
		const float localTime = wave.elapsedTime - wave.delay;
		if (localTime >= kWaveDuration || wave.hitPlayer) {
			it = bossShockwaveDamageWaves_.erase(it);
			continue;
		}
		if (player_ && playerManager_ && !player_->IsDodging() &&
			!playerManager_->IsInvincible()) {
			const Vector3 playerPosition = player_->GetWorldPosition();
			const float dx = playerPosition.x - wave.center.x;
			const float dz = playerPosition.z - wave.center.z;
			const float distance = std::sqrt(dx * dx + dz * dz);
			const float currentRadius =
				wave.radius * std::clamp(localTime / kWaveDuration, 0.0f, 1.0f);
			const float halfWidth = 4.5f + player_->GetCollisionRadius();
			if (std::abs(distance - currentRadius) <= halfWidth) {
				playerManager_->TakeDamage(wave.damage);
				recentExplosionEffectPositions_.push_back(playerPosition);
				wave.hitPlayer = true;
			}
		}
		++it;
	}
	for (auto it = bossSummonCrystals_.begin(); it != bossSummonCrystals_.end();) {
		if (!*it || !(*it)->Update(deltaTime)) {
			it = bossSummonCrystals_.erase(it);
		} else {
			++it;
		}
	}
	for (auto it = bossSlamCubes_.begin(); it != bossSlamCubes_.end();) {
		if (!*it || !(*it)->Update(deltaTime)) {
			it = bossSlamCubes_.erase(it);
		} else {
			++it;
		}
	}
}

void EnemyManager::DrawBossAttackTelegraph() const
{
}

void EnemyManager::RemoveInactiveEnemies()
{
	enemies_.erase(
		std::remove_if(enemies_.begin(), enemies_.end(), [](const std::unique_ptr<Enemy>& enemy) {
			return enemy && !enemy->IsActive() && !enemy->JustDied() && !enemy->IsDeathPresentationActive();
		}),
		enemies_.end());
}

void EnemyManager::UpdateExpOrbs(float deltaTime)
{
	if (!player_) {
		return;
	}

	const Vector3 playerPosition = player_->GetWorldPosition();
	const float expPickupRangeMultiplier = playerManager_
		? playerManager_->GetExpPickupRangeMultiplier()
		: 1.0f;
	for (auto it = expOrbs_.begin(); it != expOrbs_.end();) {
		(*it)->Update(
			playerPosition,
			deltaTime,
			expPickupRangeMultiplier);
		if (!(*it)->IsActive()) {
			if (playerManager_) {
				const int32_t gainedExp =
					playerManager_->AddEXP((*it)->GetEXP());
				recentFloatingNumberEvents_.push_back({
					{ playerPosition.x, playerPosition.y + 1.0f, playerPosition.z },
					gainedExp,
					{ 0.35f, 1.0f, 0.58f, 1.0f },
					});
			}
			it = expOrbs_.erase(it);
		} else {
			++it;
		}
	}
}

void EnemyManager::SpawnDeathDrop(const Enemy& enemy)
{
	++totalKillCount_;
	if (session_) {
		const int32_t gainedCoins = (std::max)(1, static_cast<int32_t>(
			std::lround(static_cast<float>(enemy.GetCoinValue()) *
				(playerManager_ ? playerManager_->GetCoinGainMultiplier() : 1.0f))));
		session_->AddRunCoins(gainedCoins);
		static SoundHandle coinGainSeHandle =
			GameAudioCache::LoadWave(kCoinGainSePath);
		GameAudioCache::PlayTuned(coinGainSeHandle, kAudioCoinGain, 0.36f, 0.035f);
		recentFloatingNumberEvents_.push_back({
			{ enemy.GetPosition().x + 1.15f, enemy.GetPosition().y + 2.85f, enemy.GetPosition().z },
			gainedCoins,
			{ 1.0f, 0.86f, 0.22f, 1.0f },
			});
	}
	recentDeathEffectPositions_.push_back(enemy.GetPosition());
	auto orb = std::make_unique<ExpOrb>();
	orb->Initialize(enemy.GetPosition(), enemy.GetEXP());
	expOrbs_.push_back(std::move(orb));
	peakExpOrbCount_ = (std::max)(peakExpOrbCount_, expOrbs_.size());
	while (expOrbs_.size() > kMaxExpOrbs) {
		expOrbs_.pop_front();
		++expOrbPruneCount_;
	}
}

} // namespace DirectXGame
