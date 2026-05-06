#include "game/directxgame/core/DirectXGameResourceProbe.h"
#include "game/directxgame/core/CsvReader.h"
#include "game/directxgame/core/DirectXGameDataPaths.h"
#include "game/directxgame/core/DirectXGameResourcePaths.h"
#include "game/directxgame/core/UILayoutIO.h"
#include "ModelManager.h"
#include "TextureManager.h"
#include <cassert>
#include <fstream>

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

	status.verified = true;
	return status;
}

}
