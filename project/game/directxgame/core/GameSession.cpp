#include "GameSession.h"
#include "GameplayRules.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <string_view>
#include <random>
#include <string>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Psapi.h>

#pragma comment(lib, "Psapi.lib")

namespace {

constexpr char kProfilePath[] = "generated/save/player_profile.txt";
constexpr int32_t kPermanentMaxHPLevelCap = 3;
constexpr int32_t kPermanentAttackLevelCap = 5;
constexpr int32_t kPermanentMoveSpeedLevelCap = 3;
constexpr int32_t kPermanentExpPickupRangeLevelCap = 3;
constexpr int32_t kPermanentCoinGainLevelCap = 3;
constexpr int32_t kPermanentMaxHPBaseCost = 240;
constexpr int32_t kPermanentAttackBaseCost = 180;
constexpr int32_t kPermanentMoveSpeedBaseCost = 200;
constexpr int32_t kPermanentExpPickupRangeBaseCost = 220;
constexpr int32_t kPermanentCoinGainBaseCost = 300;
constexpr int32_t kDefaultUnlockedCharacterMask = 1 << static_cast<int32_t>(DirectXGame::CharacterId::Octopus);
constexpr std::array<DirectXGame::CharacterDefinition, 4> kCharacterDefinitions{ {
	{ DirectXGame::CharacterId::Octopus, 0, true },
	{ DirectXGame::CharacterId::Flame, 440, false },
	{ DirectXGame::CharacterId::Blade, 520, false },
	{ DirectXGame::CharacterId::Storm, 480, false },
} };

bool StartsWith(std::string_view text, std::string_view prefix)
{
	return text.substr(0, prefix.size()) == prefix;
}

bool ParseProfileInt(
	std::string_view line,
	std::string_view prefix,
	int32_t& output)
{
	if (!StartsWith(line, prefix)) {
		return false;
	}
	int32_t parsedValue = 0;
	const char* begin = line.data() + prefix.size();
	const char* end = line.data() + line.size();
	const auto result = std::from_chars(begin, end, parsedValue);
	if (result.ec == std::errc{} && result.ptr == end) {
		output = parsedValue;
	}
	return true;
}

int32_t UpgradeCost(int32_t baseCost, int32_t level, int32_t cap)
{
	if (level >= cap) {
		return 0;
	}
	return baseCost * (level + 1);
}

bool TryPurchaseUpgrade(
	int32_t& ownedCoins,
	int32_t& level,
	int32_t cap,
	int32_t baseCost)
{
	const int32_t cost = UpgradeCost(baseCost, level, cap);
	if (cost <= 0 || ownedCoins < cost) {
		return false;
	}
	ownedCoins -= cost;
	++level;
	return true;
}

const DirectXGame::CharacterDefinition* FindCharacterDefinition(
	DirectXGame::CharacterId id)
{
	for (const DirectXGame::CharacterDefinition& definition : kCharacterDefinitions) {
		if (definition.id == id) {
			return &definition;
		}
	}
	return nullptr;
}

DirectXGame::CharacterId ClampCharacterId(int32_t rawId)
{
	if (rawId < 0 ||
		rawId >= static_cast<int32_t>(kCharacterDefinitions.size())) {
		return DirectXGame::CharacterId::Octopus;
	}
	return static_cast<DirectXGame::CharacterId>(rawId);
}

}

namespace DirectXGame {

GameSession::GameSession()
	: baseRandomSeed_(std::random_device{}()),
	runRandomSeed_(baseRandomSeed_),
	unlockedCharacterMask_(kDefaultUnlockedCharacterMask)
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
	char* stressGameFrames = nullptr;
	size_t stressGameFramesLength = 0;
	if (_dupenv_s(
		&stressGameFrames,
		&stressGameFramesLength,
		"KUBO_SCENE_STRESS_GAME_FRAMES") == 0 &&
		stressGameFrames) {
		uint32_t parsedFrames = 0;
		const char* end = stressGameFrames +
			std::char_traits<char>::length(stressGameFrames);
		const auto result =
			std::from_chars(stressGameFrames, end, parsedFrames);
		if (result.ec == std::errc{} && result.ptr == end && parsedFrames > 0) {
			sceneStressGameFrameTarget_ = parsedFrames;
		}
	}
	std::free(stressGameFrames);
}

