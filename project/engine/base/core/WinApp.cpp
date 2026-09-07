#include "WinApp.h"
#include "HResult.h"
#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"
// ImGui Win32 backend が提供する外部コールバック宣言。実装は imgui_impl_win32.cpp 側にある。
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#pragma comment(lib,"winmm.lib")

namespace Engine::Base {

LRESULT WinApp::WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	if (msg == WM_NCCREATE) {
		const auto* createStruct = reinterpret_cast<CREATESTRUCT*>(lparam);
		SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(createStruct->lpCreateParams));
	}
	WinApp* app = reinterpret_cast<WinApp*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

	if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) {

		return true;

	}
	//メッセージに応じて固有の処理を行う
	switch (msg) {

		//ウィンドウが破壊されたら
	case WM_DESTROY:
		//OS対して、アプリの終了を伝える
		PostQuitMessage(0);
		return 0;
	case WM_SYSKEYDOWN:
		if (wparam == VK_RETURN && (lparam & (1 << 29)) != 0) {
			if (app) {
				app->ToggleFullscreen();
			}
			return 0;
		}
	}

	return DefWindowProc(hwnd, msg, wparam, lparam);
}




void WinApp::Initialize()
{
	timeBeginPeriod(1);
	HRESULT hr = CoInitializeEx(0, COINIT_MULTITHREADED);
	ThrowIfFailed(hr, "CoInitializeEx");
	RegisterWindowClass();
	CreateMainWindow();
	ShowWindow(hwnd, SW_SHOW);
}

void WinApp::Finalize()
{
	CloseWindow(hwnd);
	CoUninitialize();
}

void WinApp::ToggleFullscreen()
{
	SetFullscreen(!fullscreen_);
}

void WinApp::SetFullscreen(bool fullscreen)
{
	if (!hwnd || fullscreen_ == fullscreen) {
		return;
	}

	if (fullscreen) {
		windowedStyle_ = static_cast<DWORD>(GetWindowLong(hwnd, GWL_STYLE));
		GetWindowRect(hwnd, &windowedRect_);

		MONITORINFO monitorInfo{};
		monitorInfo.cbSize = sizeof(monitorInfo);
		const HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
		if (!GetMonitorInfo(monitor, &monitorInfo)) {
			return;
		}

		SetWindowLong(hwnd, GWL_STYLE, windowedStyle_ & ~WS_OVERLAPPEDWINDOW);
		SetWindowPos(
			hwnd,
			HWND_TOP,
			monitorInfo.rcMonitor.left,
			monitorInfo.rcMonitor.top,
			monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left,
			monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top,
			SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
		ShowWindow(hwnd, SW_MAXIMIZE);
		fullscreen_ = true;
		return;
	}

	SetWindowLong(hwnd, GWL_STYLE, windowedStyle_);
	SetWindowPos(
		hwnd,
		nullptr,
		windowedRect_.left,
		windowedRect_.top,
		windowedRect_.right - windowedRect_.left,
		windowedRect_.bottom - windowedRect_.top,
		SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
	ShowWindow(hwnd, SW_RESTORE);
	fullscreen_ = false;
}

bool WinApp::ProcessMessage()
{
	MSG msg{};
	if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);

	}
	if (msg.message == WM_QUIT) {

		return true;
	}
	return false;
}

void WinApp::RegisterWindowClass()
{
	wc.lpfnWndProc = WindowProc;
	wc.lpszClassName = L"WindowClass";
	wc.hInstance = GetModuleHandle(nullptr);
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	RegisterClass(&wc);
}

void WinApp::CreateMainWindow()
{
	RECT wrc = { 0,0,kClientWidth ,kClientHeight };
	windowedStyle_ = WS_OVERLAPPEDWINDOW;
	AdjustWindowRect(&wrc, windowedStyle_, false);
	hwnd = CreateWindow(
		wc.lpszClassName,
		L"KuboEngine",
		windowedStyle_,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		wrc.right - wrc.left,
		wrc.bottom - wrc.top,
		nullptr,
		nullptr,
		wc.hInstance,
		this);
}

}
