#pragma once

#include <functional>

namespace DirectXGame {

struct GameplayDebugWindowVisibility {
	bool windowSwitcher = false;
	bool sceneView = true;
	bool objectView = false;
	bool particleView = false;
	bool statisticsView = true;
	bool offscreenSettings = false;
	bool lightSettings = false;
	bool gizmo = false;
	bool objectManager = false;
	bool motionEditor = false;
	bool spriteManager = false;
	bool colliderTagManager = false;
	bool audio = false;
	bool keyInputDebug = true;
	bool sceneSettings = true;
	bool sceneSpecificDebug = false;
	bool objectSettings = false;

	bool operator==(const GameplayDebugWindowVisibility&) const = default;
};

class GameplayDebugEditorShell final {
public:
	static bool Draw(
		GameplayDebugWindowVisibility& windows,
		const std::function<void()>& save,
		const std::function<void()>& reload,
		const std::function<void()>& goToTitle,
		const std::function<void()>& goToResult);
};

}