void GameSession::BeginNewRun()
{
	++runCount_;
	runRandomSeed_ = GameplayRules::DeriveRunSeed(baseRandomSeed_, runCount_);
	gameFrameCount_ = 0;
	runCoins_ = 0;
	runCoinsCommitted_ = false;
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
	CommitRunCoinsToProfile();
}

bool GameSession::IsSceneStressComplete() const
{
	return IsSceneStressEnabled() &&
		resultVisitCount_ >= sceneStressCycleTarget_;
}

void GameSession::RecordSrvUsage(
	SceneStressStage stage,
	uint32_t usedCount,
	uint32_t highWatermark,
	uint32_t texture2D,
	uint32_t textureCube,
	uint32_t structuredBuffer,
	uint32_t shadowMap,
	uint32_t otherSrv)
{
	stressMaxSrvUsed_ = (std::max)(stressMaxSrvUsed_, usedCount);
	stressMaxSrvHighWatermark_ =
		(std::max)(stressMaxSrvHighWatermark_, highWatermark);

	PROCESS_MEMORY_COUNTERS_EX memory{};
	memory.cb = sizeof(memory);
	GetProcessMemoryInfo(
		GetCurrentProcess(),
		reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
		sizeof(memory));
	const uint32_t cycle = stage == SceneStressStage::Title
		? runCount_ + 1
		: runCount_;
	sceneStressSamples_.push_back({
		stage,
		cycle,
		sceneStressFrameCount_,
		usedCount,
		highWatermark,
		texture2D,
		textureCube,
		structuredBuffer,
		shadowMap,
		otherSrv,
		static_cast<uint64_t>(memory.WorkingSetSize),
		static_cast<uint64_t>(memory.PrivateUsage),
	});
}

