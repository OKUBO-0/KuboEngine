#include "DebugEditorManager.h"

#include "IconsFontAwesome5.h"
#include "OffscreenRenderManager.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace Engine::Editor {

namespace {

std::string g_hotReloadStatus;
std::unordered_map<std::string, bool> g_windowVisibility;
std::unordered_set<const bool*> g_appliedWindowPointers;
bool g_windowVisibilityLoaded = false;

std::filesystem::path GetWindowVisibilityPath()
{
	return std::filesystem::current_path() / ".debug_editor_windows.ini";
}

void EnsureWindowVisibilityLoaded()
{
	if (g_windowVisibilityLoaded) {
		return;
	}
	g_windowVisibilityLoaded = true;
	std::ifstream file(GetWindowVisibilityPath());
	std::string line;
	while (std::getline(file, line)) {
		const size_t separator = line.find('=');
		if (separator == std::string::npos) {
			continue;
		}
		g_windowVisibility[line.substr(0, separator)] = line.substr(separator + 1) == "1";
	}
}

void SaveWindowVisibilityFile()
{
	std::ofstream file(GetWindowVisibilityPath(), std::ios::trunc);
	for (const auto& [label, open] : g_windowVisibility) {
		file << label << '=' << (open ? '1' : '0') << '\n';
	}
}

void SaveWindowOpen(const char* label, bool open)
{
	EnsureWindowVisibilityLoaded();
	const std::string key = label ? label : "";
	if (const auto it = g_windowVisibility.find(key); it != g_windowVisibility.end() && it->second == open) {
		return;
	}
	g_windowVisibility[key] = open;
	SaveWindowVisibilityFile();
}

void ApplyPersistedWindowItems(const DebugEditorMenuItem* items, size_t count)
{
	EnsureWindowVisibilityLoaded();
	for (size_t index = 0; index < count; ++index) {
		if (!items[index].label || !items[index].open ||
			g_appliedWindowPointers.find(items[index].open) != g_appliedWindowPointers.end()) {
			continue;
		}
		if (const auto it = g_windowVisibility.find(items[index].label); it != g_windowVisibility.end()) {
			*items[index].open = it->second;
		}
		g_appliedWindowPointers.insert(items[index].open);
	}
}

void DrawMenuItems(const DebugEditorMenuItem* items, size_t count)
{
	for (size_t index = 0; index < count; ++index) {
		if (items[index].open) {
			if (ImGui::MenuItem(items[index].label, nullptr, items[index].open)) {
				SaveWindowOpen(items[index].label, *items[index].open);
			}
		}
	}
}

std::wstring QuotePowerShellString(const std::wstring& text)
{
	std::wstring quoted = L"'";
	for (wchar_t character : text) {
		if (character == L'\'') {
			quoted += L"''";
		} else {
			quoted += character;
		}
	}
	quoted += L"'";
	return quoted;
}

std::wstring BuildPseudoHotReloadCommand()
{
	wchar_t modulePathBuffer[MAX_PATH]{};
	GetModuleFileNameW(nullptr, modulePathBuffer, MAX_PATH);

	const std::filesystem::path exePath = modulePathBuffer;
	const std::filesystem::path configuration = exePath.parent_path().filename();
	const std::filesystem::path generatedRoot = exePath.parent_path().parent_path().parent_path();
	const std::filesystem::path workspaceRoot = generatedRoot.parent_path();
	const std::filesystem::path projectRoot = workspaceRoot / L"project";
	const std::filesystem::path solutionPath = projectRoot / L"KuboEngine.sln";
	const std::filesystem::path buildOutputDir = generatedRoot / L"outputs" / configuration;
	const std::filesystem::path runDir = generatedRoot / L"hotreload" / configuration;
	const std::filesystem::path restartExePath = runDir / L"KuboEngine.exe";
	const std::filesystem::path msbuildPath =
		L"C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\MSBuild\\Current\\Bin\\amd64\\MSBuild.exe";

	const DWORD pid = GetCurrentProcessId();
	std::wstring command = L"$pidToWait=" + std::to_wstring(pid) + L";";
	command += L"Wait-Process -Id $pidToWait -ErrorAction SilentlyContinue;";
	command += L"& " + QuotePowerShellString(msbuildPath.wstring()) + L" " + QuotePowerShellString(solutionPath.wstring()) +
		L" /p:Configuration=" + configuration.wstring() + L" /p:Platform=x64;";
	command += L"if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE };";
	command += L"New-Item -ItemType Directory -Path " + QuotePowerShellString(runDir.wstring()) + L" -Force | Out-Null;";
	command += L"robocopy " + QuotePowerShellString(buildOutputDir.wstring()) + L" " + QuotePowerShellString(runDir.wstring()) +
		L" /MIR /NFL /NDL /NJH /NJS /NP | Out-Null;";
	command += L"if ($LASTEXITCODE -ge 8) { exit $LASTEXITCODE };";
	command += L"Start-Process -FilePath " + QuotePowerShellString(restartExePath.wstring()) +
		L" -WorkingDirectory " + QuotePowerShellString(runDir.wstring()) + L";";
	return command;
}

} // namespace

