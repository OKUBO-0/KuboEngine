#pragma once

#include "game/directxgame/core/DirectXGameResourcePaths.h"
#include <string>
#include <string_view>

namespace DirectXGame::DataPaths {

inline constexpr char kPlayerStatus[] = "playerStatus.csv";
inline constexpr char kEnemyTypes[] = "enemyTypes.csv";
inline constexpr char kEnemySpawnSettings[] = "enemySpawnSettings.csv";
inline constexpr char kWeaponUpgradeSettings[] = "weaponUpgradeSettings.csv";
inline constexpr char kLevelupWeights[] = "levelupWeights.csv";
inline constexpr char kTitleLayout[] = "ui_layout_title.csv";
inline constexpr char kPauseLayout[] = "ui_layout_pause.csv";
inline constexpr char kLevelupLayout[] = "ui_layout_levelup.csv";
inline constexpr char kResultLayout[] = "ui_layout_result.csv";
inline constexpr char kHudLayout[] = "ui_layout_hud.csv";

inline std::string Resolve(std::string_view relativePath)
{
	return ResourcePaths::MakeDataPath(relativePath);
}

}
