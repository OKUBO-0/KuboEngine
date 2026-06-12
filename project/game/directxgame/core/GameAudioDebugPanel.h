#pragma once

#include "game/directxgame/core/GameAudioTuning.h"
#include <functional>
#include <span>
#include <string_view>

namespace DirectXGame {

class GameAudioDebugPanel final {
public:
	static void Draw(
		bool* open,
		std::span<const AudioTuningEntry> entries,
		const std::function<void()>& save = {},
		const std::function<void()>& reload = {});
};

}
