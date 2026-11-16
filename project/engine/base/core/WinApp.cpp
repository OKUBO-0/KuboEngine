#include "WinApp.h"
#include "HResult.h"
#include "branding/EngineIcon.h"
#include <stdexcept>
#ifndef KUBO_RUNTIME_ONLY
#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"
// ImGui Win32 backend が提供する外部コールバック宣言。実装は imgui_impl_win32.cpp 側にある。
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif
#pragma comment(lib,"winmm.lib")

namespace Engine::Base {

LRESULT WinApp::WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	if (msg == WM_NCCREATE) {
		const auto* createStruct = reinterpret_cast<CREATESTRUCT*>(lparam);
		SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(createStruct->lpCreateParams));
	}
	WinApp* app = reinterpret_cast<WinApp*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
	// Do not destroy the Editor HWND before its document confirmation can draw.
	if (msg == WM_CLOSE && app && app->closeRequests_.OnClose()) return 0;

	#ifndef KUBO_RUNTIME_ONLY
	if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) {

		return true;

	}
	#endif
	//メッセージに応じて固有の処理を行う
	switch (msg) {
	#ifndef KUBO_RUNTIME_ONLY
	case WM_DPICHANGED:
		if (app) app->windowDpi_ = HIWORD(wparam);
		if (app && lparam && !app->fullscreen_ && !IsZoomed(hwnd) && !IsIconic(hwnd)) {
			const auto& bounds = *reinterpret_cast<const RECT*>(lparam);
			SetWindowPos(hwnd,nullptr,bounds.left,bounds.top,bounds.right-bounds.left,bounds.bottom-bounds.top,
				SWP_NOZORDER | SWP_NOACTIVATE);
		}
		return 0;
	#endif

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




WinApp::~WinApp() { Finalize(); }

#ifndef KUBO_RUNTIME_ONLY
UINT WinApp::GetWindowDpi() const noexcept
{
	if (windowDpi_ != 0) return windowDpi_;
	const UINT dpi = hwnd ? GetDpiForWindow(hwnd) : 0;
	return dpi ? dpi : USER_DEFAULT_SCREEN_DPI;
}
#endif

void WinApp::Initialize()
{
	if(hwnd || timerPeriodActive_ || comInitialized_)throw std::logic_error("WinApp is already initialized");
	try {
		if(timeBeginPeriod(1)!=TIMERR_NOERROR)throw std::runtime_error("timeBeginPeriod failed");
		timerPeriodActive_=true;
		const HRESULT hr = CoInitializeEx(0, COINIT_MULTITHREADED);
		ThrowIfFailed(hr, "CoInitializeEx");
		comInitialized_=true; // S_FALSE also owns one matching CoUninitialize.
		RegisterWindowClass();
		CreateMainWindow();
#ifndef KUBO_RUNTIME_ONLY
		if (!deferInitialShow_)
#endif
		ShowWindow(hwnd, SW_SHOW);
	} catch(...) {Finalize();throw;}
}

void WinApp::Finalize()
{
	if (hwnd && IsWindow(hwnd)) DestroyWindow(hwnd);
	hwnd = nullptr;
#ifndef KUBO_RUNTIME_ONLY
	windowDpi_ = 0;
#endif
	fullscreen_=false;
	if(timerPeriodActive_){timerPeriodActive_=false;timeEndPeriod(1);}
	if(comInitialized_){comInitialized_=false;CoUninitialize();}
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
	while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
		// WM_DESTROY posts WM_QUIT. Drain it before another frame can
		// attempt to present to a window destroyed by WM_CLOSE.
		if (msg.message == WM_QUIT) return true;
		TranslateMessage(&msg);
		DispatchMessage(&msg);

	}
	return false;
}

void WinApp::RegisterWindowClass()
{
	wc.lpfnWndProc = WindowProc;
	wc.lpszClassName = L"WindowClass";
	wc.hInstance = GetModuleHandle(nullptr);
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wc.hIcon = Engine::Branding::LoadEngineIcon(wc.hInstance);
	if(!RegisterClass(&wc)){
		const DWORD error=GetLastError();WNDCLASS existing{};
		// The class is process-owned and shared by same-engine windows. Never
		// silently borrow a foreign callback registered under this legacy name.
		if(error!=ERROR_CLASS_ALREADY_EXISTS || !GetClassInfo(wc.hInstance,wc.lpszClassName,&existing) || existing.lpfnWndProc!=WindowProc)
			ThrowIfFailed(HRESULT_FROM_WIN32(error?error:ERROR_GEN_FAILURE),"RegisterClass");
	}
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
	if(!hwnd){const DWORD error=GetLastError();ThrowIfFailed(HRESULT_FROM_WIN32(error?error:ERROR_GEN_FAILURE),"CreateWindow");}
}

}
