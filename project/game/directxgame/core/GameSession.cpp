#include "game/directxgame/core/GameSession.h"
#include "game/directxgame/core/GameplayRules.h"
#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <random>
#include <string>

namespace {

constexpr char kProfilePath[] = "generated/save/player_profile.txt";

bool StartsWith(std::string_view text, std::string_view prefix)
{
	return text.substr(0, prefix.size()) == prefix;
}

}

namespace DirectXGame {

GameSession::GameSession()
	: baseRandomSeed_(std::random_device{}()),
	runRandomSeed_(baseRandomSeed_)
{
	LoadProfile();
	char* stressCycles = nullptr;
	size_t stressCyclesLength = 0;
	if (_dupenv_s(
		&stressCycles,
		&stressCyclesLength,
		"KUBO_SCENE_STRESS_CYCLES") == 0 &&
		stressCycles) {
		uint32_t parsedCycles = 0;
		const char* end = stressCycles + std::char_traits<char>::length(stressCycles);
		const auto result = std::from_chars(stressCycles, end, parsedCycles);
		if (result.ec == std::errc{} && result.ptr == end) {
			sceneStressCycleTarget_ = parsedCycles;
		}
	}
	std::free(stressCycles);
}

void GameSession::BeginNewRun()
{
	++runCount_;
	runRandomSeed_ = GameplayRules::DeriveRunSeed(baseRandomSeed_, runCount_);
	gameFrameCount_ = 0;
	runCoins_ = 0;
	resultData_ = {};
}

void GameSession::SetRandomSeed(uint32_t seed)
{
	baseRandomSeed_ = seed;
	runRandomSeed_ = seed;
}

void GameSession::OnEnterTitleScene()
{
	++titleVisitCount_;
	sceneStressFrameCount_ = 0;
}

void GameSession::OnEnterGameScene()
{
	++gameVisitCount_;
	sceneStressFrameCount_ = 0;
}

void GameSession::OnEnterResultScene()
{
	++resultVisitCount_;
	sceneStressFrameCount_ = 0;
}

bool GameSession::IsSceneStressComplete() const
{
	return IsSceneStressEnabled() &&
		resultVisitCount_ >= sceneStressCycleTarget_;
}

void GameSession::RecordSrvUsage(
	uint32_t usedCount,
	uint32_t highWatermark)
{
	stressMaxSrvUsed_ = (std::max)(stressMaxSrvUsed_, usedCount);
	stressMaxSrvHighWatermark_ =
		(std::max)(stressMaxSrvHighWatermark_, highWatermark);
}

void GameSession::WriteSceneStressReport() const
{
	const std::filesystem::path outputPath =
		"generated/outputs/scene_transition_stress.txt";
	std::filesystem::create_directories(outputPath.parent_path());
	std::ofstream output(outputPath, std::ios::trunc);
	output << "status=PASS\n";
	output << "targetCycles=" << sceneStressCycleTarget_ << '\n';
	output << "runCount=" << runCount_ << '\n';
	output << "titleVisits=" << titleVisitCount_ << '\n';
	output << "gameVisits=" << gameVisitCount_ << '\n';
	output << "resultVisits=" << resultVisitCount_ << '\n';
	output << "maxSrvUsed=" << stressMaxSrvUsed_ << '\n';
	output << "maxSrvHighWatermark=" << stressMaxSrvHighWatermark_ << '\n';
}

void GameSession::AdvanceGameFrame()
{
	++gameFrameCount_;
}

void GameSession::SetResultSummary(
	uint32_t elapsedFrames,
	uint32_t level,
	uint32_t killCount,
	int32_t totalExp,
	int32_t coins)
{
	resultData_.elapsedFrames = elapsedFrames;
	resultData_.level = level;
	resultData_.killCount = killCount;
	resultData_.totalExp = totalExp;
	resultData_.coins = coins;
}

void GameSession::AddRunCoins(int32_t amount)
{
	if (amount <= 0) {
		return;
	}
	runCoins_ += amount;
	ownedCoins_ += amount;
	SaveProfile();
}

void GameSession::LoadProfile()
{
	std::ifstream input(kProfilePath);
	if (!input) {
		return;
	}

	std::string line;
	while (std::getline(input, line)) {
		constexpr std::string_view kOwnedCoinsPrefix = "ownedCoins=";
		if (!StartsWith(line, kOwnedCoinsPrefix)) {
			continue;
		}
		int32_t parsedCoins = 0;
		const char* begin = line.data() + kOwnedCoinsPrefix.size();
		const char* end = line.data() + line.size();
		const auto result = std::from_chars(begin, end, parsedCoins);
		if (result.ec == std::errc{} && result.ptr == end) {
			ownedCoins_ = (std::max)(0, parsedCoins);
		}
	}
}

void GameSession::SaveProfile() const
{
	const std::filesystem::path outputPath = kProfilePath;
	std::filesystem::create_directories(outputPath.parent_path());
	std::ofstream output(outputPath, std::ios::trunc);
	output << "ownedCoins=" << ownedCoins_ << '\n';
}

}
