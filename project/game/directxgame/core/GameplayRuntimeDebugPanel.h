#pragma once

namespace DirectXGame {

class EnemyManager;
class PlayerManager;

struct RuntimeObjectStatus {
	bool playerLoaded = false;
	bool playerManagerLoaded = false;
	bool enemyManagerLoaded = false;
	bool gridPlaneLoaded = false;
	bool skyDomeLoaded = false;
	bool curtainLoaded = false;
};

class GameplayRuntimeDebugPanel final {
public:
	static void DrawObjectManager(
		bool* open,
		const RuntimeObjectStatus& status,
		EnemyManager* enemyManager);
	static void DrawMotionEditor(bool* open);
	static void DrawColliderManager(
		bool* open,
		bool* debugDrawEnabled,
		const EnemyManager* enemyManager,
		const PlayerManager* playerManager);
};

}