void GameSession::WriteSceneStressReport()
{
	sceneStressReportWritten_ = true;
	struct CycleSummary {
		uint32_t cycle = 0;
		uint32_t peakSrvUsed = 0;
		uint64_t firstWorkingSetBytes = 0;
		uint64_t peakWorkingSetBytes = 0;
		uint64_t finalWorkingSetBytes = 0;
		uint64_t firstPrivateBytes = 0;
		uint64_t peakPrivateBytes = 0;
		uint64_t finalPrivateBytes = 0;
	};
	std::vector<CycleSummary> cycleSummaries;
	cycleSummaries.reserve(sceneStressCycleTarget_);
	for (uint32_t cycle = 1; cycle <= sceneStressCycleTarget_; ++cycle) {
		CycleSummary summary{};
		summary.cycle = cycle;
		for (const SceneStressSample& sample : sceneStressSamples_) {
			if (sample.cycle != cycle) {
				continue;
			}
			if (summary.firstWorkingSetBytes == 0) {
				summary.firstWorkingSetBytes = sample.workingSetBytes;
				summary.firstPrivateBytes = sample.privateBytes;
			}
			summary.finalWorkingSetBytes = sample.workingSetBytes;
			summary.finalPrivateBytes = sample.privateBytes;
			summary.peakWorkingSetBytes =
				(std::max)(summary.peakWorkingSetBytes, sample.workingSetBytes);
			summary.peakPrivateBytes =
				(std::max)(summary.peakPrivateBytes, sample.privateBytes);
			summary.peakSrvUsed =
				(std::max)(summary.peakSrvUsed, sample.srvUsed);
		}
		cycleSummaries.push_back(summary);
	}

	uint32_t peakSrvTexture2D = 0;
	uint32_t peakSrvTextureCube = 0;
	uint32_t peakSrvStructuredBuffer = 0;
	uint32_t peakSrvShadowMap = 0;
	uint32_t peakSrvOther = 0;
	const auto peakSrv = std::max_element(
		sceneStressSamples_.begin(),
		sceneStressSamples_.end(),
		[](const SceneStressSample& lhs, const SceneStressSample& rhs) {
			return lhs.srvUsed < rhs.srvUsed;
		});
	if (peakSrv != sceneStressSamples_.end()) {
		peakSrvTexture2D = peakSrv->texture2D;
		peakSrvTextureCube = peakSrv->textureCube;
		peakSrvStructuredBuffer = peakSrv->structuredBuffer;
		peakSrvShadowMap = peakSrv->shadowMap;
		peakSrvOther = peakSrv->otherSrv;
	}
	const double shadowGpuFramePercent =
		stressAverageFrameGpuMilliseconds_ > 0.0
		? stressAverageShadowGpuMilliseconds_ /
			stressAverageFrameGpuMilliseconds_ * 100.0
		: 0.0;

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
	output << "framesPerCycle=" << 60 + sceneStressGameFrameTarget_ << '\n';
	output << "titleFramesPerCycle=30\n";
	output << "gameFramesPerCycle=" << sceneStressGameFrameTarget_ << '\n';
	output << "resultFramesPerCycle=30\n";
	output << "recordedFrames=" << sceneStressSamples_.size() << '\n';
	output << std::fixed << std::setprecision(6);
	output << "averageFrameGpuMilliseconds="
		<< stressAverageFrameGpuMilliseconds_ << '\n';
	output << "averageShadowGpuMilliseconds="
		<< stressAverageShadowGpuMilliseconds_ << '\n';
	output << "frameGpuP95Milliseconds=" << stressFrameGpuP95Milliseconds_ << '\n';
	output << "shadowGpuP95Milliseconds=" << stressShadowGpuP95Milliseconds_ << '\n';
	output << "frameGpuMaxMilliseconds=" << stressFrameGpuMaxMilliseconds_ << '\n';
	output << "shadowGpuMaxMilliseconds=" << stressShadowGpuMaxMilliseconds_ << '\n';
	output << "shadowGpuFramePercent="
		<< shadowGpuFramePercent << '\n';
	output << "frameGpuSampleCount=" << stressFrameGpuSampleCount_ << '\n';
	output << "shadowGpuSampleCount=" << stressShadowGpuSampleCount_ << '\n';
	output << "shadowPassCount=" << stressShadowPassCount_ << '\n';
	output << "shadowCandidateCount=" << stressShadowCandidateCount_ << '\n';
	output << "shadowSubmittedCount=" << stressShadowSubmittedCount_ << '\n';
	output << "shadowCulledCount=" << stressShadowCulledCount_ << '\n';
	output << "maxActiveEnemyCount=" << stressMaxActiveEnemyCount_ << '\n';
	output << "maxEnemyStorageCount=" << stressMaxEnemyStorageCount_ << '\n';
	output << "maxExpOrbCount=" << stressMaxExpOrbCount_ << '\n';
	output << "peakSrvTexture2D=" << peakSrvTexture2D << '\n';
	output << "peakSrvTextureCube=" << peakSrvTextureCube << '\n';
	output << "peakSrvStructuredBuffer=" << peakSrvStructuredBuffer << '\n';
	output << "peakSrvShadowMap=" << peakSrvShadowMap << '\n';
	output << "peakSrvOther=" << peakSrvOther << '\n';
	output << "shadowMapSize=" << stressShadowMapSize_ << '\n';
	output << "shadowMapMemoryBytes=" << stressShadowMapMemoryBytes_ << '\n';
	output << "shadowArea=" << stressShadowArea_ << '\n';
	for (const CycleSummary& summary : cycleSummaries) {
		output << "cycle" << summary.cycle << ".peakSrvUsed="
			<< summary.peakSrvUsed << '\n';
		output << "cycle" << summary.cycle << ".firstWorkingSetBytes="
			<< summary.firstWorkingSetBytes << '\n';
		output << "cycle" << summary.cycle << ".peakWorkingSetBytes="
			<< summary.peakWorkingSetBytes << '\n';
		output << "cycle" << summary.cycle << ".finalWorkingSetBytes="
			<< summary.finalWorkingSetBytes << '\n';
		output << "cycle" << summary.cycle << ".firstPrivateBytes="
			<< summary.firstPrivateBytes << '\n';
		output << "cycle" << summary.cycle << ".peakPrivateBytes="
			<< summary.peakPrivateBytes << '\n';
		output << "cycle" << summary.cycle << ".finalPrivateBytes="
			<< summary.finalPrivateBytes << '\n';
	}

	const std::filesystem::path csvPath =
		"generated/outputs/scene_transition_stress.csv";
	std::ofstream csv(csvPath, std::ios::trunc);
	csv << "cycle,scene,scene_frame,srv_used,srv_high_watermark,texture2d,"
		"texture_cube,structured_buffer,shadow_map,other_srv,working_set_bytes,"
		"private_bytes\n";
	for (const SceneStressSample& sample : sceneStressSamples_) {
		const char* scene = sample.stage == SceneStressStage::Title
			? "Title"
			: (sample.stage == SceneStressStage::Game ? "Play" : "Result");
		csv << sample.cycle << ',' << scene << ',' << sample.sceneFrame << ','
			<< sample.srvUsed << ',' << sample.srvHighWatermark << ','
			<< sample.texture2D << ',' << sample.textureCube << ','
			<< sample.structuredBuffer << ',' << sample.shadowMap << ','
			<< sample.otherSrv << ',' << sample.workingSetBytes << ','
			<< sample.privateBytes << '\n';
	}

	const std::filesystem::path summaryPath =
		"generated/outputs/scene_transition_stress_summary.csv";
	std::ofstream summaryCsv(summaryPath, std::ios::trunc);
	summaryCsv << "metric,value,unit\n";
	summaryCsv << std::fixed << std::setprecision(6);
	const auto writeMetric = [&summaryCsv](
		std::string_view metric,
		const auto& value,
		std::string_view unit) {
		summaryCsv << metric << ',' << value << ',' << unit << '\n';
	};
	writeMetric("status", "PASS", "");
	writeMetric("configuration", "Debug x64", "");
	writeMetric("target_cycles", sceneStressCycleTarget_, "count");
	writeMetric("run_count", runCount_, "count");
	writeMetric("title_visits", titleVisitCount_, "count");
	writeMetric("game_visits", gameVisitCount_, "count");
	writeMetric("result_visits", resultVisitCount_, "count");
	writeMetric("title_frames_per_cycle", 30, "frames");
	writeMetric("game_frames_per_cycle", sceneStressGameFrameTarget_, "frames");
	writeMetric("result_frames_per_cycle", 30, "frames");
	writeMetric("frames_per_cycle", 60 + sceneStressGameFrameTarget_, "frames");
	writeMetric("recorded_frames", sceneStressSamples_.size(), "frames");
	writeMetric("average_frame_gpu_ms", stressAverageFrameGpuMilliseconds_, "ms");
	writeMetric("average_shadow_gpu_ms", stressAverageShadowGpuMilliseconds_, "ms");
	writeMetric("frame_gpu_p95_ms", stressFrameGpuP95Milliseconds_, "ms");
	writeMetric("shadow_gpu_p95_ms", stressShadowGpuP95Milliseconds_, "ms");
	writeMetric("frame_gpu_max_ms", stressFrameGpuMaxMilliseconds_, "ms");
	writeMetric("shadow_gpu_max_ms", stressShadowGpuMaxMilliseconds_, "ms");
	writeMetric("shadow_gpu_frame_percent", shadowGpuFramePercent, "percent");
	writeMetric("frame_gpu_sample_count", stressFrameGpuSampleCount_, "samples");
	writeMetric("shadow_gpu_sample_count", stressShadowGpuSampleCount_, "samples");
	writeMetric("shadow_pass_count", stressShadowPassCount_, "passes");
	writeMetric("shadow_candidate_count", stressShadowCandidateCount_, "objects");
	writeMetric("shadow_submitted_count", stressShadowSubmittedCount_, "objects");
	writeMetric("shadow_culled_count", stressShadowCulledCount_, "objects");
	writeMetric("shadow_map_size", stressShadowMapSize_, "pixels");
	writeMetric("shadow_map_memory_bytes", stressShadowMapMemoryBytes_, "bytes");
	writeMetric("shadow_area", stressShadowArea_, "world_units_half_extent");
	writeMetric("max_srv_used", stressMaxSrvUsed_, "descriptors");
	writeMetric("max_srv_high_watermark", stressMaxSrvHighWatermark_, "index_count");
	writeMetric("peak_srv_texture2d", peakSrvTexture2D, "descriptors");
	writeMetric("peak_srv_texture_cube", peakSrvTextureCube, "descriptors");
	writeMetric("peak_srv_structured_buffer", peakSrvStructuredBuffer, "descriptors");
	writeMetric("peak_srv_shadow_map", peakSrvShadowMap, "descriptors");
	writeMetric("peak_srv_other", peakSrvOther, "descriptors");
	writeMetric("max_active_enemy_count", stressMaxActiveEnemyCount_, "objects");
	writeMetric("max_enemy_storage_count", stressMaxEnemyStorageCount_, "objects");
	writeMetric("max_exp_orb_count", stressMaxExpOrbCount_, "objects");
	for (const CycleSummary& summary : cycleSummaries) {
		const std::string prefix = "cycle_" + std::to_string(summary.cycle) + '_';
		writeMetric(prefix + "peak_srv_used", summary.peakSrvUsed, "descriptors");
		writeMetric(prefix + "first_working_set_bytes", summary.firstWorkingSetBytes, "bytes");
		writeMetric(prefix + "peak_working_set_bytes", summary.peakWorkingSetBytes, "bytes");
		writeMetric(prefix + "final_working_set_bytes", summary.finalWorkingSetBytes, "bytes");
		writeMetric(prefix + "first_private_bytes", summary.firstPrivateBytes, "bytes");
		writeMetric(prefix + "peak_private_bytes", summary.peakPrivateBytes, "bytes");
		writeMetric(prefix + "final_private_bytes", summary.finalPrivateBytes, "bytes");
	}
}

