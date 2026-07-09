#include "EnemySpawnController.h"

#include "CsvReader.h"
#include "Enemy.h"
#include "Player.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace DirectXGame {

namespace {

bool IsSupportedEnemyType(int32_t value)
{
	return value == static_cast<int32_t>(EnemyType::Standard) ||
		value == static_cast<int32_t>(EnemyType::Tackler) ||
		value == static_cast<int32_t>(EnemyType::Bomb) ||
		value == static_cast<int32_t>(EnemyType::Fast) ||
		value == static_cast<int32_t>(EnemyType::Heavy) ||
		value == static_cast<int32_t>(EnemyType::Gold);
}

bool IsSupportedBehavior(int32_t value)
{
	return value == static_cast<int32_t>(EnemyBehaviorType::Chase) ||
		value == static_cast<int32_t>(EnemyBehaviorType::Tackle);
}

} // namespace

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
		if (row.size() < 12) {
			throw std::runtime_error(
				"enemyTypes row " + std::to_string(rowIndex + 1) +
				" requires 12 columns");
		}

		EnemyDefinition data{};
		const std::string context =
			"enemyTypes row " + std::to_string(rowIndex + 1);
		const int32_t type = CsvReader::ParseInt32(row[0], context + " type");
		data.type = static_cast<EnemyType>(type);
		data.baseHP = CsvReader::ParseInt32(row[1], context + " baseHP");
		data.baseSpeed =
			CsvReader::ParseFloat(row[2], context + " baseSpeed");
		data.baseEXP =
			CsvReader::ParseInt32(row[3], context + " baseEXP");
		data.spawnCount =
			CsvReader::ParseInt32(row[4], context + " spawnCount");
		data.coinReward = CsvReader::ParseInt32(row[5], context + " coinReward");
		data.damage = CsvReader::ParseInt32(row[6], context + " damage");
		data.knockbackResistance =
			CsvReader::ParseFloat(row[7], context + " knockbackResistance");
		const int32_t behavior =
			CsvReader::ParseInt32(row[8], context + " behavior");
		data.behavior = static_cast<EnemyBehaviorType>(behavior);
		data.deathBombDelay =
			CsvReader::ParseFloat(row[9], context + " deathBombDelay");
		data.deathBombRadius =
			CsvReader::ParseFloat(row[10], context + " deathBombRadius");
		data.deathBombDamage =
			CsvReader::ParseInt32(row[11], context + " deathBombDamage");
		if (!IsSupportedEnemyType(type) || !IsSupportedBehavior(behavior) ||
			data.baseHP < 1 ||
			data.baseSpeed <= 0.0f || data.baseEXP < 0 ||
			data.spawnCount < 1 || data.coinReward < 0 ||
			data.damage < 1 || data.knockbackResistance < 0.0f ||
			data.knockbackResistance > 1.0f ||
			data.deathBombDelay < 0.0f || data.deathBombRadius < 0.0f ||
			data.deathBombDamage < 0 ||
			(data.type == EnemyType::Bomb &&
				(data.deathBombDelay <= 0.0f || data.deathBombRadius <= 0.0f ||
					data.deathBombDamage <= 0))) {
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

void EnemySpawnController::SpawnBenchmarkEnemies(
	Player* player,
	std::vector<std::unique_ptr<Enemy>>& enemies,
	size_t targetCount)
{
	if (!player || enemyTypes_.empty()) {
		return;
	}
	while (CountActiveEnemies(enemies) < targetCount) {
		const size_t index = CountActiveEnemies(enemies) % enemyTypes_.size();
		SpawnOneEnemy(enemyTypes_[index], *player, enemies);
	}
}

void EnemySpawnController::RelocateFarEnemies(
	Player* player,
	std::vector<std::unique_ptr<Enemy>>& enemies,
	const Enemy* bossEnemy,
	bool bossPhase)
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

		const float angle = RandomAngle();
		enemy->SetPosition({
			playerPosition.x + std::cos(angle) * respawnRadius_,
			0.0f,
			playerPosition.z + std::sin(angle) * respawnRadius_,
		});
		enemy->StartSpawnPresentation();
	}
}

