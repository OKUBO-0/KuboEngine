#include "game/directxgame/enemy/EnemyManager.h"
#include "game/directxgame/core/DataPaths.h"
#include "game/directxgame/core/GameSession.h"
#include "game/directxgame/player/Player.h"
#include "game/directxgame/player/PlayerManager.h"
#include <algorithm>

namespace DirectXGame {

void EnemyManager::Initialize(const std::string& enemyTypesPath, Player* player, PlayerManager* playerManager)
{
	player_ = player;
	playerManager_ = playerManager;
	bossEnemy_ = nullptr;
	bossPhase_ = false;
	bossDefeated_ = false;
	if (playerManager_) {
		playerManager_->SetEnemyManager(this);
	}
	spawnController_.Initialize(
		enemyTypesPath,
		DataPaths::Resolve(DataPaths::kEnemySpawnSettings));
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

void EnemyManager::Update(float deltaTime)
{
	spawnController_.Update(
		deltaTime,
		player_,
		enemies_,
		bossPhase_);
	UpdateEnemies(deltaTime);
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

	EnemyCollisionSystem::RebuildContext(enemies_, collisionContext_);
	EnemyCollisionSystem::CheckCollisions(
		*player,
		*playerManager,
		collisionContext_,
		recentHitEffectPositions_,
		recentDeathEffectPositions_);
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
	EnemyCollisionSystem::ApplyAreaDamage(
		center,
		radius,
		damage,
		enemies_,
		recentHitEffectPositions_);
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
		spawnController_.CreateBossEnemy(player_);
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
			enemy->ResetJustDied();
		}
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

void EnemyManager::SpawnDeathDrop(const Enemy& enemy)
{
	++totalKillCount_;
	if (session_) {
		session_->AddRunCoins(enemy.GetCoinValue());
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
