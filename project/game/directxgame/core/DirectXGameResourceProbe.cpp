#include "game/directxgame/core/DirectXGameResourceProbe.h"
#include "game/directxgame/core/CsvReader.h"
#include "game/directxgame/core/DirectXGameDataPaths.h"
#include "game/directxgame/core/DirectXGameResourcePaths.h"
#include "game/directxgame/core/UILayoutIO.h"
#include "ModelManager.h"
#include "TextureManager.h"
#include <cassert>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <vector>

namespace DirectXGame {

namespace {

std::vector<UILayoutIO::Entry> BuildDefaultHudLayout()
{
	return {
		{ "hpPosition", { 48.0f, 40.0f } },
		{ "hpSize", { 260.0f, 24.0f } },
		{ "expFramePosition", { 260.0f, 40.0f } },
		{ "expFrameSize", { 420.0f, 24.0f } },
		{ "expGaugePosition", { 260.0f, 40.0f } },
		{ "expGaugeSize", { 420.0f, 24.0f } },
		{ "lvLabelPosition", { 204.0f, 24.0f } },
		{ "lvLabelSize", { 48.0f, 48.0f } },
		{ "lvDigitsPosition", { 246.0f, 24.0f } },
		{ "timerPosition", { 1030.0f, 24.0f } },
		{ "timerScale", { 1.5f } },
	};
}

std::vector<std::string> BuildRequiredAssetPaths()
{
	std::vector<std::string> paths;
	const CsvReader::CsvTable manifest = CsvReader::LoadRows(DataPaths::kResourceManifest);
	for (const CsvReader::CsvRow& row : manifest) {
		if (row.size() < 2 || row[0] == "type") {
			continue;
		}
		if (row[0] == "texture") {
			paths.push_back(ResourcePaths::MakeTexturePath(row[1]));
		} else if (row[0] == "audio") {
			paths.push_back(ResourcePaths::MakeAudioPath(row[1]));
		} else if (row[0] == "data") {
			paths.push_back(ResourcePaths::MakeDataPath(row[1]));
		}
	}
	if (!paths.empty()) {
		return paths;
	}

	paths = {
		ResourcePaths::MakeTexturePath("white1x1.png"),
		ResourcePaths::MakeTexturePath("ui/title/title.png"),
		ResourcePaths::MakeTexturePath("ui/title/guideUI.png"),
		ResourcePaths::MakeTexturePath("ui/title/cursor.png"),
		ResourcePaths::MakeTexturePath("ui/game/start.png"),
		ResourcePaths::MakeTexturePath("ui/game/pause.png"),
		ResourcePaths::MakeTexturePath("ui/game/pause_arrow.png"),
		ResourcePaths::MakeTexturePath("ui/game/death.png"),
		ResourcePaths::MakeTexturePath("ui/game/levelup.png"),
		ResourcePaths::MakeTexturePath("ui/game/lvup_attack.png"),
		ResourcePaths::MakeTexturePath("ui/game/lvup_attack_icon.png"),
		ResourcePaths::MakeTexturePath("ui/game/lvup_maxhp.png"),
		ResourcePaths::MakeTexturePath("ui/game/lvup_maxhp_icon.png"),
		ResourcePaths::MakeTexturePath("ui/game/lvup_speed.png"),
		ResourcePaths::MakeTexturePath("ui/game/lvup_speed_icon.png"),
		ResourcePaths::MakeTexturePath("ui/game/lvup_heal.png"),
		ResourcePaths::MakeTexturePath("ui/game/lvup_heal_icon.png"),
		ResourcePaths::MakeTexturePath("ui/game/normal/icon.png"),
		ResourcePaths::MakeTexturePath("ui/game/orbit/icon.png"),
		ResourcePaths::MakeTexturePath("ui/game/orbit/add.png"),
		ResourcePaths::MakeTexturePath("ui/game/drone/icon.png"),
		ResourcePaths::MakeTexturePath("ui/game/drone/add.png"),
		ResourcePaths::MakeTexturePath("ui/game/lightning/icon.png"),
		ResourcePaths::MakeTexturePath("ui/game/lightning/add.png"),
		ResourcePaths::MakeTexturePath("ui/result/Result.png"),
		ResourcePaths::MakeTexturePath("ui/result/finish_ui.png"),
		ResourcePaths::MakeTexturePath("ui/number/numbers.png"),
		ResourcePaths::MakeAudioPath("se/se_exp.wav"),
		ResourcePaths::MakeAudioPath("se/se_pause.wav"),
		ResourcePaths::MakeAudioPath("se/se_death.wav"),
		ResourcePaths::MakeDataPath("playerStatus.csv"),
		ResourcePaths::MakeDataPath("weaponUpgradeSettings.csv"),
		ResourcePaths::MakeDataPath("enemySpawnSettings.csv"),
		ResourcePaths::MakeDataPath("levelupWeights.csv"),
		ResourcePaths::MakeDataPath("enemyTypes.csv"),
		ResourcePaths::MakeDataPath("debug_tuning.csv"),
	};

	const std::array<const char*, 4> weaponDirectories{ "normal", "orbit", "drone", "lightning" };
	for (const char* weaponDirectory : weaponDirectories) {
		for (int32_t level = 2; level <= 8; ++level) {
			paths.push_back(ResourcePaths::MakeTexturePath(
				std::string("ui/game/") + weaponDirectory + "/lv" + std::to_string(level) + ".png"));
		}
	}

	return paths;
}

bool ExistsRelativeToKnownRoots(const std::string& path)
{
	if (std::filesystem::exists(path)) {
		return true;
	}

	std::error_code error;
	const std::filesystem::path exeRelative =
		std::filesystem::current_path(error).parent_path() / path;
	return !error && std::filesystem::exists(exeRelative);
}

void VerifyRequiredAssets(DirectXGameResourceProbeStatus& status)
{
	const std::vector<std::string> requiredAssets = BuildRequiredAssetPaths();
	status.requiredAssetCount = requiredAssets.size();
	status.missingRequiredAssets.clear();
	for (const std::string& path : requiredAssets) {
		if (!ExistsRelativeToKnownRoots(path)) {
			status.missingRequiredAssets.push_back(path);
		}
	}
	status.requiredAssetsReady = status.missingRequiredAssets.empty();
}

}

const DirectXGameResourceProbeStatus& DirectXGameResourceProbe::Verify()
{
	static DirectXGameResourceProbeStatus status{};
	if (status.verified) {
		return status;
	}

	Engine::Base::TextureManager::GetInstance()->LoadTexture(
		ResourcePaths::MakeTexturePath("ui/title/title.png"));
	status.textureLoaded = true;

	Engine::Graphics3D::ModelManager::GetInstance()->LoadModelFromResourceRoot(
		ResourcePaths::GetModelResourceRoot(), "octopus.obj");
	status.modelLoaded = true;

	std::ifstream csvFile(ResourcePaths::MakeDataPath("playerStatus.csv"));
	assert(csvFile.is_open());
	status.csvOpened = true;

	const CsvReader::KeyValueMap playerStatus = CsvReader::LoadKeyValueMap(DataPaths::kPlayerStatus);
	const CsvReader::KeyValueMap weaponUpgrades = CsvReader::LoadKeyValueMap(DataPaths::kWeaponUpgradeSettings);
	const CsvReader::KeyValueMap spawnSettings = CsvReader::LoadKeyValueMap(DataPaths::kEnemySpawnSettings);
	const CsvReader::KeyValueMap levelupWeights = CsvReader::LoadKeyValueMap(DataPaths::kLevelupWeights);
	const CsvReader::CsvTable enemyTypes = CsvReader::LoadRows(DataPaths::kEnemyTypes);
	status.gameplayDataLoaded = !playerStatus.empty() && !weaponUpgrades.empty() &&
		!spawnSettings.empty() && !levelupWeights.empty() && !enemyTypes.empty();

	const UILayoutIO::LayoutMap titleLayout = UILayoutIO::LoadOrDefault(DataPaths::kTitleLayout, {});
	const UILayoutIO::LayoutMap pauseLayout = UILayoutIO::LoadOrDefault(DataPaths::kPauseLayout, {});
	const UILayoutIO::LayoutMap levelupLayout = UILayoutIO::LoadOrDefault(DataPaths::kLevelupLayout, {});
	const UILayoutIO::LayoutMap resultLayout = UILayoutIO::LoadOrDefault(DataPaths::kResultLayout, {});
	const UILayoutIO::LayoutMap hudLayout = UILayoutIO::LoadOrDefault(DataPaths::kHudLayout, BuildDefaultHudLayout());
	status.uiLayoutsLoaded =
		!titleLayout.empty() && !pauseLayout.empty() && !levelupLayout.empty() && !resultLayout.empty();
	status.hudLayoutReady = !hudLayout.empty() &&
		hudLayout.contains("hpPosition") &&
		hudLayout.contains("expFramePosition") &&
		hudLayout.contains("timerPosition");

	VerifyRequiredAssets(status);

	status.verified = true;
	return status;
}

}