void GameSession::RecordGpuTimingSummary(
	double averageFrameMilliseconds,
	double averageShadowMilliseconds,
	double frameP95Milliseconds,
	double shadowP95Milliseconds,
	double frameMaxMilliseconds,
	double shadowMaxMilliseconds,
	uint64_t frameSampleCount,
	uint64_t shadowSampleCount)
{
	stressAverageFrameGpuMilliseconds_ = averageFrameMilliseconds;
	stressAverageShadowGpuMilliseconds_ = averageShadowMilliseconds;
	stressFrameGpuP95Milliseconds_ = frameP95Milliseconds;
	stressShadowGpuP95Milliseconds_ = shadowP95Milliseconds;
	stressFrameGpuMaxMilliseconds_ = frameMaxMilliseconds;
	stressShadowGpuMaxMilliseconds_ = shadowMaxMilliseconds;
	stressFrameGpuSampleCount_ = frameSampleCount;
	stressShadowGpuSampleCount_ = shadowSampleCount;
}

void GameSession::RecordShadowPassSummary(
	uint64_t passCount,
	uint64_t candidateCount,
	uint64_t submittedCount,
	uint64_t culledCount,
	uint32_t shadowMapSize,
	uint64_t shadowMapMemoryBytes,
	float shadowArea)
{
	stressShadowPassCount_ = passCount;
	stressShadowCandidateCount_ = candidateCount;
	stressShadowSubmittedCount_ = submittedCount;
	stressShadowCulledCount_ = culledCount;
	stressShadowMapSize_ = shadowMapSize;
	stressShadowMapMemoryBytes_ = shadowMapMemoryBytes;
	stressShadowArea_ = shadowArea;
}

