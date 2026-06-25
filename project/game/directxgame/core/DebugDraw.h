#pragma once

namespace DirectXGame {

class EnemyManager;
class Player;
class PlayerManager;

namespace DebugDraw {

void Queue(
		bool collisionEnabled,
		bool lightEnabled,
		const Player* player,
		const EnemyManager* enemyManager,
		const PlayerManager* playerManager);

}

}
