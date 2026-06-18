#include "game/directxgame/enemy/EnemySpawnController.h"

#include "game/directxgame/core/CsvReader.h"
#include "game/directxgame/enemy/Enemy.h"
#include "game/directxgame/player/Player.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <stdexcept>

namespace DirectXGame {

void EnemySpawnController::Initialize(
	const std::string& enemyTypesPath,
	const std::string& spawnSettingsPath)
{
	elapsedTime_ = 0.0f;
	LoadEnemyTypes(enemyTypesPath);
	LoadSpawnSettings(spawnSettingsPath);
	spawnTimer_ = spawnInterval_;
}

void EnemySpawnController::LoadEnemyTypes(const std::string& filePath)
{
	enemyTypes_.clear();
	const CsvReader::CsvTable rows = CsvReader::LoadRows(filePath);
	for (size_t rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
		const CsvReader::CsvRow& row = rows[rowIndex];
		if (row.size() < 5) {
			throw std::runtime_error(
				"enemyTypes row " + std::to_string(rowIndex + 1) +
				" requires 5 columns");
		}

		EnemyTypeData data{};
		const std::string context =
			"enemyTypes row " + std::to_string(rowIndex + 1);
		data.type = CsvReader::ParseInt32(row[0], context + " type");
		data.baseHP = CsvReader::ParseInt32(row[1], context + " baseHP");
		data.baseSpeed =
			CsvReader::ParseFloat(row[2], context + " baseSpeed");
		data.baseEXP =
			CsvReader::ParseInt32(row[3], context + " baseEXP");
		data.spawnCount =
			CsvReader::ParseInt32(row[4], context + " spawnCount");
		if (data.type < 0 || data.baseHP < 1 ||
			data.baseSpeed <= 0.0f || data.baseEXP < 0 ||
			data.spawnCount < 1) {
			throw std::runtime_error(
				context + " contains an out-of-range value");
		}
		enemyTypes_.push_back(data);
	}
	if (enemyTypes_.empty()) {
		throw std::runtime_error("enemyTypes contains no enemy definitions");
	}
}

void EnemySpawnController::LoadSpawnSettings(const std::string& filePath)
{
	const CsvReader::KeyValueMap values =
		CsvReader::LoadKeyValueMap(filePath);
	for (const auto& [key, value] : values) {
		if (key == "maxActiveEnemies") {
			maxActiveEnemies_ = static_cast<size_t>((std::max)(
				1,
				CsvReader::ParseInt32(
					value,
					"enemySpawnSettings.maxActiveEnemies")));
		} else if (key == "spawnUnlockInterval") {
			spawnUnlockInterval_ =
				CsvReader::ParseFloat(
					value,
					"enemySpawnSettings.spawnUnlockInterval");
		} else if (key == "spawnDistance") {
			spawnDistance_ = CsvReader::ParseFloat(
				value,
				"enemySpawnSettings.spawnDistance");
		} else if (key == "respawnDistance") {
			respawnDistance_ = CsvReader::ParseFloat(
				value,
				"enemySpawnSettings.respawnDistance");
		} else if (key == "respawnRadius") {
			respawnRadius_ = CsvReader::ParseFloat(
				value,
				"enemySpawnSettings.respawnRadius");
		} else if (key == "minSpawnInterval") {
			minSpawnInterval_ =
				CsvReader::ParseFloat(
					value,
					"enemySpawnSettings.minSpawnInterval");
		} else if (key == "baseSpawnInterval") {
			baseSpawnInterval_ =
				CsvReader::ParseFloat(
					value,
					"enemySpawnSettings.baseSpawnInterval");
			spawnInterval_ = baseSpawnInterval_;
		} else if (key == "spawnAcceleration") {
			spawnAcceleration_ =
				CsvReader::ParseFloat(
					value,
					"enemySpawnSettings.spawnAcceleration");
		}
	}
	if (spawnUnlockInterval_ <= 0.0f ||
		spawnDistance_ <= 0.0f ||
		respawnDistance_ <= 0.0f ||
		respawnRadius_ <= 0.0f ||
		minSpawnInterval_ <= 0.0f ||
		baseSpawnInterval_ < minSpawnInterval_ ||
		spawnAcceleration_ < 0.0f) {
		throw std::runtime_error(
			"enemySpawnSettings contains an out-of-range value");
	}
}

void EnemySpawnController::Update(
	float deltaTime,
	Player* player,
	std::vector<std::unique_ptr<Enemy>>& enemies,
	bool bossPhase)
{
	if (bossPhase || !player) {
		return;
	}

	elapsedTime_ += deltaTime;
	spawnTimer_ += deltaTime;
	spawnInterval_ = (std::max)(
		minSpawnInterval_,
		baseSpawnInterval_ - elapsedTime_ * spawnAcceleration_);
	while (spawnTimer_ >= spawnInterval_) {
		SpawnEnemies(*player, enemies);
		spawnTimer_ -= spawnInterval_;
	}
}

void EnemySpawnController::RelocateFarEnemies(
	Player* player,
	std::vector<std::unique_ptr<Enemy>>& enemies,
	const Enemy* bossEnemy,
	bool bossPhase) const
{
	if (!player) {
		return;
	}

	const Vector3 playerPosition = player->GetWorldPosition();
	const float respawnDistanceSq =
		respawnDistance_ * respawnDistance_;
	for (std::unique_ptr<Enemy>& enemy : enemies) {
		if (!enemy || !enemy->IsActive()) {
			continue;
		}
		if (bossPhase && enemy.get() == bossEnemy) {
			continue;
		}
		if (enemy->IsSuicideType()) {
			continue;
		}

		const Vector3 enemyPosition = enemy->GetPosition();
		const float dx = enemyPosition.x - playerPosition.x;
		const float dz = enemyPosition.z - playerPosition.z;
		if (dx * dx + dz * dz <= respawnDistanceSq) {
			continue;
		}

		const float angle =
			(static_cast<float>(std::rand()) /
				static_cast<float>(RAND_MAX)) *
			6.283185307f;
		enemy->SetPosition({
			playerPosition.x + std::cos(angle) * respawnRadius_,
			0.0f,
			playerPosition.z + std::sin(angle) * respawnRadius_,
		});
	}
}

std::unique_ptr<Enemy> EnemySpawnController::CreateBossEnemy(
	Player* player) const
{
	if (!player) {
		return nullptr;
	}

	EnemyTypeData data{};
	if (!enemyTypes_.empty()) {
		data = enemyTypes_.back();
	}
	data.type = 5;
	data.baseHP = (std::max)(data.baseHP, 140);
	data.baseSpeed = (std::max)(data.baseSpeed, 0.18f);
	data.baseEXP = (std::max)(data.baseEXP, 80);

	const Vector3 playerPosition = player->GetWorldPosition();
	auto enemy = std::make_unique<Enemy>();
	enemy->Initialize();
	enemy->SetPlayer(player);
	enemy->SetPosition({
		playerPosition.x,
		4.0f,
		playerPosition.z + 34.0f,
	});
	enemy->SetModelByType(data.type);
	enemy->SetBehaviorByType(data.type);
	enemy->SetBoss(true);
	enemy->SetHP(data.baseHP);
	enemy->SetEXP(data.baseEXP);
	enemy->SetSpeed(data.baseSpeed);
	return enemy;
}

void EnemySpawnController::SpawnEnemies(
	Player& player,
	std::vector<std::unique_ptr<Enemy>>& enemies)
{
	if (enemyTypes_.empty() ||
		CountActiveEnemies(enemies) >= maxActiveEnemies_) {
		return;
	}

	int maxIndex =
		static_cast<int>(elapsedTime_ / spawnUnlockInterval_);
	maxIndex = std::clamp(
		maxIndex,
		0,
		static_cast<int>(enemyTypes_.size()) - 1);
	const EnemyTypeData& data =
		enemyTypes_[static_cast<size_t>(
			std::rand() % (maxIndex + 1))];

	for (int index = 0; index < data.spawnCount; ++index) {
		if (CountActiveEnemies(enemies) >= maxActiveEnemies_) {
			break;
		}
		SpawnOneEnemy(data, player, enemies);
	}
}

void EnemySpawnController::SpawnOneEnemy(
	const EnemyTypeData& data,
	Player& player,
	std::vector<std::unique_ptr<Enemy>>& enemies) const
{
	const Vector3 playerPosition = player.GetWorldPosition();
	const float angle =
		(static_cast<float>(std::rand()) /
			static_cast<float>(RAND_MAX)) *
		6.283185307f;

	auto enemy = std::make_unique<Enemy>();
	enemy->Initialize();
	enemy->SetPlayer(&player);
	enemy->SetPosition({
		playerPosition.x + std::cos(angle) * spawnDistance_,
		0.0f,
		playerPosition.z + std::sin(angle) * spawnDistance_,
	});
	enemy->SetModelByType(data.type);
	enemy->SetBehaviorByType(data.type);
	enemy->SetHP(
		data.baseHP + static_cast<int>(elapsedTime_ / 45.0f));
	enemy->SetEXP(
		data.baseEXP + static_cast<int>(elapsedTime_ / 35.0f));
	enemy->SetSpeed(
		data.baseSpeed + elapsedTime_ * 0.0015f);
	enemies.push_back(std::move(enemy));
}

size_t EnemySpawnController::CountActiveEnemies(
	const std::vector<std::unique_ptr<Enemy>>& enemies)
{
	return static_cast<size_t>(std::count_if(
		enemies.begin(),
		enemies.end(),
		[](const std::unique_ptr<Enemy>& enemy) {
			return enemy && enemy->IsActive();
		}));
}

}
