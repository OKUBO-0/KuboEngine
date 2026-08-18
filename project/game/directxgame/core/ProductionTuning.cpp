#include "ProductionTuning.h"

#include "DataPaths.h"
#include "GameParticleEffects.h"
#include "Player.h"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

#ifdef _DEBUG
#include <imgui.h>
#endif

namespace DirectXGame::ProductionTuning {

namespace {

std::string ResolvePath(std::string_view filePath)
{
	if (filePath.starts_with("Resources/") ||
		filePath.starts_with("Resources\\")) {
		return std::filesystem::path(filePath).generic_string();
	}
	return DataPaths::Resolve(filePath);
}

void SkipWhitespace(const std::string& text, size_t& index)
{
	while (index < text.size() &&
		std::isspace(static_cast<unsigned char>(text[index]))) {
		++index;
	}
}

bool Consume(const std::string& text, size_t& index, char expected)
{
	SkipWhitespace(text, index);
	if (index >= text.size() || text[index] != expected) {
		return false;
	}
	++index;
	return true;
}

std::string ParseString(const std::string& text, size_t& index)
{
	SkipWhitespace(text, index);
	if (index >= text.size() || text[index] != '"') {
		throw std::runtime_error("production tuning JSON expected string key");
	}
	++index;
	std::string value;
	while (index < text.size()) {
		const char c = text[index++];
		if (c == '"') {
			return value;
		}
		if (c == '\\' && index < text.size()) {
			const char escaped = text[index++];
			switch (escaped) {
			case '"':
			case '\\':
			case '/':
				value.push_back(escaped);
				break;
			case 'n':
				value.push_back('\n');
				break;
			case 'r':
				value.push_back('\r');
				break;
			case 't':
				value.push_back('\t');
				break;
			default:
				value.push_back(escaped);
				break;
			}
			continue;
		}
		value.push_back(c);
	}
	throw std::runtime_error("production tuning JSON unterminated string");
}

double ParseNumberOrBool(const std::string& text, size_t& index)
{
	SkipWhitespace(text, index);
	if (text.compare(index, 4, "true") == 0) {
		index += 4;
		return 1.0;
	}
	if (text.compare(index, 5, "false") == 0) {
		index += 5;
		return 0.0;
	}
	const size_t start = index;
	while (index < text.size()) {
		const char c = text[index];
		if (!(std::isdigit(static_cast<unsigned char>(c)) ||
			c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E')) {
			break;
		}
		++index;
	}
	if (start == index) {
		throw std::runtime_error("production tuning JSON expected number");
	}
	double value = 0.0;
	const auto result = std::from_chars(
		text.data() + start,
		text.data() + index,
		value);
	if (result.ec != std::errc{}) {
		throw std::runtime_error("production tuning JSON invalid number");
	}
	return value;
}

void WriteJsonString(std::ostream& stream, std::string_view value)
{
	stream << '"';
	for (const char c : value) {
		switch (c) {
		case '"':
			stream << "\\\"";
			break;
		case '\\':
			stream << "\\\\";
			break;
		case '\n':
			stream << "\\n";
			break;
		case '\r':
			stream << "\\r";
			break;
		case '\t':
			stream << "\\t";
			break;
		default:
			stream << c;
			break;
		}
	}
	stream << '"';
}

} // namespace

NumberMap DefaultMap()
{
	return {
		{ "player.moveSpeed", 30.0 },
		{ "camera.height", 70.0 },
		{ "camera.distance", 90.0 },
		{ "camera.pitch", 0.52 },
		{ "boss.beam.extraLength", 32.0 },
		{ "boss.beam.minLength", 36.0 },
		{ "boss.beam.tripleAngleOffset", 0.38 },
		{ "boss.beam.width", 2.2 },
		{ "boss.bulletHell.angleStepDivisor", 18.0 },
		{ "boss.bulletHell.extraWaveCount", 5.0 },
		{ "boss.bulletHell.firstDelay", 0.24 },
		{ "boss.bulletHell.projectileCount", 18.0 },
		{ "boss.bulletHell.projectileLifetime", 6.0 },
		{ "boss.bulletHell.projectileRadius", 1.1 },
		{ "boss.bulletHell.projectileSpeed", 14.5 },
		{ "boss.bulletHell.waveInterval", 0.5 },
		{ "boss.converging.projectileCount", 24.0 },
		{ "boss.converging.ringRadius", 24.0 },
		{ "boss.converging.spawnRadius", 23.5 },
		{ "boss.domeBurst.damageScale", 1.5 },
		{ "boss.domeBurst.radius", 58.0 },
		{ "boss.shockwave.duration", 2.4 },
		{ "boss.shockwave.radius", 135.0 },
		{ "boss.summonShockwave.radius", 155.0 },
		{ "boss.summonShockwave.subDamageScale", 0.66 },
		{ "boss.summonShockwave.subDelay", 0.45 },
		{ "boss.summonShockwave.subRadius", 120.0 },
		{ "camera.followSmoothness", 8.0 },
		{ "camera.lookSmoothing", 0.32 },
		{ "camera.mode", 4.0 },
		{ "camera.mouseAimEnabled", 1.0 },
		{ "particle.enemyHitSparkCount", 10.0 },
		{ "particle.enemyDeathSparkCount", 24.0 },
		{ "particle.enemyDeathSmokeCount", 10.0 },
		{ "particle.explosionBurstCount", 44.0 },
		{ "particle.explosionSmokeCount", 18.0 },
		{ "particle.levelUpConfettiCount", 120.0 },
		{ "particle.playerDeathSparkCount", 48.0 },
		{ "particle.particleEmissionScale", 1.0 },
	};
}

NumberMap Load(std::string_view filePath)
{
	const std::string resolvedPath = ResolvePath(filePath);
	std::ifstream file(resolvedPath);
	if (!file.is_open()) {
		return {};
	}
	std::stringstream buffer;
	buffer << file.rdbuf();
	const std::string text = buffer.str();

	NumberMap values;
	size_t index = 0;
	if (!Consume(text, index, '{')) {
		return values;
	}
	SkipWhitespace(text, index);
	while (index < text.size() && text[index] != '}') {
		const std::string key = ParseString(text, index);
		if (!Consume(text, index, ':')) {
			throw std::runtime_error("production tuning JSON expected ':'");
		}
		values[key] = ParseNumberOrBool(text, index);
		SkipWhitespace(text, index);
		if (index < text.size() && text[index] == ',') {
			++index;
			continue;
		}
		break;
	}
	return values;
}

NumberMap LoadOrCreate(std::string_view filePath)
{
	NumberMap values = DefaultMap();
	const std::string resolvedPath = ResolvePath(filePath);
	const bool existed = std::filesystem::exists(resolvedPath);
	const NumberMap loaded = Load(filePath);
	for (const auto& [key, value] : loaded) {
		values[key] = value;
	}
	if (!existed || loaded.size() < values.size()) {
		Save(filePath, values);
	}
	return values;
}

bool Save(std::string_view filePath, const NumberMap& values)
{
	const std::string resolvedPath = ResolvePath(filePath);
	std::filesystem::create_directories(
		std::filesystem::path(resolvedPath).parent_path());
	std::ofstream file(resolvedPath, std::ios::trunc);
	if (!file.is_open()) {
		return false;
	}
	file << "{\n";
	const std::vector<Entry> entries = ToSortedEntries(values);
	for (size_t index = 0; index < entries.size(); ++index) {
		file << "  ";
		WriteJsonString(file, entries[index].key);
		file << ": " << std::fixed << std::setprecision(4)
			<< entries[index].value;
		if (index + 1 < entries.size()) {
			file << ',';
		}
		file << '\n';
	}
	file << "}\n";
	return true;
}

std::vector<Entry> ToSortedEntries(const NumberMap& values)
{
	std::vector<Entry> entries;
	entries.reserve(values.size());
	for (const auto& [key, value] : values) {
		entries.push_back({ key, value });
	}
	std::sort(
		entries.begin(),
		entries.end(),
		[](const Entry& lhs, const Entry& rhs) {
			return lhs.key < rhs.key;
		});
	return entries;
}

float GetFloat(
	const NumberMap& values,
	std::string_view key,
	float fallback)
{
	const auto it = values.find(std::string(key));
	if (it == values.end()) {
		return fallback;
	}
	return static_cast<float>(it->second);
}

int GetInt(
	const NumberMap& values,
	std::string_view key,
	int fallback)
{
	const auto it = values.find(std::string(key));
	if (it == values.end()) {
		return fallback;
	}
	return static_cast<int>(std::lround(it->second));
}

bool GetBool(
	const NumberMap& values,
	std::string_view key,
	bool fallback)
{
	const auto it = values.find(std::string(key));
	if (it == values.end()) {
		return fallback;
	}
	return it->second > 0.5;
}

void SetFloat(NumberMap& values, std::string_view key, float value)
{
	values[std::string(key)] = value;
}

void SetInt(NumberMap& values, std::string_view key, int value)
{
	values[std::string(key)] = static_cast<double>(value);
}

void SetBool(NumberMap& values, std::string_view key, bool value)
{
	values[std::string(key)] = value ? 1.0 : 0.0;
}

void ApplyToPlayer(const NumberMap& values, Player* player)
{
	if (!player) {
		return;
	}
	player->SetMoveSpeed(
		std::clamp(
			GetFloat(values, "player.moveSpeed", player->GetMoveSpeed()),
			0.0f,
			160.0f));
	player->SetCameraHeight(
		std::clamp(
			GetFloat(values, "camera.height", player->GetCameraHeight()),
			1.0f,
			240.0f));
	player->SetCameraDistance(
		std::clamp(
			GetFloat(values, "camera.distance", player->GetCameraDistance()),
			1.0f,
			260.0f));
	player->SetCameraPitch(
		std::clamp(
			GetFloat(values, "camera.pitch", player->GetCameraPitch()),
			0.0f,
			1.5f));
	player->SetCameraFollowSmoothness(
		std::clamp(
			GetFloat(values, "camera.followSmoothness", player->GetCameraFollowSmoothness()),
			0.0f,
			40.0f));
	player->SetCameraLookSmoothing(
		std::clamp(
			GetFloat(values, "camera.lookSmoothing", player->GetCameraLookSmoothing()),
			0.0f,
			0.95f));
	const int mode = std::clamp(
		GetInt(values, "camera.mode", static_cast<int>(player->GetCameraMode())),
		0,
		4);
	player->SetCameraMode(static_cast<Player::CameraMode>(mode));
	player->SetMouseAimEnabled(
		GetBool(values, "camera.mouseAimEnabled", player->IsMouseAimEnabled()));
}

void CaptureFromPlayer(NumberMap& values, const Player* player)
{
	if (!player) {
		return;
	}
	SetFloat(values, "player.moveSpeed", player->GetMoveSpeed());
	SetFloat(values, "camera.height", player->GetCameraHeight());
	SetFloat(values, "camera.distance", player->GetCameraDistance());
	SetFloat(values, "camera.pitch", player->GetCameraPitch());
	SetFloat(values, "camera.followSmoothness", player->GetCameraFollowSmoothness());
	SetFloat(values, "camera.lookSmoothing", player->GetCameraLookSmoothing());
	SetInt(values, "camera.mode", static_cast<int>(player->GetCameraMode()));
	SetBool(values, "camera.mouseAimEnabled", player->IsMouseAimEnabled());
}

void ApplyToParticles(
	const NumberMap& values,
	GameParticleEffects& particleEffects)
{
	GameParticleEffects::Tuning tuning = particleEffects.GetTuning();
	tuning.enemyHitSparkCount =
		GetInt(values, "particle.enemyHitSparkCount", tuning.enemyHitSparkCount);
	tuning.enemyDeathSparkCount =
		GetInt(values, "particle.enemyDeathSparkCount", tuning.enemyDeathSparkCount);
	tuning.enemyDeathSmokeCount =
		GetInt(values, "particle.enemyDeathSmokeCount", tuning.enemyDeathSmokeCount);
	tuning.explosionBurstCount =
		GetInt(values, "particle.explosionBurstCount", tuning.explosionBurstCount);
	tuning.explosionSmokeCount =
		GetInt(values, "particle.explosionSmokeCount", tuning.explosionSmokeCount);
	tuning.levelUpConfettiCount =
		GetInt(values, "particle.levelUpConfettiCount", tuning.levelUpConfettiCount);
	tuning.playerDeathSparkCount =
		GetInt(values, "particle.playerDeathSparkCount", tuning.playerDeathSparkCount);
	tuning.particleEmissionScale =
		GetFloat(values, "particle.particleEmissionScale", tuning.particleEmissionScale);
	particleEffects.SetTuning(tuning);
	particleEffects.ApplyTuning();
}

void CaptureFromParticles(
	NumberMap& values,
	const GameParticleEffects& particleEffects)
{
	const GameParticleEffects::Tuning& tuning = particleEffects.GetTuning();
	SetInt(values, "particle.enemyHitSparkCount", tuning.enemyHitSparkCount);
	SetInt(values, "particle.enemyDeathSparkCount", tuning.enemyDeathSparkCount);
	SetInt(values, "particle.enemyDeathSmokeCount", tuning.enemyDeathSmokeCount);
	SetInt(values, "particle.explosionBurstCount", tuning.explosionBurstCount);
	SetInt(values, "particle.explosionSmokeCount", tuning.explosionSmokeCount);
	SetInt(values, "particle.levelUpConfettiCount", tuning.levelUpConfettiCount);
	SetInt(values, "particle.playerDeathSparkCount", tuning.playerDeathSparkCount);
	SetFloat(values, "particle.particleEmissionScale", tuning.particleEmissionScale);
}

#ifdef _DEBUG
bool DrawDebugUI(
	NumberMap& values,
	Player* player,
	GameParticleEffects& particleEffects)
{
	bool changed = false;
	if (ImGui::CollapsingHeader("Production JSON", ImGuiTreeNodeFlags_DefaultOpen)) {
		float moveSpeed = GetFloat(values, "player.moveSpeed", 30.0f);
		if (ImGui::DragFloat("Player Move Speed", &moveSpeed, 0.5f, 0.0f, 160.0f)) {
			SetFloat(values, "player.moveSpeed", moveSpeed);
			changed = true;
		}
		float height = GetFloat(values, "camera.height", 70.0f);
		float distance = GetFloat(values, "camera.distance", 90.0f);
		float pitch = GetFloat(values, "camera.pitch", 0.52f);
		float follow = GetFloat(values, "camera.followSmoothness", 8.0f);
		float look = GetFloat(values, "camera.lookSmoothing", 0.32f);
		if (ImGui::DragFloat("Camera Height", &height, 0.5f, 1.0f, 240.0f)) {
			SetFloat(values, "camera.height", height);
			changed = true;
		}
		if (ImGui::DragFloat("Camera Distance", &distance, 0.5f, 1.0f, 260.0f)) {
			SetFloat(values, "camera.distance", distance);
			changed = true;
		}
		if (ImGui::DragFloat("Camera Pitch", &pitch, 0.005f, 0.0f, 1.5f)) {
			SetFloat(values, "camera.pitch", pitch);
			changed = true;
		}
		if (ImGui::DragFloat("Camera Follow Smoothness", &follow, 0.1f, 0.0f, 40.0f)) {
			SetFloat(values, "camera.followSmoothness", follow);
			changed = true;
		}
		if (ImGui::DragFloat("Camera Look Smoothing", &look, 0.01f, 0.0f, 0.95f)) {
			SetFloat(values, "camera.lookSmoothing", look);
			changed = true;
		}
		int mode = GetInt(values, "camera.mode", 4);
		if (ImGui::SliderInt("Camera Mode", &mode, 0, 4)) {
			SetInt(values, "camera.mode", mode);
			changed = true;
		}
		bool mouseAim = GetBool(values, "camera.mouseAimEnabled", true);
		if (ImGui::Checkbox("Mouse Aim Enabled", &mouseAim)) {
			SetBool(values, "camera.mouseAimEnabled", mouseAim);
			changed = true;
		}
		int enemyHit = GetInt(values, "particle.enemyHitSparkCount", 10);
		int enemyDeath = GetInt(values, "particle.enemyDeathSparkCount", 24);
		int smoke = GetInt(values, "particle.enemyDeathSmokeCount", 10);
		int burst = GetInt(values, "particle.explosionBurstCount", 44);
		float emissionScale = GetFloat(values, "particle.particleEmissionScale", 1.0f);
		if (ImGui::SliderInt("Enemy Hit Sparks", &enemyHit, 0, 160)) {
			SetInt(values, "particle.enemyHitSparkCount", enemyHit);
			changed = true;
		}
		if (ImGui::SliderInt("Enemy Death Sparks", &enemyDeath, 0, 220)) {
			SetInt(values, "particle.enemyDeathSparkCount", enemyDeath);
			changed = true;
		}
		if (ImGui::SliderInt("Enemy Death Smoke", &smoke, 0, 120)) {
			SetInt(values, "particle.enemyDeathSmokeCount", smoke);
			changed = true;
		}
		if (ImGui::SliderInt("Explosion Burst", &burst, 0, 260)) {
			SetInt(values, "particle.explosionBurstCount", burst);
			changed = true;
		}
		if (ImGui::SliderFloat("Emission Scale", &emissionScale, 0.0f, 1.0f)) {
			SetFloat(values, "particle.particleEmissionScale", emissionScale);
			changed = true;
		}
		if (changed) {
			ApplyToPlayer(values, player);
			ApplyToParticles(values, particleEffects);
		}
		if (ImGui::Button("Capture Runtime")) {
			CaptureFromPlayer(values, player);
			CaptureFromParticles(values, particleEffects);
		}
		ImGui::SameLine();
		if (ImGui::Button("Reload JSON")) {
			values = LoadOrCreate(DataPaths::kProductionTuning);
			ApplyToPlayer(values, player);
			ApplyToParticles(values, particleEffects);
		}
		ImGui::SameLine();
		if (ImGui::Button("Save JSON")) {
			CaptureFromPlayer(values, player);
			CaptureFromParticles(values, particleEffects);
			Save(DataPaths::kProductionTuning, values);
		}
	}
	return changed;
}
#endif

}
