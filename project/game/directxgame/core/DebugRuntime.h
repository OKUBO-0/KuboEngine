#pragma once

namespace DirectXGame {

class EnemyManager;
class GridPlane;
class PlayerManager;

struct RuntimeObjectStatus {
	bool playerLoaded = false;
	bool playerManagerLoaded = false;
	bool enemyManagerLoaded = false;
	bool gridPlaneLoaded = false;
	bool skyDomeLoaded = false;
	bool curtainLoaded = false;
};

namespace DebugUI::Runtime {

void DrawObjectManager(
		bool* open,
		const RuntimeObjectStatus& status,
		EnemyManager* enemyManager,
		const GridPlane* gridPlane);
void DrawMotionEditor(bool* open);
void DrawColliderManager(
		bool* open,
		bool* debugDrawEnabled,
		const EnemyManager* enemyManager,
		const PlayerManager* playerManager);

}

}
