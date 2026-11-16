#include "engine/app/Application.h"
#include "engine/base/core/RunDiagnostics.h"
#include <Windows.h>
#include <shellapi.h>
#include <filesystem>
#include <memory>
#include <utility>
#include <cstdio>
#include <exception>
#include <cerrno>
#include <cstdint>
#include <cwchar>

// Windowsアプリのエントリーポイント（main関数に相当）
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
	std::filesystem::path projectPath;
	bool diagnostics=false;
#ifndef KUBO_RUNTIME_ONLY
	bool playerMode = false;
	bool enableMcp = false;
	std::unique_ptr<void, decltype(&CloseHandle)> hubReadyEvent(nullptr, &CloseHandle);
	wchar_t executableName[MAX_PATH]{};
	if (GetModuleFileNameW(nullptr, executableName, MAX_PATH) != 0) {
		playerMode = _wcsicmp(std::filesystem::path(executableName).filename().c_str(), L"KuboPlayer.exe") == 0;
	}
#endif
	int argumentCount = 0;
	LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
	if (arguments) {
		for (int index = 1; index + 1 < argumentCount; ++index) {
			if (std::wstring(arguments[index]) == L"--project") {
				projectPath = arguments[index + 1];
				break;
			}
		}
		if (argumentCount > 1) {
			for (int index = 1; index < argumentCount; ++index) {
				if(std::wstring(arguments[index])==L"--diagnostics")diagnostics=true;
				if (std::wstring(arguments[index]) == L"--player") {
				#ifndef KUBO_RUNTIME_ONLY
					playerMode = true;
				#endif
				}
				#ifndef KUBO_RUNTIME_ONLY
				if (std::wstring(arguments[index]) == L"--enable-mcp") enableMcp = true;
				if (std::wstring(arguments[index]) == L"--hub-ready-event" && index + 1 < argumentCount) {
					const wchar_t* value = arguments[++index];
					wchar_t* end = nullptr;
					errno = 0;
					const auto number = std::wcstoull(value, &end, 10);
					DWORD flags = 0;
					auto handle = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(number));
					if (!hubReadyEvent && value[0] >= L'0' && value[0] <= L'9' &&
						end && *end == L'\0' && errno == 0 && handle &&
						GetHandleInformation(handle, &flags) && (flags & HANDLE_FLAG_INHERIT) &&
						SetHandleInformation(handle, HANDLE_FLAG_INHERIT, 0))
						hubReadyEvent.reset(handle);
				}
				#endif
			}
		}
		LocalFree(arguments);
	}
	std::unique_ptr<Engine::App::Application> application;
#ifdef KUBO_RUNTIME_ONLY
	application = std::make_unique<Engine::App::PlayerApplication>(std::move(projectPath));
#else
	if (playerMode) {
		application = std::make_unique<Engine::App::PlayerApplication>(std::move(projectPath));
	} else {
		SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
		application = std::make_unique<Engine::App::EditorApplication>(std::move(projectPath));
	}
#endif
	#ifndef KUBO_RUNTIME_ONLY
	application->SetMcpAtStartup(enableMcp);
	if (!playerMode) application->SetEditorReadyEvent(hubReadyEvent.get());
	#endif
	application->SetDiagnosticOutput(diagnostics);
	try { return application->Run(); }
	catch (const Engine::Base::RunFailure& error) {
		Engine::Base::ReportRunFailure(error);
		#ifndef KUBO_RUNTIME_ONLY
		MessageBoxA(nullptr, "Operation and shutdown both failed. See the error output for both diagnostics.", "KuboEngine runtime error", MB_OK | MB_ICONERROR);
		#endif
		return 1;
	}
	catch (const std::exception& error) {
		std::fprintf(stderr, "KuboEngine fatal error: %s\n", error.what());
		OutputDebugStringA(error.what());
		#ifndef KUBO_RUNTIME_ONLY
		MessageBoxA(nullptr, error.what(), "KuboEngine runtime error", MB_OK | MB_ICONERROR);
		#endif
		return 1;
	}
	catch (...) {
		Engine::Base::ReportRunException("runtime error", std::current_exception());
		return 1;
	}
}
