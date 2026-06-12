#pragma once

namespace DirectXGame {

class EnemyManager;
class Player;
class PlayerManager;

class GameplayDebugDraw final {
public:
	static void Queue(
		bool collisionEnabled,
		bool lightEnabled,
		const Player* player,
		const EnemyManager* enemyManager,
		const PlayerManager* playerManager);
};

}