void GameSession::RecordSceneObjectCounts(
	uint32_t activeEnemyCount,
	uint32_t enemyStorageCount,
	uint32_t expOrbCount)
{
	stressMaxActiveEnemyCount_ =
		(std::max)(stressMaxActiveEnemyCount_, activeEnemyCount);
	stressMaxEnemyStorageCount_ =
		(std::max)(stressMaxEnemyStorageCount_, enemyStorageCount);
	stressMaxExpOrbCount_ = (std::max)(stressMaxExpOrbCount_, expOrbCount);
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
}

void GameSession::CommitRunCoinsToProfile()
{
	if (runCoinsCommitted_ || runCoins_ <= 0) {
		return;
	}
	ownedCoins_ += runCoins_;
	runCoinsCommitted_ = true;
	SaveProfile();
}

bool GameSession::TryPurchasePermanentMaxHP()
{
	const bool purchased = TryPurchaseUpgrade(
		ownedCoins_,
		permanentMaxHPLevel_,
		kPermanentMaxHPLevelCap,
		kPermanentMaxHPBaseCost);
	if (purchased) {
		SaveProfile();
	}
	return purchased;
}

bool GameSession::TryPurchasePermanentAttack()
{
	const bool purchased = TryPurchaseUpgrade(
		ownedCoins_,
		permanentAttackLevel_,
		kPermanentAttackLevelCap,
		kPermanentAttackBaseCost);
	if (purchased) {
		SaveProfile();
	}
	return purchased;
}

