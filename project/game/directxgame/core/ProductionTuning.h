#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace DirectXGame {

class GameParticleEffects;
class Player;

namespace ProductionTuning {

using NumberMap = std::unordered_map<std::string, double>;

struct Entry {
	std::string key;
	double value = 0.0;
};

NumberMap DefaultMap();
NumberMap Load(std::string_view filePath);
NumberMap LoadOrCreate(std::string_view filePath);
bool Save(std::string_view filePath, const NumberMap& values);
std::vector<Entry> ToSortedEntries(const NumberMap& values);

float GetFloat(
	const NumberMap& values,
	std::string_view key,
	float fallback);
int GetInt(
	const NumberMap& values,
	std::string_view key,
	int fallback);
bool GetBool(
	const NumberMap& values,
	std::string_view key,
	bool fallback);
void SetFloat(NumberMap& values, std::string_view key, float value);
void SetInt(NumberMap& values, std::string_view key, int value);
void SetBool(NumberMap& values, std::string_view key, bool value);

void ApplyToPlayer(const NumberMap& values, Player* player);
void CaptureFromPlayer(NumberMap& values, const Player* player);
void ApplyToParticles(
	const NumberMap& values,
	GameParticleEffects& particleEffects);
void CaptureFromParticles(
	NumberMap& values,
	const GameParticleEffects& particleEffects);

#ifdef _DEBUG
bool DrawDebugUI(
	NumberMap& values,
	Player* player,
	GameParticleEffects& particleEffects);
#endif

}

}
