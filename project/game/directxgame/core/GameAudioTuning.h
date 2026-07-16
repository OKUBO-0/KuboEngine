#pragma once

#include "GameAudioCache.h"
#include "UILayoutIO.h"
#include <span>
#include <vector>

namespace DirectXGame {

struct AudioTuningEntry {
	const char* label;
	const char* key;
	float fallbackVolume;
};

std::span<const AudioTuningEntry> GetGameAudioTuningEntries();
void LoadGameAudioTuning();
void LoadGameAudioTuning(const UILayoutIO::LayoutMap& tuning);
void AppendGameAudioTuningEntries(std::vector<UILayoutIO::Entry>& entries);

}