void DebugEditorManager::BuildDefaultDockLayout(unsigned int dockspaceId)
{
	ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::DockBuilderRemoveNode(dockspaceId);
	ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
	ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->Size);

	ImGuiID dockMain = dockspaceId;
	ImGuiID dockBottom = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Down, 0.30f, nullptr, &dockMain);
	ImGuiID dockLeft = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, 0.63f, nullptr, &dockMain);
	ImGuiID dockRight = dockMain;
	ImGuiID dockRightTop = ImGui::DockBuilderSplitNode(dockRight, ImGuiDir_Up, 0.58f, nullptr, &dockRight);

	ImGui::DockBuilderDockWindow("Scene", dockLeft);
	ImGui::DockBuilderDockWindow("統計", dockRightTop);
	ImGui::DockBuilderDockWindow("シーン設定", dockRightTop);
	ImGui::DockBuilderDockWindow("キー操作デバッグ", dockBottom);
	ImGui::DockBuilderDockWindow("ライト設定", dockRight);
	ImGui::DockBuilderDockWindow("オーディオ", dockRight);
	ImGui::DockBuilderDockWindow("OffscreenRenderManager", dockRight);
	ImGui::DockBuilderDockWindow("シーン固有デバッグ", dockRight);
	ImGui::DockBuilderDockWindow("オブジェクトビュー / オブジェクト設定", dockRight);
	ImGui::DockBuilderDockWindow("パーティクルビュー / スプライトマネージャ", dockRight);
	ImGui::DockBuilderDockWindow("オフスクリーン設定", dockRight);
	ImGui::DockBuilderDockWindow("ギズモ", dockRight);
	ImGui::DockBuilderDockWindow("オブジェクトマネージャ", dockRight);
	ImGui::DockBuilderDockWindow("モーションエディター", dockRight);
	ImGui::DockBuilderDockWindow("コライダー/タグ管理", dockRight);
	ImGui::DockBuilderFinish(dockspaceId);
}

void DebugEditorManager::DrawMainMenu(const DebugEditorMenuConfig& config)
{
	ApplyPersistedWindowItems(config.windowItems, config.windowItemCount);
	ApplyPersistedWindowItems(config.editItems, config.editItemCount);
	ApplyPersistedWindowItems(config.objectItems, config.objectItemCount);
	if (!ImGui::BeginMainMenuBar()) {
		return;
	}

	if (ImGui::BeginMenu(ICON_FA_FILE " ファイル")) {
		if (config.saveLabel && config.onSave && ImGui::MenuItem(config.saveLabel)) {
			config.onSave();
		}
		if (config.restoreLabel && config.onRestore && ImGui::MenuItem(config.restoreLabel)) {
			config.onRestore();
		}
		if (ImGui::MenuItem("疑似ホットリロード")) {
			RequestPseudoHotReload();
		}
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu(ICON_FA_EDIT " 編集")) {
		DrawMenuItems(config.editItems, config.editItemCount);
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu(ICON_FA_EYE " 表示")) {
		if (ImGui::BeginMenu("ウィンドウ")) {
			DrawMenuItems(config.windowItems, config.windowItemCount);
			ImGui::EndMenu();
		}
		if (config.windowSwitcher) {
			ImGui::MenuItem("ウィンドウ表示切り替え", nullptr, config.windowSwitcher);
		}
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu(ICON_FA_CUBE " オブジェクト")) {
		DrawMenuItems(config.objectItems, config.objectItemCount);
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu(ICON_FA_QUESTION_CIRCLE " ヘルプ")) {
		if (config.helpText) {
			ImGui::TextUnformatted(config.helpText);
		}
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu(ICON_FA_GLOBE " シーン選択")) {
		if (config.onGoTitle && ImGui::MenuItem("タイトルへ移動")) {
			config.onGoTitle();
		}
		if (config.onGoGame && ImGui::MenuItem("ゲームへ移動")) {
			config.onGoGame();
		}
		if (config.onGoResult && ImGui::MenuItem("リザルトへ移動")) {
			config.onGoResult();
		}
		ImGui::EndMenu();
	}

	ImGui::EndMainMenuBar();
}

