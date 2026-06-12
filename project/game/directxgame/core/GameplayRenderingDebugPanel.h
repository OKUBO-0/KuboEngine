#pragma once

#include "Vector3.h"
#include <functional>

namespace DirectXGame {

class GameplayRenderingDebugPanel final {
public:
	static void DrawLighting(
		bool* open,
		bool& debugCameraEnabled,
		Vector3& debugCameraPosition,
		Vector3& debugCameraRotation,
		bool& lightDebugDrawEnabled,
		bool& debugDrawEnabled,
		const std::function<void()>& updateDebugCamera);
	static void DrawOffscreen(bool* open);
};

}
