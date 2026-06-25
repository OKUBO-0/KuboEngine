#include "game/directxgame/core/GameAudioTuning.h"
#include "game/directxgame/core/DataPaths.h"
#include <array>
#include <string>

namespace DirectXGame {

namespace {

constexpr std::array<AudioTuningEntry, 13> kEntries{ {
	{ "Title BGM", "title.bgm", 0.1f },
	{ "Title Select", "title.select", 1.0f },
	{ "Title Decide", "title.decide", 1.0f },
	{ "Game Start", "game.start", 1.0f },
	{ "Pause Toggle", "game.pauseToggle", 0.5f },
	{ "Level Up", "game.levelUp", 1.0f },
	{ "Player Death", "game.death", 1.0f },
	{ "Shot", "combat.shot", 1.0f },
	{ "Enemy Hit", "combat.enemyHit", 0.5f },
	{ "Enemy Death", "combat.enemyDeath", 1.0f },
	{ "Player Damage", "combat.playerDamage", 0.8f },
	{ "EXP Pickup", "combat.expPickup", 1.0f },
	{ "Result Finish", "result.finish", 1.0f },
} };

}

std::span<const AudioTuningEntry> GetGameAudioTuningEntries()
{
	return kEntries;
}

void LoadGameAudioTuning()
{
	LoadGameAudioTuning(
		UILayoutIO::LoadOrDefault(DataPaths::kDebugTuning, {}));
}

void LoadGameAudioTuning(const UILayoutIO::LayoutMap& tuning)
{
	GameAudioCache::SetMasterVolume(
		UILayoutIO::GetFloat(
			tuning,
			"audio.master",
			GameAudioCache::GetMasterVolume()));
	for (const AudioTuningEntry& entry : kEntries) {
		GameAudioCache::SetTunedVolume(
			entry.key,
			UILayoutIO::GetFloat(
				tuning,
				std::string("audio.") + std::string(entry.key),
				entry.fallbackVolume));
	}
}

void AppendGameAudioTuningEntries(std::vector<UILayoutIO::Entry>& entries)
{
	entries.push_back({
		"audio.master",
		{ GameAudioCache::GetMasterVolume() }
	});
	for (const AudioTuningEntry& entry : kEntries) {
		entries.push_back({
			std::string("audio.") + std::string(entry.key),
			{ GameAudioCache::GetTunedVolume(entry.key, entry.fallbackVolume) }
		});
	}
}

}
