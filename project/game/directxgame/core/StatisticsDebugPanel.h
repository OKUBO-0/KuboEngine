#pragma once

#include "SoftCapTelemetry.h"
#include <array>
#include <cstdint>

namespace DirectXGame {

class EnemyManager;
class PlayerManager;

class StatisticsDebugPanel final {
public:
	void Draw(
		bool* open,
		const char* stateName,
		uint32_t gameFrameCount,
		bool autoSaveAllowed,
		EnemyManager* enemyManager,
		PlayerManager* playerManager);

private:
	SoftCapTelemetry softCapTelemetry_{};
	std::array<float, 120> fpsHistory_{};
	int32_t fpsHistoryOffset_ = 0;
};

}
