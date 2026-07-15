#include "EnemyManager.h"
#include "DataPaths.h"
#include "GameSession.h"
#include "Player.h"
#include "PlayerManager.h"
#include "Line.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <numbers>
#include <string_view>

namespace DirectXGame {

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
			enemy->Draw();
		}
	}
	for (std::unique_ptr<ExpOrb>& orb : expOrbs_) {
		orb->Draw();
	}
	for (const std::unique_ptr<EnemyDeathBomb>& bomb : deathBombs_) {
		if (bomb) {
			bomb->Draw();
		}
	}
	for (const std::unique_ptr<BossInkProjectile>& projectile :
		bossInkProjectiles_) {
		if (projectile) {
			projectile->Draw();
		}
	}
	bossRushTelegraph_.Draw();
	for (const std::unique_ptr<BossSlamCube>& cube : bossSlamCubes_) {
		if (cube) {
			cube->Draw();
		}
	}
	DrawBossAttackTelegraph();
}

void EnemyManager::DrawShadow()
{
	for (std::unique_ptr<Enemy>& enemy : enemies_) {
		if (enemy && (enemy->IsActive() || enemy->IsDeathPresentationActive())) {
			enemy->DrawShadow();
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
	for (std::unique_ptr<Enemy>& enemy : enemies_) {
		if (!enemy) {
			continue;
		}
		if (enemy->IsActive()) {
			enemy->Update(deltaTime);
			if (enemy->ConsumeGroundImpact()) {
				recentDeathEffectPositions_.push_back(enemy->GetPosition());
			}
		} else if (enemy->GetHP() <= 0 && enemy->JustDied()) {
			if (enemy.get() == bossEnemy_) {
				bossDefeated_ = true;
				enemy->StartDeathPresentation();
			}
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
		if (event.type == BossAttackType::TentacleSlam) {
			const Vector3 side{
				-event.direction.z,
				0.0f,
				event.direction.x,
			};
			for (int32_t row = 1; row <= 6; ++row) {
				const float forwardDistance = static_cast<float>(row) * 4.5f;
				const float columnSpacing = 2.2f + static_cast<float>(row) * 1.8f;
				for (int32_t column = -2; column <= 2; ++column) {
					auto cube = std::make_unique<BossSlamCube>();
					cube->Initialize(
						event.position + event.direction * forwardDistance +
							side * (columnSpacing * static_cast<float>(column)),
						static_cast<float>(row - 1) * 0.045f);
					bossSlamCubes_.push_back(std::move(cube));
				}
			}
			if (!player_ || !playerManager_ || player_->IsDodging() ||
				playerManager_->IsInvincible()) {
				continue;
			}
			const Vector3 offset = player_->GetWorldPosition() - event.position;
			const float distanceSq = offset.x * offset.x + offset.z * offset.z;
			constexpr float kSlamRadius = 30.0f;
			if (distanceSq > kSlamRadius * kSlamRadius) {
				continue;
			}
			const float distance = std::sqrt(distanceSq);
			const float directionDot = distance <= 0.001f
				? 1.0f
				: (offset.x * event.direction.x + offset.z * event.direction.z) /
					distance;
			if (directionDot >= std::cos(0.8f)) {
				playerManager_->TakeDamage(bossEnemy_->GetAttackPower());
				recentExplosionEffectPositions_.push_back(event.position);
			}
		} else if (event.type == BossAttackType::InkBurst) {
			bossInkWavePosition_ = event.position;
			SpawnBossInkWave(bossInkWavePosition_, 0);
			pendingBossInkWaves_ = 2;
			bossInkNextWaveIndex_ = 1;
			bossInkWaveTimer_ = 0.5f;
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
	constexpr int32_t kProjectileCount = 12;
	const float angleOffset =
		static_cast<float>(waveIndex) * std::numbers::pi_v<float> / 12.0f;
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
	if (!bossEnemy_ || !bossEnemy_->IsActive()) {
		return;
	}
	const BossAttackTelegraph& telegraph =
		bossEnemy_->GetBossAttackTelegraph();
	if (telegraph.type == BossAttackType::None) {
		return;
	}
	Engine::LineSystem::Line line;
	const float alpha = 0.35f + telegraph.progress * 0.65f;
	if (telegraph.type == BossAttackType::TentacleSlam) {
		constexpr float kLength = 30.0f;
		constexpr float kHalfWidth = 31.0f;
		const Vector3 origin{
			telegraph.position.x,
			0.15f,
			telegraph.position.z,
		};
		const Vector3 end = origin + telegraph.direction * kLength;
		const Vector3 side{
			-telegraph.direction.z * kHalfWidth,
			0.0f,
			telegraph.direction.x * kHalfWidth,
		};
		const Vector4 color{ 1.0f, 0.0f, 0.0f, alpha };
		line.Draw(origin, end - side, color);
		line.Draw(origin, end + side, color);
		line.Draw(end - side, end + side, color);
		for (int32_t ray = -1; ray <= 1; ++ray) {
			line.Draw(
				origin,
				end + side * (static_cast<float>(ray) * 0.5f),
				color);
		}
		for (int32_t band = 1; band <= 4; ++band) {
			const float progress = static_cast<float>(band) / 4.0f;
			const Vector3 bandCenter =
				origin + telegraph.direction * (kLength * progress);
			const Vector3 bandSide = side * progress;
			line.Draw(bandCenter - bandSide, bandCenter + bandSide, color);
		}
	} else if (telegraph.type == BossAttackType::InkBurst) {
		line.DrawSphere(
			{ telegraph.position.x, 0.25f, telegraph.position.z },
			5.0f + telegraph.progress * 2.0f,
			{ 0.46f, 0.08f, 0.7f, alpha });
	}
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
