#pragma once

#include "Vector2.h"
#include "WinApp.h"
#include <Windows.h>

namespace DirectXGame::ScreenUtil {

inline Vector2 GetClientSize()
{
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

}
