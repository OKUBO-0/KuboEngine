#pragma once

#include "Vector2.h"
#include "WinApp.h"
#include <Windows.h>

namespace DirectXGame::ScreenUtil {

inline bool gDebugSceneViewportEnabled = false;
inline bool gDebugSceneInputActive = false;
inline Vector2 gDebugSceneViewportMin{ 0.0f, 0.0f };
inline Vector2 gDebugSceneViewportSize{ 0.0f, 0.0f };

inline Vector2 GetClientSize()
{
	if (gDebugSceneViewportEnabled && gDebugSceneViewportSize.x > 0.0f && gDebugSceneViewportSize.y > 0.0f) {
		return {
			static_cast<float>(Engine::Base::WinApp::kClientWidth),
			static_cast<float>(Engine::Base::WinApp::kClientHeight),
		};
	}

	RECT clientRect{};
	HWND hwnd = GetActiveWindow();
	if (hwnd != nullptr && GetClientRect(hwnd, &clientRect)) {
		return {
			static_cast<float>(clientRect.right - clientRect.left),
			static_cast<float>(clientRect.bottom - clientRect.top),
		};
	}

	return {
		static_cast<float>(Engine::Base::WinApp::kClientWidth),
		static_cast<float>(Engine::Base::WinApp::kClientHeight),
	};
}

inline void SetDebugSceneViewport(const Vector2& min, const Vector2& size)
{
	gDebugSceneViewportEnabled = size.x > 0.0f && size.y > 0.0f;
	gDebugSceneViewportMin = min;
	gDebugSceneViewportSize = size;
}

inline void ClearDebugSceneViewport()
{
	gDebugSceneViewportEnabled = false;
	gDebugSceneViewportMin = { 0.0f, 0.0f };
	gDebugSceneViewportSize = { 0.0f, 0.0f };
	gDebugSceneInputActive = false;
}

inline Vector2 ToGamePosition(const Vector2& windowPosition)
{
	if (!gDebugSceneViewportEnabled || gDebugSceneViewportSize.x <= 0.0f || gDebugSceneViewportSize.y <= 0.0f) {
		return windowPosition;
	}

	return {
		(windowPosition.x - gDebugSceneViewportMin.x) *
			static_cast<float>(Engine::Base::WinApp::kClientWidth) / gDebugSceneViewportSize.x,
		(windowPosition.y - gDebugSceneViewportMin.y) *
			static_cast<float>(Engine::Base::WinApp::kClientHeight) / gDebugSceneViewportSize.y,
	};
}

inline bool IsInsideDebugSceneViewport(const Vector2& windowPosition)
{
	if (!gDebugSceneViewportEnabled) {
		return true;
	}

	return windowPosition.x >= gDebugSceneViewportMin.x &&
		windowPosition.y >= gDebugSceneViewportMin.y &&
		windowPosition.x <= gDebugSceneViewportMin.x + gDebugSceneViewportSize.x &&
		windowPosition.y <= gDebugSceneViewportMin.y + gDebugSceneViewportSize.y;
}

inline void SetDebugSceneInputActive(bool active)
{
	gDebugSceneInputActive = active;
}

inline bool IsDebugSceneInputActive()
{
	return gDebugSceneInputActive;
}

}
