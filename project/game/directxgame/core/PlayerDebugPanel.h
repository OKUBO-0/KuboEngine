#pragma once

namespace DirectXGame {

class Player;
class PlayerManager;

namespace DebugUI::PlayerPanel {

void Draw(
		bool* objectViewOpen,
		bool* objectSettingsOpen,
		Player* player,
		PlayerManager* playerManager);

}

}