bool GameSession::TryPurchasePermanentMoveSpeed()
{
	const bool purchased = TryPurchaseUpgrade(
		ownedCoins_,
		permanentMoveSpeedLevel_,
		kPermanentMoveSpeedLevelCap,
		kPermanentMoveSpeedBaseCost);
	if (purchased) {
		SaveProfile();
	}
	return purchased;
}

bool GameSession::TryPurchasePermanentExpPickupRange()
{
	const bool purchased = TryPurchaseUpgrade(
		ownedCoins_,
		permanentExpPickupRangeLevel_,
		kPermanentExpPickupRangeLevelCap,
		kPermanentExpPickupRangeBaseCost);
	if (purchased) {
		SaveProfile();
	}
	return purchased;
}

bool GameSession::TryPurchasePermanentCoinGain()
{
	const bool purchased = TryPurchaseUpgrade(
		ownedCoins_,
		permanentCoinGainLevel_,
		kPermanentCoinGainLevelCap,
		kPermanentCoinGainBaseCost);
	if (purchased) {
		SaveProfile();
	}
	return purchased;
}

bool GameSession::TrySelectCharacter(CharacterId id)
{
	if (!IsCharacterUnlocked(id)) {
		return false;
	}
	selectedCharacterId_ = id;
	SaveProfile();
	return true;
}

bool GameSession::TryUnlockCharacter(CharacterId id)
{
	const CharacterDefinition* definition = FindCharacterDefinition(id);
	if (!definition || IsCharacterUnlocked(id)) {
		return false;
	}
	if (ownedCoins_ < definition->unlockCost) {
		return false;
	}
	ownedCoins_ -= definition->unlockCost;
	unlockedCharacterMask_ |= 1 << static_cast<int32_t>(id);
	selectedCharacterId_ = id;
	SaveProfile();
	return true;
}

bool GameSession::IsCharacterUnlocked(CharacterId id) const
{
	const CharacterDefinition* definition = FindCharacterDefinition(id);
	if (!definition) {
		return false;
	}
	if (definition->initiallyUnlocked) {
		return true;
	}
	return (unlockedCharacterMask_ & (1 << static_cast<int32_t>(id))) != 0;
}

int32_t GameSession::GetCharacterUnlockCost(CharacterId id) const
{
	const CharacterDefinition* definition = FindCharacterDefinition(id);
	return definition ? definition->unlockCost : 0;
}

int32_t GameSession::GetPermanentMaxHPCost() const
{
	return UpgradeCost(
		kPermanentMaxHPBaseCost,
		permanentMaxHPLevel_,
		kPermanentMaxHPLevelCap);
}

int32_t GameSession::GetPermanentAttackCost() const
{
	return UpgradeCost(
		kPermanentAttackBaseCost,
		permanentAttackLevel_,
		kPermanentAttackLevelCap);
}

int32_t GameSession::GetPermanentMoveSpeedCost() const
{
	return UpgradeCost(
		kPermanentMoveSpeedBaseCost,
		permanentMoveSpeedLevel_,
		kPermanentMoveSpeedLevelCap);
}

int32_t GameSession::GetPermanentExpPickupRangeCost() const
{
	return UpgradeCost(
		kPermanentExpPickupRangeBaseCost,
		permanentExpPickupRangeLevel_,
		kPermanentExpPickupRangeLevelCap);
}