std::unique_ptr<Enemy> EnemySpawnController::CreateBossEnemy(
	Player* player,
	int32_t playerLevel) const
{
	if (!player) {
		return nullptr;
	}

	EnemyDefinition data{};
	if (!enemyTypes_.empty()) {
		data = enemyTypes_.back();
	}
	data.type = EnemyType::Boss;
	data.behavior = EnemyBehaviorType::Boss;
	constexpr int32_t kBaseBossHP = 1200;
	constexpr int32_t kHpPerPlayerLevel = 60;
	const int32_t scaledPlayerLevel = std::clamp(playerLevel, 1, 100);
	data.baseHP = kBaseBossHP +
		(scaledPlayerLevel - 1) * kHpPerPlayerLevel;
	data.baseSpeed = (std::max)(data.baseSpeed, 0.18f);
	data.baseEXP = (std::max)(data.baseEXP, 80);
	data.coinReward = (std::max)(data.coinReward, 50);
	data.damage = (std::max)(data.damage, 30);

	const Vector3 playerPosition = player->GetWorldPosition();
	auto enemy = std::make_unique<Enemy>();
	enemy->Initialize();
	enemy->SetPlayer(player);
	enemy->SetPosition({
		playerPosition.x,
		4.0f,
		playerPosition.z + 26.0f,
	});
	enemy->SetRotationY(std::numbers::pi_v<float>);
	enemy->SetType(data.type);
	enemy->SetModelByType(static_cast<int32_t>(data.type));
	enemy->SetBehavior(data.behavior);
	enemy->SetBoss(true);
	enemy->SetHP(data.baseHP);
	enemy->SetEXP(data.baseEXP);
	enemy->SetCoinValue(data.coinReward);
	enemy->SetAttackPower(data.damage);
	enemy->SetKnockbackResistance(data.knockbackResistance);
	enemy->SetDeathBomb(
		data.deathBombDelay, data.deathBombRadius, data.deathBombDamage);
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
	const EnemyDefinition& data =
		enemyTypes_[static_cast<size_t>(
			std::uniform_int_distribution<int>(0, maxIndex)(randomEngine_))];

	for (int index = 0; index < data.spawnCount; ++index) {
		if (CountActiveEnemies(enemies) >= maxActiveEnemies_) {
			break;
		}
		SpawnOneEnemy(data, player, enemies);
	}
}

void EnemySpawnController::SpawnOneEnemy(
	const EnemyDefinition& data,
	Player& player,
	std::vector<std::unique_ptr<Enemy>>& enemies)
{
	const Vector3 playerPosition = player.GetWorldPosition();
	const float angle = RandomAngle();

	auto enemy = std::make_unique<Enemy>();
	enemy->Initialize();
	enemy->SetPlayer(&player);
	enemy->SetPosition({
		playerPosition.x + std::cos(angle) * spawnDistance_,
		0.0f,
		playerPosition.z + std::sin(angle) * spawnDistance_,
	});
	enemy->SetType(data.type);
	enemy->SetModelByType(static_cast<int32_t>(data.type));
	enemy->SetBehavior(data.behavior);
	enemy->SetHP(
		data.baseHP + static_cast<int>(elapsedTime_ / 45.0f));
	enemy->SetEXP(
		data.baseEXP + static_cast<int>(elapsedTime_ / 35.0f));
	enemy->SetCoinValue(
		data.coinReward + static_cast<int>(elapsedTime_ / 75.0f));
	enemy->SetAttackPower(
		data.damage + static_cast<int>(elapsedTime_ / 90.0f));
	enemy->SetKnockbackResistance(data.knockbackResistance);
	enemy->SetDeathBomb(
		data.deathBombDelay, data.deathBombRadius, data.deathBombDamage);
	enemy->SetSpeed(
		data.baseSpeed + elapsedTime_ * 0.0015f);
	enemy->StartSpawnPresentation();
	enemies.push_back(std::move(enemy));
}

float EnemySpawnController::RandomAngle()
{
	return std::uniform_real_distribution<float>(
		0.0f,
		2.0f * std::numbers::pi_v<float>)(randomEngine_);
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
