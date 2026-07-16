#pragma once

#include "GameAudioTuning.h"
#include <functional>
#include <span>

namespace DirectXGame::DebugUI::Audio {

void Draw(
	bool* open,
	std::span<const AudioTuningEntry> entries,
	const std::function<void()>& save = {},
	const std::function<void()>& reload = {});

}
