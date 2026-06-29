#pragma once

#include "GameAudioCache.h"
#include "UILayoutIO.h"
#include <span>
#include <string_view>
#include <vector>

namespace DirectXGame {

struct AudioTuningEntry {
	std::string_view label;
	std::string_view key;
	float fallbackVolume = 1.0f;
	SoundHandle liveHandle{};
};

std::span<const AudioTuningEntry> GetGameAudioTuningEntries();
void LoadGameAudioTuning();
void LoadGameAudioTuning(const UILayoutIO::LayoutMap& tuning);
void AppendGameAudioTuningEntries(std::vector<UILayoutIO::Entry>& entries);

}
