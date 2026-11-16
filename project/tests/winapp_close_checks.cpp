#include "../engine/base/core/WinApp.h"
#include <iostream>
#include <stdexcept>
#include <string>

// Input backend is not initialized in this message-only window test.
// Deferred WM_CLOSE must be intercepted before even reaching this dependency.
static int backendCalls = 0;
LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM) { ++backendCalls; return 0; }
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

int main()
{
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    const auto className = L"KuboCloseChecks" + std::to_wstring(GetCurrentProcessId());
    WNDCLASSW windowClass{};
    windowClass.hInstance = instance;
    windowClass.lpszClassName = className.c_str();
    windowClass.lpfnWndProc = Engine::Base::WinApp::WindowProc;
    HWND editorWindow = nullptr, playerWindow = nullptr;
    Engine::Base::WinApp editor, player;
    try {
        Require(RegisterClassW(&windowClass) != 0, "Test window class registration failed");
        editor.SetCloseRequestDeferred(true);
        // Message-only windows have no visible surface and cannot steal focus.
        editorWindow = CreateWindowExW(0, className.c_str(), L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, &editor);
        playerWindow = CreateWindowExW(0, className.c_str(), L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, &player);
        Require(editorWindow && playerWindow, "Message-only test windows could not be created");
        const int callsBefore = backendCalls;
        SendMessageW(editorWindow, WM_CLOSE, 0, 0);
        SendMessageW(editorWindow, WM_CLOSE, 0, 0);
        Require(IsWindow(editorWindow) && editor.ConsumeCloseRequest() && !editor.ConsumeCloseRequest(),
            "Editor WM_CLOSE destroyed the HWND or did not latch a single request");
        Require(backendCalls == callsBefore, "Deferred Editor WM_CLOSE reached the input backend");
        // Cancellation consumes the request while retaining the live window.
        SendMessageW(editorWindow, WM_CLOSE, 0, 0);
        Require(editor.ConsumeCloseRequest() && IsWindow(editorWindow), "Cancelled close destroyed the Editor HWND");
        PostMessageW(playerWindow, WM_APP, 0, 0);
        PostMessageW(playerWindow, WM_APP+1, 0, 0);
        PostMessageW(playerWindow, WM_CLOSE, 0, 0);
        Require(player.ProcessMessage(), "Message pump did not drain queued close through WM_QUIT before next frame");
        Require(!IsWindow(playerWindow) && !player.ConsumeCloseRequest(), "Player no longer uses native close destruction");
        editor.SetCloseRequestDeferred(false);
        SendMessageW(editorWindow, WM_CLOSE, 0, 0);
        Require(!IsWindow(editorWindow), "Approved native close did not destroy the Editor HWND");
        UnregisterClassW(className.c_str(), instance);
        std::cout << "PASS: production WinApp WM_CLOSE dispatch, deferred live Editor HWND, cancellation and native Player destruction\n";
        return 0;
    } catch (const std::exception& error) {
        if (editorWindow && IsWindow(editorWindow)) DestroyWindow(editorWindow);
        if (playerWindow && IsWindow(playerWindow)) DestroyWindow(playerWindow);
        UnregisterClassW(className.c_str(), instance);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
