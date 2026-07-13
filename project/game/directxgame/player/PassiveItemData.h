#pragma once

#include "PassiveItemType.h"
#include "PlayerStats.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace DirectXGame {

struct PassiveItemDefinition {
	PassiveItemType type{};
	PlayerStatType statType{};
	std::string_view name;
	std::string_view effect;
	std::string_view iconPath;
	float amount = 0.0f;
};

inline constexpr int32_t kPassiveItemMaxLevel = 8;
inline constexpr size_t kMaxEquippedPassiveItems = 6;

inline constexpr std::array<PassiveItemDefinition, 18> kPassiveItemDefinitions{ {
	{ PassiveItemType::Damage, PlayerStatType::Damage, "攻撃の書", "ダメージ+10%", "ui/game/lvup/attack_icon.png", 0.10f },
	{ PassiveItemType::MaxHp, PlayerStatType::Damage, "生命の書", "最大HP+15", "ui/game/lvup/maxhp_icon.png", 15.0f },
	{ PassiveItemType::MoveSpeed, PlayerStatType::MovementSpeed, "俊足の書", "移動速度+8%", "ui/game/lvup/speed_icon.png", 0.08f },
	{ PassiveItemType::AttackSpeed, PlayerStatType::AttackSpeed, "連射の書", "攻撃速度+7.5%", "ui/game/lvup/icon_common_unknown.png", 0.075f },
	{ PassiveItemType::Duration, PlayerStatType::Duration, "持続の書", "持続時間+10%", "ui/game/lvup/icon_common_unknown.png", 0.10f },
	{ PassiveItemType::AreaSize, PlayerStatType::AreaSize, "巨大化の書", "攻撃サイズ+10%", "ui/game/lvup/icon_common_unknown.png", 0.10f },
	{ PassiveItemType::ProjectileSpeed, PlayerStatType::ProjectileSpeed, "弾速の書", "投射物速度+12%", "ui/game/lvup/icon_common_unknown.png", 0.12f },
	{ PassiveItemType::ProjectileCount, PlayerStatType::ProjectileCount, "数量の書", "発射数+1", "ui/game/lvup/icon_common_unknown.png", 1.0f },
	{ PassiveItemType::PickupRange, PlayerStatType::PickupRange, "吸引の書", "EXP取得範囲+25%", "ui/game/lvup/icon_common_unknown.png", 0.25f },
	{ PassiveItemType::ExpGain, PlayerStatType::ExpGain, "経験の書", "EXP獲得量+8%", "ui/game/lvup/icon_common_unknown.png", 0.08f },
	{ PassiveItemType::CoinGain, PlayerStatType::CoinGain, "金運の書", "コイン獲得量+10%", "ui/game/lvup/icon_common_unknown.png", 0.10f },
	{ PassiveItemType::CritChance, PlayerStatType::CritChance, "会心の書", "会心率+5%", "ui/game/lvup/icon_common_unknown.png", 0.05f },
	{ PassiveItemType::CritDamage, PlayerStatType::CritDamage, "必殺の書", "会心倍率+15%", "ui/game/lvup/icon_common_unknown.png", 0.15f },
	{ PassiveItemType::Armor, PlayerStatType::Armor, "防護の書", "Armor+0.08", "ui/game/lvup/icon_common_unknown.png", 0.08f },
	{ PassiveItemType::Evasion, PlayerStatType::Evasion, "回避の書", "回避+4%", "ui/game/lvup/icon_common_unknown.png", 0.04f },
	{ PassiveItemType::HpRegen, PlayerStatType::HpRegen, "再生の書", "HP回復+0.25/秒", "ui/game/lvup/heal_icon.png", 0.25f },
	{ PassiveItemType::LifeSteal, PlayerStatType::LifeSteal, "吸血の書", "吸血率+3%", "ui/game/lvup/icon_common_unknown.png", 0.03f },
	{ PassiveItemType::Knockback, PlayerStatType::Knockback, "吹き飛ばしの書", "ノックバック+12%", "ui/game/lvup/icon_common_unknown.png", 0.12f },
} };

inline constexpr size_t PassiveItemIndex(PassiveItemType type)
{
	return static_cast<size_t>(type);
}

inline constexpr const PassiveItemDefinition& GetPassiveItemDefinition(
	PassiveItemType type)
{
	return kPassiveItemDefinitions[PassiveItemIndex(type)];
}

} // namespace DirectXGame