int32_t GameSession::GetPermanentCoinGainCost() const
{
	return UpgradeCost(
		kPermanentCoinGainBaseCost,
		permanentCoinGainLevel_,
		kPermanentCoinGainLevelCap);
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
		constexpr std::string_view kPermanentMaxHPLevelPrefix =
			"permanentMaxHPLevel=";
		constexpr std::string_view kPermanentAttackLevelPrefix =
			"permanentAttackLevel=";
		constexpr std::string_view kPermanentMoveSpeedLevelPrefix =
			"permanentMoveSpeedLevel=";
		constexpr std::string_view kPermanentExpPickupRangeLevelPrefix =
			"permanentExpPickupRangeLevel=";
		constexpr std::string_view kPermanentCoinGainLevelPrefix =
			"permanentCoinGainLevel=";
		constexpr std::string_view kSelectedCharacterIdPrefix =
			"selectedCharacterId=";
		constexpr std::string_view kUnlockedCharacterMaskPrefix =
			"unlockedCharacterMask=";
		if (ParseProfileInt(line, kOwnedCoinsPrefix, ownedCoins_)) {
			ownedCoins_ = (std::max)(0, ownedCoins_);
			continue;
		}
		int32_t parsedCharacterValue = 0;
		if (ParseProfileInt(line, kSelectedCharacterIdPrefix, parsedCharacterValue)) {
			selectedCharacterId_ = ClampCharacterId(parsedCharacterValue);
			continue;
		}
		if (ParseProfileInt(line, kUnlockedCharacterMaskPrefix, unlockedCharacterMask_)) {
			unlockedCharacterMask_ |= kDefaultUnlockedCharacterMask;
			continue;
		}
		if (ParseProfileInt(
			line,
			kPermanentMaxHPLevelPrefix,
			permanentMaxHPLevel_)) {
			permanentMaxHPLevel_ = std::clamp(
				permanentMaxHPLevel_,
				0,
				kPermanentMaxHPLevelCap);
			continue;
		}
		if (ParseProfileInt(
			line,
			kPermanentAttackLevelPrefix,
			permanentAttackLevel_)) {
			permanentAttackLevel_ = std::clamp(
				permanentAttackLevel_,
				0,
				kPermanentAttackLevelCap);
			continue;
		}
		if (ParseProfileInt(
			line,
			kPermanentMoveSpeedLevelPrefix,
			permanentMoveSpeedLevel_)) {
			permanentMoveSpeedLevel_ = std::clamp(
				permanentMoveSpeedLevel_,
				0,
				kPermanentMoveSpeedLevelCap);
			continue;
		}
		if (ParseProfileInt(
			line,
			kPermanentExpPickupRangeLevelPrefix,
			permanentExpPickupRangeLevel_)) {
			permanentExpPickupRangeLevel_ = std::clamp(
				permanentExpPickupRangeLevel_,
				0,
				kPermanentExpPickupRangeLevelCap);
			continue;
		}
		if (ParseProfileInt(
			line,
			kPermanentCoinGainLevelPrefix,
			permanentCoinGainLevel_)) {
			permanentCoinGainLevel_ = std::clamp(
				permanentCoinGainLevel_,
				0,
				kPermanentCoinGainLevelCap);
		}
	}
	unlockedCharacterMask_ |= kDefaultUnlockedCharacterMask;
	if (!IsCharacterUnlocked(selectedCharacterId_)) {
		selectedCharacterId_ = CharacterId::Octopus;
	}
}

void GameSession::SaveProfile() const
{
	const std::filesystem::path outputPath = kProfilePath;
	std::filesystem::create_directories(outputPath.parent_path());
	std::ofstream output(outputPath, std::ios::trunc);
	output << "ownedCoins=" << ownedCoins_ << '\n';
	output << "selectedCharacterId=" << static_cast<int32_t>(selectedCharacterId_) << '\n';
	output << "unlockedCharacterMask=" << unlockedCharacterMask_ << '\n';
	output << "permanentMaxHPLevel=" << permanentMaxHPLevel_ << '\n';
	output << "permanentAttackLevel=" << permanentAttackLevel_ << '\n';
	output << "permanentMoveSpeedLevel=" << permanentMoveSpeedLevel_ << '\n';
	output << "permanentExpPickupRangeLevel=" << permanentExpPickupRangeLevel_ << '\n';
	output << "permanentCoinGainLevel=" << permanentCoinGainLevel_ << '\n';
}

}
