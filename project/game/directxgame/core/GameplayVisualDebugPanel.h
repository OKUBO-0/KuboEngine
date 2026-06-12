#pragma once

#include <functional>

namespace DirectXGame {

class ExpGauge;
class GameParticleEffects;
class HpGauge;
class KeyUI;
class MiniMap;
class PauseBuildHud;
class Player;
class Timer;

class GameplayVisualDebugPanel final {
public:
	static void Draw(
		bool* particleViewOpen,
		bool* spriteManagerOpen,
		bool uiInitialized,
		bool* debugDrawEnabled,
		float hitFlashTimer,
		float deathTimer,
		Player* player,
		GameParticleEffects& particleEffects,
		Timer& timer,
		HpGauge& hpGauge,
		ExpGauge& expGauge,
		KeyUI& keyUi,
		MiniMap& miniMap,
		PauseBuildHud& pauseBuildHud,
		const std::function<void()>& emitLevelUpConfetti);
};

}
