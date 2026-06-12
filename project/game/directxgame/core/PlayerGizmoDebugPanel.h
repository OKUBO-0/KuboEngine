#pragma once

#include <cstdint>

namespace DirectXGame {

class Player;

class PlayerGizmoDebugPanel final {
public:
	void Draw(bool* open, Player* player);

private:
	int32_t operation_ = 0;
};

}
