#pragma once

#include "Vector3.h"
#include <functional>

namespace DirectXGame {

namespace DebugUI::Rendering {

void DrawLighting(
	bool* open,
	bool& debugCameraEnabled,
	Vector3& debugCameraPosition,
	Vector3& debugCameraRotation,
	bool& lightDebugDrawEnabled,
	bool& debugDrawEnabled,
	const std::function<void()>& updateDebugCamera);
void DrawOffscreen(bool* open);

}

}
