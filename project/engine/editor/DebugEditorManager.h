#pragma once

#include "Vector2.h"
#include <cstddef>
#include <functional>

namespace Engine::Editor {

struct DebugSceneViewportState {
	bool drawn = false;
	bool inputActive = false;
	Vector2 min{ 0.0f, 0.0f };
	Vector2 size{ 0.0f, 0.0f };
};

struct DebugEditorMenuItem {
	const char* label = "";
	bool* open = nullptr;
};

struct DebugEditorMenuConfig {
	const DebugEditorMenuItem* windowItems = nullptr;
	size_t windowItemCount = 0;
	const DebugEditorMenuItem* editItems = nullptr;
	size_t editItemCount = 0;
	const DebugEditorMenuItem* objectItems = nullptr;
	size_t objectItemCount = 0;
	const char* saveLabel = nullptr;
	std::function<void()> onSave;
	const char* restoreLabel = nullptr;
	std::function<void()> onRestore;
	const char* helpText = nullptr;
	std::function<void()> onGoTitle;
	std::function<void()> onGoGame;
	std::function<void()> onGoResult;
	bool* windowSwitcher = nullptr;
};

class DebugEditorManager {
public:
	static void BuildDefaultDockLayout(unsigned int dockspaceId);
	static void DrawMainMenu(const DebugEditorMenuConfig& config);
	static void DrawWindowSwitcher(
		const char* title,
		bool* open,
		const DebugEditorMenuItem* items,
		size_t itemCount,
		const Vector2& defaultSize,
		std::function<void()> footer = {});
	static DebugSceneViewportState DrawSceneViewport(bool* open);
};

} // namespace Engine::Editor
