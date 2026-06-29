#pragma once

#include "Vector3.h"
#include "Vector4.h"
#include <cstdint>

namespace DirectXGame {

struct FloatingNumberEvent {
	Vector3 position{};
	int32_t value = 0;
	Vector4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
};

} // namespace DirectXGame
