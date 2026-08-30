#include "EnemyManager.h"
#include "DataPaths.h"
#include "GameAudioCache.h"
#include "GameSession.h"
#include "Player.h"
#include "PlayerManager.h"
#include "ProductionTuning.h"
#include <algorithm>
#include <charconv>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <numbers>
#include <string_view>

#ifdef _DEBUG
#include <imgui.h>
#endif

namespace DirectXGame {
namespace {

constexpr char kCoinGainSePath[] = "se/coin_gain.wav";
constexpr char kPlayerDamageSePath[] = "se/player_damage.wav";
constexpr char kAudioCoinGain[] = "combat.coinGain";
constexpr char kAudioPlayerDamage[] = "combat.playerDamage";
constexpr float kEnemyShadowLodDistanceSq = 42.0f * 42.0f;
constexpr float kEnemyAnimationLodNearDistanceSq = 28.0f * 28.0f;
constexpr float kEnemyAnimationLodMidDistanceSq = 52.0f * 52.0f;
constexpr float kEnemySimplifiedNearDistanceSq = 38.0f * 38.0f;
constexpr float kEnemySimplifiedFarDistanceSq = 62.0f * 62.0f;
constexpr size_t kEnemyFullModelBudget = 128;
constexpr float kNormalEnemyDeathPresentationDuration = 0.52f;
constexpr float kBossSummonCrystalOffset = 28.0f;

BossAttackType BossAttackTypeFromIndex(int32_t index)
{
	switch (index) {
	case 1: return BossAttackType::Beam;
	case 2: return BossAttackType::TripleBeam;
	case 3: return BossAttackType::LeapShockwave;
	case 4: return BossAttackType::SummonLeapShockwave;
	case 5: return BossAttackType::BulletHell;
	case 6: return BossAttackType::ConvergingShockwave;
	case 7: return BossAttackType::DomeBurst;
	default: return BossAttackType::None;
	}
}

void PlayPlayerDamageSound()
{
	static SoundHandle sharedPlayerDamageSeHandle{};
	if (!sharedPlayerDamageSeHandle) {
		sharedPlayerDamageSeHandle = GameAudioCache::LoadWave(kPlayerDamageSePath);
	}
	GameAudioCache::PlayTuned(
		sharedPlayerDamageSeHandle,
		kAudioPlayerDamage,
		0.76f,
		0.12f);
}

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
	if (enemy.GetCollisionRadius() >= 1.9f) {
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

bool IsPlayerVulnerableToGroundBossExplosion(const Player& player, float jumpSafeHeight)
{
	if (player.IsDodging()) {
		return false;
	}
	const Vector3& position = player.GetWorldPosition();
	return position.y < jumpSafeHeight;
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
	bossPresentationPositionOverrideActive_ = false;
	bossPresentationPositionOverride_ = {};
	bossBeamBursts_.clear();
	bossBeamExplosionChains_.clear();
	bossBeamExplosionDamages_.clear();
	bossShockwaveRings_.clear();
	bossDomeBurstVisuals_.clear();
	bossSummonCrystals_.clear();
	bossShockwaveDamageWaves_.clear();
	bossSlamCubes_.clear();
	pendingBossSummonProjectileBursts_.clear();
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
	ReloadBossAttackTuning();
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
		bossBeamBursts_.size() * 512u +
		bossBeamExplosionChains_.size() * 4096u +
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
	for (const std::unique_ptr<BossBeamExplosionChain>& chain :
		bossBeamExplosionChains_) {
		if (chain) {
			chain->AppendRenderObjects(staticObjects);
		}
	}
	for (const std::unique_ptr<BossShockwaveRing>& ring : bossShockwaveRings_) {
		if (ring) {
			ring->AppendRenderObjects(staticObjects);
		}
	}
	for (const std::unique_ptr<BossDomeBurstVisual>& dome : bossDomeBurstVisuals_) {
		if (dome) {
			dome->AppendRenderObjects(staticObjects);
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
	std::vector<Engine::Graphics3D::Object3D*> bossAttackShadowObjects;
	bossAttackShadowObjects.reserve(8u);
	bossAreaTelegraph_.AppendShadowObjects(bossAttackShadowObjects);
	for (const std::unique_ptr<BossSummonCrystal>& crystal : bossSummonCrystals_) {
		if (!crystal) {
			continue;
		}
		if (Engine::Graphics3D::Object3D* object = crystal->GetRenderObject()) {
			bossAttackShadowObjects.push_back(object);
		}
	}
	for (Engine::Graphics3D::Object3D* object : bossAttackShadowObjects) {
		Engine::Graphics3D::Object3D::SubmitForShadow(object);
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

void EnemyManager::ReloadBossAttackTuning()
{
	const ProductionTuning::NumberMap values =
		ProductionTuning::LoadOrCreate(DataPaths::kProductionTuning);
	bossAttackTuning_.beamExtraLength =
		ProductionTuning::GetFloat(values, "boss.beam.extraLength", bossAttackTuning_.beamExtraLength);
	bossAttackTuning_.beamMinLength =
		ProductionTuning::GetFloat(values, "boss.beam.minLength", bossAttackTuning_.beamMinLength);
	bossAttackTuning_.beamWidth =
		ProductionTuning::GetFloat(values, "boss.beam.width", bossAttackTuning_.beamWidth);
	bossAttackTuning_.tripleBeamAngleOffset =
		ProductionTuning::GetFloat(values, "boss.beam.tripleAngleOffset", bossAttackTuning_.tripleBeamAngleOffset);
	bossAttackTuning_.beamExplosionSpacing =
		ProductionTuning::GetFloat(values, "boss.beam.explosionSpacing", bossAttackTuning_.beamExplosionSpacing);
	bossAttackTuning_.beamExplosionInterval =
		ProductionTuning::GetFloat(values, "boss.beam.explosionInterval", bossAttackTuning_.beamExplosionInterval);
	bossAttackTuning_.beamExplosionRadius =
		ProductionTuning::GetFloat(values, "boss.beam.explosionRadius", bossAttackTuning_.beamExplosionRadius);
	bossAttackTuning_.beamExplosionStartDelay =
		ProductionTuning::GetFloat(values, "boss.beam.explosionStartDelay", bossAttackTuning_.beamExplosionStartDelay);
	bossAttackTuning_.beamJumpSafeHeight =
		ProductionTuning::GetFloat(values, "boss.beam.jumpSafeHeight", bossAttackTuning_.beamJumpSafeHeight);
	bossAttackTuning_.shockwaveJumpSafeHeight =
		ProductionTuning::GetFloat(values, "boss.shockwave.jumpSafeHeight", bossAttackTuning_.shockwaveJumpSafeHeight);
	bossAttackTuning_.shockwaveRadius =
		ProductionTuning::GetFloat(values, "boss.shockwave.radius", bossAttackTuning_.shockwaveRadius);
	bossAttackTuning_.shockwaveDuration =
		ProductionTuning::GetFloat(values, "boss.shockwave.duration", bossAttackTuning_.shockwaveDuration);
	bossAttackTuning_.summonShockwaveRadius =
		ProductionTuning::GetFloat(values, "boss.summonShockwave.radius", bossAttackTuning_.summonShockwaveRadius);
	bossAttackTuning_.summonSubRadius =
		ProductionTuning::GetFloat(values, "boss.summonShockwave.subRadius", bossAttackTuning_.summonSubRadius);
	bossAttackTuning_.summonSubDelay =
		ProductionTuning::GetFloat(values, "boss.summonShockwave.subDelay", bossAttackTuning_.summonSubDelay);
	bossAttackTuning_.summonSubDamageScale =
		ProductionTuning::GetFloat(values, "boss.summonShockwave.subDamageScale", bossAttackTuning_.summonSubDamageScale);
	bossAttackTuning_.bulletHellProjectileSpeed =
		ProductionTuning::GetFloat(values, "boss.bulletHell.projectileSpeed", bossAttackTuning_.bulletHellProjectileSpeed);
	bossAttackTuning_.bulletHellProjectileLifetime =
		ProductionTuning::GetFloat(values, "boss.bulletHell.projectileLifetime", bossAttackTuning_.bulletHellProjectileLifetime);
	bossAttackTuning_.bulletHellProjectileRadius =
		ProductionTuning::GetFloat(values, "boss.bulletHell.projectileRadius", bossAttackTuning_.bulletHellProjectileRadius);
	bossAttackTuning_.bulletHellProjectileCount =
		ProductionTuning::GetInt(values, "boss.bulletHell.projectileCount", bossAttackTuning_.bulletHellProjectileCount);
	bossAttackTuning_.bulletHellExtraWaveCount =
		ProductionTuning::GetInt(values, "boss.bulletHell.extraWaveCount", bossAttackTuning_.bulletHellExtraWaveCount);
	bossAttackTuning_.bulletHellFirstDelay =
		ProductionTuning::GetFloat(values, "boss.bulletHell.firstDelay", bossAttackTuning_.bulletHellFirstDelay);
	bossAttackTuning_.bulletHellWaveInterval =
		ProductionTuning::GetFloat(values, "boss.bulletHell.waveInterval", bossAttackTuning_.bulletHellWaveInterval);
	bossAttackTuning_.bulletHellAngleStepDivisor =
		ProductionTuning::GetFloat(values, "boss.bulletHell.angleStepDivisor", bossAttackTuning_.bulletHellAngleStepDivisor);
	bossAttackTuning_.convergingProjectileCount =
		ProductionTuning::GetInt(values, "boss.converging.projectileCount", bossAttackTuning_.convergingProjectileCount);
	bossAttackTuning_.convergingSpawnRadius =
		ProductionTuning::GetFloat(values, "boss.converging.spawnRadius", bossAttackTuning_.convergingSpawnRadius);
	bossAttackTuning_.convergingRingRadius =
		ProductionTuning::GetFloat(values, "boss.converging.ringRadius", bossAttackTuning_.convergingRingRadius);
	bossAttackTuning_.domeBurstRadius =
		ProductionTuning::GetFloat(values, "boss.domeBurst.radius", bossAttackTuning_.domeBurstRadius);
	bossAttackTuning_.domeBurstDamageScale =
		ProductionTuning::GetFloat(values, "boss.domeBurst.damageScale", bossAttackTuning_.domeBurstDamageScale);

	bossAttackTuning_.beamMinLength = std::clamp(bossAttackTuning_.beamMinLength, 1.0f, 260.0f);
	bossAttackTuning_.beamExtraLength = std::clamp(bossAttackTuning_.beamExtraLength, 0.0f, 260.0f);
	bossAttackTuning_.beamWidth = std::clamp(bossAttackTuning_.beamWidth, 0.2f, 16.0f);
	bossAttackTuning_.tripleBeamAngleOffset = std::clamp(bossAttackTuning_.tripleBeamAngleOffset, 0.0f, 1.6f);
	bossAttackTuning_.beamExplosionSpacing = std::clamp(bossAttackTuning_.beamExplosionSpacing, 2.0f, 32.0f);
	bossAttackTuning_.beamExplosionInterval = std::clamp(bossAttackTuning_.beamExplosionInterval, 0.02f, 1.0f);
	bossAttackTuning_.beamExplosionRadius = std::clamp(bossAttackTuning_.beamExplosionRadius, 0.5f, 12.0f);
	bossAttackTuning_.beamExplosionStartDelay = std::clamp(bossAttackTuning_.beamExplosionStartDelay, 0.0f, 2.0f);
	bossAttackTuning_.beamJumpSafeHeight = std::clamp(bossAttackTuning_.beamJumpSafeHeight, 0.0f, 4.0f);
	bossAttackTuning_.shockwaveJumpSafeHeight = std::clamp(bossAttackTuning_.shockwaveJumpSafeHeight, 0.0f, 4.0f);
	bossAttackTuning_.shockwaveRadius = std::clamp(bossAttackTuning_.shockwaveRadius, 1.0f, 260.0f);
	bossAttackTuning_.shockwaveDuration = std::clamp(bossAttackTuning_.shockwaveDuration, 0.2f, 8.0f);
	bossAttackTuning_.summonShockwaveRadius = std::clamp(bossAttackTuning_.summonShockwaveRadius, 1.0f, 280.0f);
	bossAttackTuning_.summonSubRadius = std::clamp(bossAttackTuning_.summonSubRadius, 1.0f, 260.0f);
	bossAttackTuning_.summonSubDelay = std::clamp(bossAttackTuning_.summonSubDelay, 0.0f, 5.0f);
	bossAttackTuning_.summonSubDamageScale = std::clamp(bossAttackTuning_.summonSubDamageScale, 0.0f, 4.0f);
	bossAttackTuning_.bulletHellProjectileSpeed = std::clamp(bossAttackTuning_.bulletHellProjectileSpeed, 0.1f, 80.0f);
	bossAttackTuning_.bulletHellProjectileLifetime = std::clamp(bossAttackTuning_.bulletHellProjectileLifetime, 0.1f, 20.0f);
	bossAttackTuning_.bulletHellProjectileRadius = std::clamp(bossAttackTuning_.bulletHellProjectileRadius, 0.1f, 12.0f);
	bossAttackTuning_.bulletHellProjectileCount = std::clamp(bossAttackTuning_.bulletHellProjectileCount, 1, 96);
	bossAttackTuning_.bulletHellExtraWaveCount = std::clamp(bossAttackTuning_.bulletHellExtraWaveCount, 0, 24);
	bossAttackTuning_.bulletHellFirstDelay = std::clamp(bossAttackTuning_.bulletHellFirstDelay, 0.0f, 5.0f);
	bossAttackTuning_.bulletHellWaveInterval = std::clamp(bossAttackTuning_.bulletHellWaveInterval, 0.02f, 5.0f);
	bossAttackTuning_.bulletHellAngleStepDivisor = std::clamp(bossAttackTuning_.bulletHellAngleStepDivisor, 1.0f, 96.0f);
	bossAttackTuning_.convergingProjectileCount = std::clamp(bossAttackTuning_.convergingProjectileCount, 1, 128);
	bossAttackTuning_.convergingSpawnRadius = std::clamp(bossAttackTuning_.convergingSpawnRadius, 1.0f, 160.0f);
	bossAttackTuning_.convergingRingRadius = std::clamp(bossAttackTuning_.convergingRingRadius, 1.0f, 160.0f);
	bossAttackTuning_.domeBurstRadius = std::clamp(bossAttackTuning_.domeBurstRadius, 1.0f, 260.0f);
	bossAttackTuning_.domeBurstDamageScale = std::clamp(bossAttackTuning_.domeBurstDamageScale, 0.0f, 6.0f);
}

#ifdef _DEBUG
void EnemyManager::SaveBossAttackTuning() const
{
	ProductionTuning::NumberMap values =
		ProductionTuning::LoadOrCreate(DataPaths::kProductionTuning);
	ProductionTuning::SetFloat(values, "boss.beam.extraLength", bossAttackTuning_.beamExtraLength);
	ProductionTuning::SetFloat(values, "boss.beam.minLength", bossAttackTuning_.beamMinLength);
	ProductionTuning::SetFloat(values, "boss.beam.width", bossAttackTuning_.beamWidth);
	ProductionTuning::SetFloat(values, "boss.beam.tripleAngleOffset", bossAttackTuning_.tripleBeamAngleOffset);
	ProductionTuning::SetFloat(values, "boss.beam.explosionSpacing", bossAttackTuning_.beamExplosionSpacing);
	ProductionTuning::SetFloat(values, "boss.beam.explosionInterval", bossAttackTuning_.beamExplosionInterval);
	ProductionTuning::SetFloat(values, "boss.beam.explosionRadius", bossAttackTuning_.beamExplosionRadius);
	ProductionTuning::SetFloat(values, "boss.beam.explosionStartDelay", bossAttackTuning_.beamExplosionStartDelay);
	ProductionTuning::SetFloat(values, "boss.beam.jumpSafeHeight", bossAttackTuning_.beamJumpSafeHeight);
	ProductionTuning::SetFloat(values, "boss.shockwave.jumpSafeHeight", bossAttackTuning_.shockwaveJumpSafeHeight);
	ProductionTuning::SetFloat(values, "boss.shockwave.radius", bossAttackTuning_.shockwaveRadius);
	ProductionTuning::SetFloat(values, "boss.shockwave.duration", bossAttackTuning_.shockwaveDuration);
	ProductionTuning::SetFloat(values, "boss.summonShockwave.radius", bossAttackTuning_.summonShockwaveRadius);
	ProductionTuning::SetFloat(values, "boss.summonShockwave.subRadius", bossAttackTuning_.summonSubRadius);
	ProductionTuning::SetFloat(values, "boss.summonShockwave.subDelay", bossAttackTuning_.summonSubDelay);
	ProductionTuning::SetFloat(values, "boss.summonShockwave.subDamageScale", bossAttackTuning_.summonSubDamageScale);
	ProductionTuning::SetFloat(values, "boss.bulletHell.projectileSpeed", bossAttackTuning_.bulletHellProjectileSpeed);
	ProductionTuning::SetFloat(values, "boss.bulletHell.projectileLifetime", bossAttackTuning_.bulletHellProjectileLifetime);
	ProductionTuning::SetFloat(values, "boss.bulletHell.projectileRadius", bossAttackTuning_.bulletHellProjectileRadius);
	ProductionTuning::SetInt(values, "boss.bulletHell.projectileCount", bossAttackTuning_.bulletHellProjectileCount);
	ProductionTuning::SetInt(values, "boss.bulletHell.extraWaveCount", bossAttackTuning_.bulletHellExtraWaveCount);
	ProductionTuning::SetFloat(values, "boss.bulletHell.firstDelay", bossAttackTuning_.bulletHellFirstDelay);
	ProductionTuning::SetFloat(values, "boss.bulletHell.waveInterval", bossAttackTuning_.bulletHellWaveInterval);
	ProductionTuning::SetFloat(values, "boss.bulletHell.angleStepDivisor", bossAttackTuning_.bulletHellAngleStepDivisor);
	ProductionTuning::SetInt(values, "boss.converging.projectileCount", bossAttackTuning_.convergingProjectileCount);
	ProductionTuning::SetFloat(values, "boss.converging.spawnRadius", bossAttackTuning_.convergingSpawnRadius);
	ProductionTuning::SetFloat(values, "boss.converging.ringRadius", bossAttackTuning_.convergingRingRadius);
	ProductionTuning::SetFloat(values, "boss.domeBurst.radius", bossAttackTuning_.domeBurstRadius);
	ProductionTuning::SetFloat(values, "boss.domeBurst.damageScale", bossAttackTuning_.domeBurstDamageScale);
	ProductionTuning::Save(DataPaths::kProductionTuning, values);
}

void EnemyManager::QueueDebugBossAttack(BossAttackType type)
{
	if (type == BossAttackType::None || !player_) {
		return;
	}
	if (!HasActiveBoss()) {
		StartBossPhase();
	}
	if (!bossEnemy_) {
		return;
	}
	const Vector3 bossPosition = bossEnemy_->GetPosition();
	const Vector3 playerPosition = player_->GetWorldPosition();
	Vector3 direction{
		playerPosition.x - bossPosition.x,
		0.0f,
		playerPosition.z - bossPosition.z,
	};
	const float length =
		std::sqrt(direction.x * direction.x + direction.z * direction.z);
	if (length > 0.001f) {
		direction.x /= length;
		direction.z /= length;
	} else {
		direction = { 0.0f, 0.0f, 1.0f };
	}
	bossEnemy_->QueueBossAttack(type, direction, playerPosition);
}

void EnemyManager::DrawBossAttackTuningDebugUI()
{
	static int selectedAttack = 1;
	const char* labels[] = {
		"None",
		"Beam",
		"Triple Beam",
		"Shockwave",
		"Enhanced Shockwave",
		"Bullet Hell",
		"Converging Bullets",
		"Dome Burst",
	};
	selectedAttack = std::clamp(selectedAttack, 1, 7);
	if (ImGui::CollapsingHeader("Boss Attack Tuning", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Combo("Preview Attack", &selectedAttack, labels, IM_ARRAYSIZE(labels));
		if (ImGui::Button("Preview Selected Boss Attack")) {
			QueueDebugBossAttack(BossAttackTypeFromIndex(selectedAttack));
		}
		ImGui::SameLine();
		if (ImGui::Button("Reload Boss JSON")) {
			ReloadBossAttackTuning();
		}
		ImGui::SameLine();
		if (ImGui::Button("Save Boss JSON")) {
			SaveBossAttackTuning();
		}

		ImGui::SeparatorText("Beam");
		ImGui::DragFloat("Beam Min Length", &bossAttackTuning_.beamMinLength, 0.5f, 1.0f, 260.0f);
		ImGui::DragFloat("Beam Extra Length", &bossAttackTuning_.beamExtraLength, 0.5f, 0.0f, 260.0f);
		ImGui::DragFloat("Beam Width", &bossAttackTuning_.beamWidth, 0.05f, 0.2f, 16.0f);
		ImGui::DragFloat("Triple Beam Angle", &bossAttackTuning_.tripleBeamAngleOffset, 0.01f, 0.0f, 1.6f);
		ImGui::DragFloat("Beam Explosion Spacing", &bossAttackTuning_.beamExplosionSpacing, 0.1f, 2.0f, 32.0f);
		ImGui::DragFloat("Beam Explosion Interval", &bossAttackTuning_.beamExplosionInterval, 0.01f, 0.02f, 1.0f);
		ImGui::DragFloat("Beam Explosion Radius", &bossAttackTuning_.beamExplosionRadius, 0.05f, 0.5f, 12.0f);
		ImGui::DragFloat("Beam Explosion Start Delay", &bossAttackTuning_.beamExplosionStartDelay, 0.01f, 0.0f, 2.0f);
		ImGui::DragFloat("Beam Jump Safe Height", &bossAttackTuning_.beamJumpSafeHeight, 0.05f, 0.0f, 4.0f);

		ImGui::SeparatorText("Shockwave");
		ImGui::DragFloat("Shockwave Jump Safe Height", &bossAttackTuning_.shockwaveJumpSafeHeight, 0.05f, 0.0f, 4.0f);
		ImGui::DragFloat("Shockwave Radius", &bossAttackTuning_.shockwaveRadius, 1.0f, 1.0f, 260.0f);
		ImGui::DragFloat("Shockwave Duration", &bossAttackTuning_.shockwaveDuration, 0.05f, 0.2f, 8.0f);
		ImGui::DragFloat("Enhanced Radius", &bossAttackTuning_.summonShockwaveRadius, 1.0f, 1.0f, 280.0f);
		ImGui::DragFloat("Enhanced Sub Radius", &bossAttackTuning_.summonSubRadius, 1.0f, 1.0f, 260.0f);
		ImGui::DragFloat("Enhanced Sub Delay", &bossAttackTuning_.summonSubDelay, 0.02f, 0.0f, 5.0f);
		ImGui::DragFloat("Enhanced Sub Damage Scale", &bossAttackTuning_.summonSubDamageScale, 0.02f, 0.0f, 4.0f);

		ImGui::SeparatorText("Bullets");
		ImGui::SliderInt("Bullet Hell Count", &bossAttackTuning_.bulletHellProjectileCount, 1, 96);
		ImGui::SliderInt("Bullet Hell Extra Waves", &bossAttackTuning_.bulletHellExtraWaveCount, 0, 24);
		ImGui::DragFloat("Bullet Speed", &bossAttackTuning_.bulletHellProjectileSpeed, 0.2f, 0.1f, 80.0f);
		ImGui::DragFloat("Bullet Lifetime", &bossAttackTuning_.bulletHellProjectileLifetime, 0.05f, 0.1f, 20.0f);
		ImGui::DragFloat("Bullet Radius", &bossAttackTuning_.bulletHellProjectileRadius, 0.05f, 0.1f, 12.0f);
		ImGui::DragFloat("First Wave Delay", &bossAttackTuning_.bulletHellFirstDelay, 0.02f, 0.0f, 5.0f);
		ImGui::DragFloat("Wave Interval", &bossAttackTuning_.bulletHellWaveInterval, 0.02f, 0.02f, 5.0f);
		ImGui::DragFloat("Wave Angle Divisor", &bossAttackTuning_.bulletHellAngleStepDivisor, 0.5f, 1.0f, 96.0f);
		ImGui::SliderInt("Converging Count", &bossAttackTuning_.convergingProjectileCount, 1, 128);
		ImGui::DragFloat("Converging Spawn Radius", &bossAttackTuning_.convergingSpawnRadius, 0.5f, 1.0f, 160.0f);
		ImGui::DragFloat("Converging Ring Radius", &bossAttackTuning_.convergingRingRadius, 0.5f, 1.0f, 160.0f);

		ImGui::SeparatorText("Dome Burst");
		ImGui::DragFloat("Dome Radius", &bossAttackTuning_.domeBurstRadius, 1.0f, 1.0f, 260.0f);
		ImGui::DragFloat("Dome Damage Scale", &bossAttackTuning_.domeBurstDamageScale, 0.05f, 0.0f, 6.0f);
	}
}
#endif

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
	if (bossPresentationPositionOverrideActive_) {
		outPosition = bossPresentationPositionOverride_;
		return true;
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
		bossPresentationPositionOverrideActive_ = true;
		bossPresentationPositionOverride_ = position;
		bossEnemy_->ApplyPresentationPose(position, true);
	}
}

void EnemyManager::ClearBossPresentationPositionOverride()
{
	bossPresentationPositionOverrideActive_ = false;
	bossPresentationPositionOverride_ = {};
	if (bossEnemy_) {
		bossEnemy_->FinishSpawnPresentation();
		bossEnemy_->ApplyPresentationPose(bossEnemy_->GetPosition(), true);
		bossEnemy_->SetPresentationCullingEnabled(true);
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
				if (playerManager_->TakeDamage(damage)) {
					PlayPlayerDamageSound();
				}
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
				if (playerManager_->TakeDamage(damage)) {
					PlayPlayerDamageSound();
				}
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
		const auto addShockwaveDamage = [&](
			const Vector3& center,
			float radius,
			float delay,
			int32_t damage,
			float duration = 0.0f,
			bool inward = false) {
			bossShockwaveDamageWaves_.push_back({
				center,
				radius,
				delay,
				duration,
				0.0f,
				damage,
				inward,
				false,
			});
		};
		const auto addBeamExplosionDamage = [&](const Vector3& center, float delay, int32_t damage) {
			bossBeamExplosionDamages_.push_back({
				center,
				bossAttackTuning_.beamExplosionRadius,
				delay,
				0.0f,
				damage,
				false,
			});
		};

		if (event.type == BossAttackType::Beam ||
			event.type == BossAttackType::TripleBeam) {
			const Vector3 beamTarget = event.targetPosition;
			const Vector3 beamOffset = beamTarget - event.position;
			const float beamDistance = std::sqrt(
				beamOffset.x * beamOffset.x + beamOffset.z * beamOffset.z);
			const float beamLength = (std::max)(
				bossAttackTuning_.beamMinLength,
				beamDistance + bossAttackTuning_.beamExtraLength);
			auto beam = std::make_unique<BossBeamBurst>();
			beam->Initialize(
				event.position,
				event.direction,
				event.type == BossAttackType::TripleBeam ? 3 : 1,
				beamLength,
				bossAttackTuning_.beamWidth,
				bossAttackTuning_.tripleBeamAngleOffset);
			bossBeamBursts_.push_back(std::move(beam));
			const int32_t beamCount = event.type == BossAttackType::TripleBeam ? 3 : 1;
			auto explosionChain = std::make_unique<BossBeamExplosionChain>();
			explosionChain->Initialize(
				event.position,
				event.direction,
				beamCount,
				beamLength,
				bossAttackTuning_.beamExplosionSpacing,
				bossAttackTuning_.beamExplosionInterval,
				bossAttackTuning_.beamExplosionRadius,
				bossAttackTuning_.beamExplosionStartDelay,
				bossAttackTuning_.tripleBeamAngleOffset);
			bossBeamExplosionChains_.push_back(std::move(explosionChain));
			const size_t explosionCountPerBeam = (std::min)(
				size_t{ 24 },
				(std::max)(
					size_t{ 1 },
					static_cast<size_t>(beamLength /
						bossAttackTuning_.beamExplosionSpacing)));
			for (int32_t index = 0; index < beamCount; ++index) {
				const float angleOffset = beamCount == 3
					? (static_cast<float>(index) - 1.0f) *
						bossAttackTuning_.tripleBeamAngleOffset
					: 0.0f;
				const Vector3 direction = RotateDirectionXZ(event.direction, angleOffset);
				for (size_t explosionIndex = 0;
					explosionIndex < explosionCountPerBeam;
					++explosionIndex) {
					const float distance = (std::min)(
						beamLength,
						(static_cast<float>(explosionIndex) + 1.0f) *
							bossAttackTuning_.beamExplosionSpacing);
					const Vector3 center = event.position + direction * distance;
					addBeamExplosionDamage(
						center,
						bossAttackTuning_.beamExplosionStartDelay,
						bossDamage);
				}
			}
		} else if (event.type == BossAttackType::LeapShockwave ||
			event.type == BossAttackType::SummonLeapShockwave) {
			const float radius = event.type == BossAttackType::SummonLeapShockwave
				? bossAttackTuning_.summonShockwaveRadius
				: bossAttackTuning_.shockwaveRadius;
			auto landingBurst = std::make_unique<BossShockwaveRing>();
			landingBurst->Initialize(
				event.targetPosition,
				18.0f,
				0.0f,
				{ 0.82f, 0.82f, 0.76f, 0.62f },
				0.62f,
				1.25f,
				0.34f,
				false,
				42u);
			bossShockwaveRings_.push_back(std::move(landingBurst));
			auto landingSpark = std::make_unique<BossShockwaveRing>();
			landingSpark->Initialize(
				event.targetPosition,
				9.0f,
				0.04f,
				{ 0.96f, 0.96f, 0.90f, 0.54f },
				0.46f,
				0.95f,
				0.72f,
				false,
				28u);
			bossShockwaveRings_.push_back(std::move(landingSpark));
			auto ring = std::make_unique<BossShockwaveRing>();
			ring->Initialize(
				event.targetPosition,
				radius,
				0.0f,
				{ 1.0f, 1.0f, 1.0f, 0.92f },
				bossAttackTuning_.shockwaveDuration,
				0.48f,
				0.16f);
			bossShockwaveRings_.push_back(std::move(ring));
			addShockwaveDamage(event.targetPosition, radius, 0.0f, bossDamage);
			if (event.type == BossAttackType::SummonLeapShockwave) {
				for (int32_t index = 0; index < 4; ++index) {
					if (bossSummonCrystals_.empty()) {
						EnsureBossSummonCrystals(event.targetPosition);
					}
					const size_t crystalIndex = static_cast<size_t>(index);
					Vector3 summonPosition = crystalIndex < bossSummonCrystals_.size() &&
						bossSummonCrystals_[crystalIndex]
						? bossSummonCrystals_[crystalIndex]->GetPosition()
						: event.targetPosition;
					const float dx = summonPosition.x - event.targetPosition.x;
					const float dz = summonPosition.z - event.targetPosition.z;
					const float distanceFromImpact = std::sqrt(dx * dx + dz * dz);
					const float parentArrivalDelay =
						bossAttackTuning_.shockwaveDuration *
						std::clamp(distanceFromImpact / (std::max)(1.0f, radius), 0.0f, 1.0f);
					const float burstDelay =
						parentArrivalDelay + bossAttackTuning_.summonSubDelay;
					if (crystalIndex < bossSummonCrystals_.size() &&
						bossSummonCrystals_[crystalIndex]) {
						bossSummonCrystals_[crystalIndex]->ScheduleBurst(burstDelay);
					}
					auto summonRing = std::make_unique<BossShockwaveRing>();
					summonRing->Initialize(
						summonPosition,
						10.0f,
						burstDelay,
						{ 0.92f, 0.82f, 1.0f, 0.72f },
						0.34f,
						0.74f,
						0.72f,
						false,
						24u);
					bossShockwaveRings_.push_back(std::move(summonRing));
					pendingBossSummonProjectileBursts_.push_back({
						summonPosition,
						burstDelay,
						0.0f,
						index,
						});
				}
			}
		} else if (event.type == BossAttackType::BulletHell) {
			bossInkWavePosition_ = event.position;
			SpawnBossInkWave(bossInkWavePosition_, 0);
			pendingBossInkWaves_ = bossAttackTuning_.bulletHellExtraWaveCount;
			bossInkNextWaveIndex_ = 1;
			bossInkWaveTimer_ = bossAttackTuning_.bulletHellFirstDelay;
		} else if (event.type == BossAttackType::ConvergingShockwave) {
			const Vector3 center = player_ ? player_->GetWorldPosition() : event.targetPosition;
			const float duration = bossAttackTuning_.shockwaveDuration * 0.86f;
			auto ring = std::make_unique<BossShockwaveRing>();
			ring->Initialize(
				center,
				bossAttackTuning_.convergingRingRadius,
				0.0f,
				{ 1.0f, 1.0f, 1.0f, 0.92f },
				duration,
				0.42f,
				0.18f,
				false,
				96u,
				true);
			bossShockwaveRings_.push_back(std::move(ring));
			addShockwaveDamage(
				center,
				bossAttackTuning_.convergingRingRadius,
				0.0f,
				bossDamage,
				duration,
				true);
		} else if (event.type == BossAttackType::DomeBurst) {
			auto domeGroundFlash = std::make_unique<BossShockwaveRing>();
			domeGroundFlash->Initialize(
				event.position,
				bossAttackTuning_.domeBurstRadius,
				0.0f,
				{ 1.0f, 0.08f, 0.16f, 0.9f },
				0.9f,
				0.82f,
				0.24f,
				true,
				96u);
			bossShockwaveRings_.push_back(std::move(domeGroundFlash));
			auto domeBurst = std::make_unique<BossDomeBurstVisual>();
			domeBurst->Initialize(
				event.position,
				bossAttackTuning_.domeBurstRadius,
				1.05f,
				{ 1.0f, 0.08f, 0.16f, 0.95f });
			bossDomeBurstVisuals_.push_back(std::move(domeBurst));
			damagePlayerIfInRadius(
				event.position,
				bossAttackTuning_.domeBurstRadius,
				static_cast<int32_t>(
					std::lround(static_cast<float>(bossDamage) *
						bossAttackTuning_.domeBurstDamageScale)));
			recentExplosionEffectPositions_.push_back(event.position);
		}
	}
}

void EnemyManager::UpdateBossInkProjectiles(float deltaTime)
{
	for (auto it = pendingBossSummonProjectileBursts_.begin();
		it != pendingBossSummonProjectileBursts_.end();) {
		it->elapsedTime += (std::max)(0.0f, deltaTime);
		if (it->elapsedTime >= it->delay) {
			SpawnBossSummonProjectileBurst(it->center, it->burstIndex);
			it = pendingBossSummonProjectileBursts_.erase(it);
		} else {
			++it;
		}
	}
	if (pendingBossInkWaves_ > 0) {
		bossInkWaveTimer_ -= (std::max)(0.0f, deltaTime);
		while (pendingBossInkWaves_ > 0 && bossInkWaveTimer_ <= 0.0f) {
			SpawnBossInkWave(bossInkWavePosition_, bossInkNextWaveIndex_);
			++bossInkNextWaveIndex_;
			--pendingBossInkWaves_;
			bossInkWaveTimer_ += bossAttackTuning_.bulletHellWaveInterval;
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
					if (playerManager_->TakeDamage(damage)) {
						PlayPlayerDamageSound();
					}
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

void EnemyManager::SpawnBossSummonProjectileBurst(
	const Vector3& position,
	int32_t burstIndex)
{
	constexpr int32_t kProjectileCount = 10;
	const float angleOffset =
		static_cast<float>(burstIndex) * std::numbers::pi_v<float> /
		static_cast<float>(kProjectileCount);
	for (int32_t index = 0; index < kProjectileCount; ++index) {
		const float angle = angleOffset +
			2.0f * std::numbers::pi_v<float> *
			static_cast<float>(index) /
			static_cast<float>(kProjectileCount);
		auto projectile = std::make_unique<BossInkProjectile>();
		projectile->Initialize(
			position,
			{ std::sin(angle), 0.0f, std::cos(angle) },
			bossAttackTuning_.bulletHellProjectileSpeed * 0.78f,
			3.6f,
			0.92f,
			{ 0.74f, 0.34f, 1.0f, 0.98f },
			"cube.obj");
		bossInkProjectiles_.push_back(std::move(projectile));
	}
}

void EnemyManager::EnsureBossSummonCrystals(const Vector3& targetPosition)
{
	if (!bossSummonCrystals_.empty()) {
		return;
	}
	bossSummonCrystals_.reserve(4u);
	for (int32_t index = 0; index < 4; ++index) {
		const float angle = 2.0f * std::numbers::pi_v<float> *
			static_cast<float>(index) / 4.0f + 0.785f;
		Vector3 summonPosition = targetPosition;
		summonPosition.x += std::sin(angle) * kBossSummonCrystalOffset;
		summonPosition.y = 0.0f;
		summonPosition.z += std::cos(angle) * kBossSummonCrystalOffset;
		auto crystal = std::make_unique<BossSummonCrystal>();
		crystal->Initialize(summonPosition, 99999.0f);
		bossSummonCrystals_.push_back(std::move(crystal));
	}
}

void EnemyManager::SpawnBossInkWave(
	const Vector3& position,
	int32_t waveIndex)
{
	if (bossEnemy_ && bossEnemy_->IsActive()) {
		bossEnemy_->NotifyAttack();
	}
	const int32_t projectileCount = bossAttackTuning_.bulletHellProjectileCount;
	const float divisor = (std::max)(
		1.0f,
		bossAttackTuning_.bulletHellAngleStepDivisor);
	const float angleOffset =
		static_cast<float>(waveIndex) * std::numbers::pi_v<float> / divisor;
	const Vector4 bulletColor{
		0.0f,
		0.90f + 0.05f * (static_cast<float>(waveIndex % 2)),
		1.0f,
		0.98f,
	};
	for (int32_t index = 0; index < projectileCount; ++index) {
		const float angle = angleOffset +
			2.0f * std::numbers::pi_v<float> *
			static_cast<float>(index) /
			static_cast<float>(projectileCount);
		auto projectile = std::make_unique<BossInkProjectile>();
		projectile->Initialize(
			position,
			{ std::sin(angle), 0.0f, std::cos(angle) },
			bossAttackTuning_.bulletHellProjectileSpeed,
			bossAttackTuning_.bulletHellProjectileLifetime,
			bossAttackTuning_.bulletHellProjectileRadius,
			bulletColor,
			"cube.obj");
		bossInkProjectiles_.push_back(std::move(projectile));
	}
}

void EnemyManager::UpdateBossAttackVisuals(float deltaTime)
{
	const BossAttackTelegraph emptyTelegraph{};
	const BossAttackTelegraph& areaTelegraph =
		bossEnemy_ && bossEnemy_->IsActive()
			? bossEnemy_->GetBossAttackTelegraph()
			: emptyTelegraph;
	if (areaTelegraph.type == BossAttackType::SummonLeapShockwave) {
		EnsureBossSummonCrystals(areaTelegraph.targetPosition);
	}
	bossRushTelegraph_.Update(
		bossEnemy_ && bossEnemy_->IsActive()
			? bossEnemy_->GetBossAttackTelegraph()
			: emptyTelegraph);
	bossAreaTelegraph_.Update(areaTelegraph);
	for (auto it = bossBeamBursts_.begin(); it != bossBeamBursts_.end();) {
		if (!*it || !(*it)->Update(deltaTime)) {
			it = bossBeamBursts_.erase(it);
		} else {
			++it;
		}
	}
	for (auto it = bossBeamExplosionChains_.begin();
		it != bossBeamExplosionChains_.end();) {
		if (!*it || !(*it)->Update(deltaTime)) {
			it = bossBeamExplosionChains_.erase(it);
		} else {
			++it;
		}
	}
	for (auto it = bossBeamExplosionDamages_.begin();
		it != bossBeamExplosionDamages_.end();) {
		ActiveBossBeamExplosionDamage& explosion = *it;
		explosion.elapsedTime += (std::max)(0.0f, deltaTime);
		if (explosion.elapsedTime < explosion.delay) {
			++it;
			continue;
		}
		if (!explosion.resolved && player_ && playerManager_ &&
			!playerManager_->IsInvincible() &&
			IsPlayerVulnerableToGroundBossExplosion(
				*player_,
				bossAttackTuning_.beamJumpSafeHeight)) {
			const Vector3 playerPosition = player_->GetWorldPosition();
			const float dx = playerPosition.x - explosion.center.x;
			const float dz = playerPosition.z - explosion.center.z;
			const float radius =
				explosion.radius + player_->GetCollisionRadius();
			if (dx * dx + dz * dz <= radius * radius) {
				if (playerManager_->TakeDamage(explosion.damage)) {
					PlayPlayerDamageSound();
				}
				recentExplosionEffectPositions_.push_back(explosion.center);
			}
		}
		explosion.resolved = true;
		if (explosion.elapsedTime >= explosion.delay + 0.1f) {
			it = bossBeamExplosionDamages_.erase(it);
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
	for (auto it = bossDomeBurstVisuals_.begin(); it != bossDomeBurstVisuals_.end();) {
		if (!*it || !(*it)->Update(deltaTime)) {
			it = bossDomeBurstVisuals_.erase(it);
		} else {
			++it;
		}
	}
	for (auto it = bossShockwaveDamageWaves_.begin(); it != bossShockwaveDamageWaves_.end();) {
		ActiveBossShockwaveDamage& wave = *it;
		wave.elapsedTime += (std::max)(0.0f, deltaTime);
		const float waveDuration = wave.duration > 0.0f
			? wave.duration
			: bossAttackTuning_.shockwaveDuration;
		if (wave.elapsedTime < wave.delay) {
			++it;
			continue;
		}
		const float localTime = wave.elapsedTime - wave.delay;
		if (localTime >= waveDuration || wave.hitPlayer) {
			it = bossShockwaveDamageWaves_.erase(it);
			continue;
		}
		if (player_ && playerManager_ &&
			!playerManager_->IsInvincible() &&
			IsPlayerVulnerableToGroundBossExplosion(
				*player_,
				bossAttackTuning_.shockwaveJumpSafeHeight)) {
			const Vector3 playerPosition = player_->GetWorldPosition();
			const float dx = playerPosition.x - wave.center.x;
			const float dz = playerPosition.z - wave.center.z;
			const float distance = std::sqrt(dx * dx + dz * dz);
			const float progress = std::clamp(localTime / waveDuration, 0.0f, 1.0f);
			const float currentRadius = wave.inward
				? wave.radius * (1.0f - progress)
				: wave.radius * progress;
			const float halfWidth =
				(wave.inward ? 3.4f : 4.5f) + player_->GetCollisionRadius();
			if (std::abs(distance - currentRadius) <= halfWidth) {
				if (playerManager_->TakeDamage(wave.damage)) {
					PlayPlayerDamageSound();
				}
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
