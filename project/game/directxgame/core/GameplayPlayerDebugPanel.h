#pragma once

namespace DirectXGame {

class Player;
class PlayerManager;

class GameplayPlayerDebugPanel final {
public:
	static void Draw(
		bool* objectViewOpen,
		bool* objectSettingsOpen,
		Player* player,
		PlayerManager* playerManager);
};

}
