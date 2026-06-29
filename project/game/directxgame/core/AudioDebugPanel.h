#pragma once

#include "GameAudioTuning.h"
#include <functional>
#include <span>
#include <string_view>

namespace DirectXGame {

namespace DebugUI::Audio {

void Draw(
		bool* open,
		std::span<const AudioTuningEntry> entries,
		const std::function<void()>& save = {},
		const std::function<void()>& reload = {});

}

}