void DebugEditorManager::SaveWindowItems(const DebugEditorMenuItem* items, size_t itemCount)
{
	for (size_t index = 0; index < itemCount; ++index) {
		if (items[index].label && items[index].open) {
			SaveWindowOpen(items[index].label, *items[index].open);
		}
	}
}

void DebugEditorManager::DrawHotReloadButton()
{
	if (ImGui::Button("疑似ホットリロード")) {
		RequestPseudoHotReload();
	}
	if (!g_hotReloadStatus.empty()) {
		ImGui::TextUnformatted(g_hotReloadStatus.c_str());
	}
}

void DebugEditorManager::RequestPseudoHotReload()
{
	const std::wstring command = BuildPseudoHotReloadCommand();
	std::wstring commandLine = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -Command " + QuotePowerShellString(command);

	STARTUPINFOW startupInfo{};
	startupInfo.cb = sizeof(startupInfo);
	PROCESS_INFORMATION processInfo{};
	if (!CreateProcessW(
		nullptr,
		commandLine.data(),
		nullptr,
		nullptr,
		FALSE,
		CREATE_NO_WINDOW,
		nullptr,
		nullptr,
		&startupInfo,
		&processInfo)) {
		g_hotReloadStatus = "Failed to start pseudo hot reload.";
		return;
	}

	CloseHandle(processInfo.hThread);
	CloseHandle(processInfo.hProcess);
	g_hotReloadStatus = "Pseudo hot reload started. This instance will close.";
	PostQuitMessage(0);
}

void DebugEditorManager::DrawWindowSwitcher(
	const char* title,
	bool* open,
	const DebugEditorMenuItem* items,
	size_t itemCount,
	const Vector2& defaultSize,
	std::function<void()> footer)
{
	if (!open || !*open) {
		return;
	}

	ImGui::SetNextWindowSize(ImVec2(defaultSize.x, defaultSize.y), ImGuiCond_FirstUseEver);
	ImGui::Begin(title, open);
	DrawMenuItems(items, itemCount);
	if (footer) {
		ImGui::Separator();
		footer();
	}
	ImGui::End();
}

DebugSceneViewportState DebugEditorManager::DrawSceneViewport(bool* open)
{
	DebugSceneViewportState state{};
	if (open && !*open) {
		return state;
	}

	ImGui::SetNextWindowPos(ImVec2(288.0f, 12.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(430.0f, 300.0f), ImGuiCond_FirstUseEver);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	if (!ImGui::Begin("Scene", open)) {
		ImGui::End();
		ImGui::PopStyleVar();
		return state;
	}

	state.inputActive = ImGui::IsWindowHovered() || ImGui::IsWindowFocused();
	if (Engine::Base::OffscreenRenderManager* offscreen = Engine::Base::OffscreenRenderManager::GetInstance();
		offscreen && offscreen->HasImGuiSceneTexture()) {
		ImVec2 availableSize = ImGui::GetContentRegionAvail();
		constexpr float kSceneAspect = 16.0f / 9.0f;
		ImVec2 imageSize{ availableSize.x, availableSize.x / kSceneAspect };
		if (imageSize.y > availableSize.y) {
			imageSize.y = availableSize.y;
			imageSize.x = imageSize.y * kSceneAspect;
		}

		const float cursorX = ImGui::GetCursorPosX() + (availableSize.x - imageSize.x) * 0.5f;
		ImGui::SetCursorPosX(cursorX);
		const ImVec2 imageMin = ImGui::GetCursorScreenPos();
		state.drawn = imageSize.x > 0.0f && imageSize.y > 0.0f;
		state.min = { imageMin.x, imageMin.y };
		state.size = { imageSize.x, imageSize.y };

		ImGui::GetWindowDrawList()->AddRectFilled(
			imageMin,
			ImVec2(imageMin.x + imageSize.x, imageMin.y + imageSize.y),
			IM_COL32(0, 0, 0, 255));
		ImGui::Image(
			static_cast<ImTextureID>(offscreen->GetImGuiSceneTextureHandle().ptr),
			imageSize,
			ImVec2(0.0f, 0.0f),
			ImVec2(1.0f, 1.0f));
	}

	ImGui::End();
	ImGui::PopStyleVar();
	return state;
}

} // namespace Engine::Editor
