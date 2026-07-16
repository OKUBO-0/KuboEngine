#include "GameAudioTuning.h"

#include "DataPaths.h"
#include <array>
#include <string>

namespace DirectXGame {
namespace {

constexpr std::array<AudioTuningEntry, 27> kEntries{ {
	{ "Title BGM", "title.bgm", 0.42f },
	{ "Gameplay BGM", "game.bgm", 0.34f },
	{ "Boss BGM", "boss.bgm", 0.40f },
	{ "Menu Select", "ui.select", 0.55f },
	{ "Menu Decide", "ui.decide", 0.72f },
	{ "Menu Back", "ui.back", 0.52f },
	{ "Game Start", "game.start", 0.72f },
	{ "Pause Toggle", "game.pauseToggle", 0.58f },
	{ "Level Up", "game.levelUp", 0.72f },
	{ "Game Over", "game.over", 0.78f },
	{ "Player Dodge", "player.dodge", 0.48f },
	{ "Shot", "combat.shot", 0.34f },
	{ "Enemy Hit", "combat.enemyHit", 0.44f },
	{ "Enemy Death", "combat.enemyDeath", 0.54f },
	{ "Player Damage", "combat.playerDamage", 0.76f },
	{ "EXP Pickup", "combat.expPickup", 0.45f },
	{ "Coin Gain", "combat.coinGain", 0.36f },
	{ "Boss Phase", "combat.bossPhase", 0.62f },
	{ "Boss Rush", "boss.rush", 0.58f },
	{ "Boss Slam", "boss.slam", 0.72f },
	{ "Boss Ink", "boss.ink", 0.64f },
	{ "Boss Defeat", "boss.defeat", 0.78f },
	{ "Weapon Lightning", "weapon.lightning", 0.48f },
	{ "Weapon Sword", "weapon.sword", 0.42f },
	{ "Weapon Aura", "weapon.aura", 0.34f },
	{ "Weapon Flame", "weapon.flame", 0.40f },
	{ "Result Finish", "result.finish", 0.72f },
} };

}

std::span<const AudioTuningEntry> GetGameAudioTuningEntries()
{
	return kEntries;
}

void LoadGameAudioTuning()
{
	LoadGameAudioTuning(UILayoutIO::LoadOrDefault(DataPaths::kDebugTuning, {}));
}

void LoadGameAudioTuning(const UILayoutIO::LayoutMap& tuning)
{
	GameAudioCache::SetMasterVolume(
		UILayoutIO::GetFloat(tuning, "audio.master", GameAudioCache::GetMasterVolume()));
	GameAudioCache::SetBusVolume(
		AudioBus::Bgm,
		UILayoutIO::GetFloat(tuning, "audio.bus.bgm", GameAudioCache::GetBusVolume(AudioBus::Bgm)));
	GameAudioCache::SetBusVolume(
		AudioBus::Se,
		UILayoutIO::GetFloat(tuning, "audio.bus.se", GameAudioCache::GetBusVolume(AudioBus::Se)));
	for (const AudioTuningEntry& entry : kEntries) {
		GameAudioCache::SetTunedVolume(
			entry.key,
			UILayoutIO::GetFloat(
				tuning,
				std::string("audio.") + entry.key,
				entry.fallbackVolume));
	}
}

void AppendGameAudioTuningEntries(std::vector<UILayoutIO::Entry>& entries)
{
	entries.push_back({ "audio.master", { GameAudioCache::GetMasterVolume() } });
	entries.push_back({ "audio.bus.bgm", { GameAudioCache::GetBusVolume(AudioBus::Bgm) } });
	entries.push_back({ "audio.bus.se", { GameAudioCache::GetBusVolume(AudioBus::Se) } });
	for (const AudioTuningEntry& entry : kEntries) {
		entries.push_back({
			std::string("audio.") + entry.key,
			{ GameAudioCache::GetTunedVolume(entry.key, entry.fallbackVolume) },
			});
	}
}

}
