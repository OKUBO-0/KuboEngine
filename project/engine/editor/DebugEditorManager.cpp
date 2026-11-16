#include "DebugEditorManager.h"
#include "DocumentShortcuts.h"
#include "Logger.h"
#include "engine/base/ProjectPaths.h"
#include "EditorSettingsFile.h"
#include "RenderComponentInspector.h"
#include "AssetThumbnailCache.h"
#include <sstream>

#include "IconsFontAwesome5.h"
#include "OffscreenRenderManager.h"
#include "engine/assets/AssetRegistry.h"
#include "engine/assets/AssetImportJob.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <Windows.h>
#include <shellapi.h>
#include <commdlg.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <chrono>
#include <cwchar>
#include <optional>
#include <set>

#pragma comment(lib,"comdlg32.lib")

namespace Engine::Editor {

void DebugEditorManager::DrawConsole(bool* open,const std::function<void(const Base::Logger::Entry&)>& actions)
{
	if (open && !*open) {
		return;
	}
	ImGui::SetNextWindowSize({640.0f, 320.0f}, ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Console", open)) {
		ImGui::End();
		return;
	}
	namespace Logger = Engine::Base::Logger;
	static ImGuiTextFilter filter;
	static int minimumLevel = 0;
	static bool autoScroll = true;
	static uint64_t selectedSequence = 0;
	static uint64_t folderErrorSequence = 0;
	if (ImGui::Button(ICON_FA_TRASH "##ClearConsole")) {
		Logger::Clear();
	}
	if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Clear"); }
	ImGui::SameLine();
	const bool copy = ImGui::Button(ICON_FA_COPY "##CopyConsole");
	if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Copy filtered logs"); }
	ImGui::SameLine();
	ImGui::Checkbox("Auto-scroll", &autoScroll);
	ImGui::SameLine();
	const bool jumpToLatest = ImGui::Button("Latest");
	const bool wide = ImGui::GetWindowWidth() > 550.0f;
	if (wide) ImGui::SameLine();
	ImGui::SetNextItemWidth(140.0f);
	ImGui::Combo("##Level", &minimumLevel, "All levels\0Warning + Error\0Error\0");
	if (wide) ImGui::SameLine();
	filter.Draw("Filter", -60.0f);
	const auto entries = Logger::GetEntries();
	size_t counts[3]{};
	std::vector<const Logger::Entry*> matches;
	const Logger::Entry* selected = nullptr;
	for (const auto& entry : entries) {
		const auto level = static_cast<int>(entry.level);
		++counts[level];
		if (entry.sequence == selectedSequence) selected = &entry;
		if (level >= minimumLevel && filter.PassFilter(entry.message.c_str())) matches.push_back(&entry);
	}
	if (!selected) selectedSequence = 0;
	ImGui::Text("Info %zu   Warnings %zu   Errors %zu   /   %zu shown",counts[0],counts[1],counts[2],matches.size());
	std::string copiedText;
	if (copy) for (const auto* entry : matches) {
		copiedText += "[" + std::string(Logger::GetLevelName(entry->level)) + "] " + entry->message + '\n';
	}
    const bool actionable=selected && selected->target.project;
	const float detailHeight = selected ? (std::min)(actionable?150.0f:110.0f,ImGui::GetContentRegionAvail().y * 0.5f) : 0.0f;
	if (ImGui::BeginChild("ConsoleEntries", {0,-detailHeight}, 0, ImGuiWindowFlags_HorizontalScrollbar)) {
		const bool follow = jumpToLatest || (autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f);
		if (matches.empty()) ImGui::TextDisabled(entries.empty() ? "No log entries." : "No matching log entries.");
		ImGuiListClipper clipper;
		clipper.Begin(static_cast<int>(matches.size()),ImGui::GetTextLineHeightWithSpacing());
		if (follow && !matches.empty()) clipper.IncludeItemByIndex(static_cast<int>(matches.size()-1));
		while (clipper.Step()) for (int index=clipper.DisplayStart; index<clipper.DisplayEnd; ++index) {
			const auto& entry = *matches[static_cast<size_t>(index)];
			const auto firstLine = entry.message.substr(0,entry.message.find_first_of("\r\n"));
			const std::string line = "[" + std::string(Logger::GetLevelName(entry.level)) + "] " + firstLine;
			const ImVec4 color = entry.level == Logger::Level::Error ? ImVec4(1.0f,0.4f,0.4f,1.0f) :
				entry.level == Logger::Level::Warning ? ImVec4(1.0f,0.8f,0.35f,1.0f) : ImGui::GetStyleColorVec4(ImGuiCol_Text);
			ImGui::PushID(static_cast<int>(entry.sequence));
			ImGui::PushStyleColor(ImGuiCol_Text,color);
			const auto start = ImGui::GetCursorScreenPos();
			if (ImGui::Selectable("##LogEntry",entry.sequence == selectedSequence,0,{ImGui::CalcTextSize(line.c_str(),nullptr,false).x,0})) {
				selectedSequence = entry.sequence;
			}
#if defined(KUBO_APPLICATION_CHECKS)
            if(entry.target.project)ObserveRenderInspectorChecks("Diagnostic Console entry");
#endif
			ImGui::GetWindowDrawList()->AddText(start,ImGui::GetColorU32(ImGuiCol_Text),line.c_str());
			ImGui::PopStyleColor();
			ImGui::PopID();
			if (follow && static_cast<size_t>(index+1) == matches.size()) ImGui::SetScrollHereY(1.0f);
		}
	}
	ImGui::EndChild();
	if (selected && detailHeight > 0.0f) {
		if (ImGui::BeginChild("ConsoleDetail",{0,0},ImGuiChildFlags_Borders)) {
			ImGui::Text("#%llu  %s",static_cast<unsigned long long>(selected->sequence),Logger::GetLevelName(selected->level));
			ImGui::SameLine();
			if (ImGui::Button("Copy entry")) ImGui::SetClipboardText(selected->message.c_str());
			if (!selected->sourceFile.empty()) {
				ImGui::SameLine();
				if (ImGui::Button("Open source folder")) {
					const auto folder = selected->sourceFile.parent_path();
					std::error_code error;
					const bool opened = std::filesystem::is_directory(folder,error) &&
						reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr,L"explore",folder.c_str(),nullptr,nullptr,SW_SHOWNORMAL)) > 32;
					folderErrorSequence = opened ? 0 : selected->sequence;
				}
				if (ImGui::Button("Copy source path")) ImGui::SetClipboardText(Engine::Base::PathToUtf8(selected->sourceFile).c_str());
				ImGui::TextWrapped("%s",Engine::Base::PathToUtf8(selected->sourceFile).c_str());
				if (folderErrorSequence == selected->sequence) ImGui::TextDisabled("The source folder could not be opened.");
			}
            if(actionable && actions)actions(*selected);
			ImGui::TextWrapped("%s",selected->message.c_str());
		}
		ImGui::EndChild();
	}
	if (copy) { ImGui::SetClipboardText(copiedText.c_str()); }
	ImGui::End();
}

static std::vector<std::filesystem::path> PickImportFiles(const std::filesystem::path& initialFolder,std::string& error)
{
	std::vector<wchar_t> paths(32768);
	OPENFILENAMEW dialog{};
	dialog.lStructSize = sizeof(dialog);
	dialog.hwndOwner = GetActiveWindow();
	dialog.lpstrFilter = L"Asset files\0*.*\0";
	dialog.lpstrFile = paths.data();
	dialog.nMaxFile = static_cast<DWORD>(paths.size());
	dialog.lpstrInitialDir = initialFolder.c_str();
	dialog.lpstrTitle = L"Import files into the project";
	dialog.Flags = OFN_EXPLORER | OFN_ALLOWMULTISELECT | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
	if (!GetOpenFileNameW(&dialog)) {
		if (const auto failure = CommDlgExtendedError())
			error = "Cannot select import files (dialog error " + std::to_string(failure) + ")";
		return {};
	}
	const auto* next = paths.data() + std::wcslen(paths.data()) + 1;
	std::filesystem::path first(paths.data());
	if (!*next) return {std::move(first)};
	std::vector<std::filesystem::path> result;
	while (*next) {
		result.push_back(first / next);
		next += std::wcslen(next) + 1;
	}
	return result;
}

void DebugEditorManager::DrawAssetBrowser(bool* open, const std::filesystem::path& projectAssetRoot, Engine::Assets::AssetRegistry* sharedRegistry,
    Engine::Assets::AssetImportJob* sharedJob, const char* refreshBlockedReason, bool allowMoves, const std::filesystem::path& owningProjectRoot,
    const std::filesystem::path& projectFile, std::function<std::string(const std::string&)> moveGuard, AssetThumbnailCache* thumbnails,
    std::function<void(const std::filesystem::path&)> openScene,std::string* requestedReveal)
{
	const bool allowRefresh = !refreshBlockedReason;
	static char search[128] = {};
	static int type = 0;
	static Engine::Assets::AssetRegistry fallbackRegistry;
	static Engine::Assets::AssetDependencies dependencies;
	auto& registry = sharedRegistry ? *sharedRegistry : fallbackRegistry;
	static Engine::Assets::AssetImportJob fallbackJob;
	auto& job = sharedJob ? *sharedJob : fallbackJob;
    static std::filesystem::path scannedRoot, scannedProject, scannedProjectFile;
	static Engine::Assets::AssetImportJob* lastJob = nullptr;
	static std::string selectedGuid;
	static std::string revealGuid;
	static std::string folder;
	static bool includeSubfolders = true;
	static Engine::Assets::AssetMoveRequest pendingMove;
	static char moveDestination[1024]{};
	bool openMoveDialog = false;
	static bool recoveryDialog = false;
	static std::string scanError, scanStatus;
	static std::chrono::steady_clock::time_point lastScan{};
	static bool readOnly = false, refreshPending = false, autoRefresh = true;
	static const char* typeNames[] = {"All", "Models", "Textures", "Shaders", "Audio", "Scenes", "Scripts", "Materials", "Prefabs", "Controllers"};
	const auto projectRoot = owningProjectRoot.empty() ? std::filesystem::current_path() : owningProjectRoot;
	auto root = projectAssetRoot.empty() ? projectRoot / "Assets" : projectAssetRoot;
	std::error_code rootError;
	if (projectAssetRoot.empty() && !std::filesystem::is_directory(root, rootError)) { root = projectRoot / "Resources"; }
	root = std::filesystem::absolute(root).lexically_normal();
	const bool changedOwner = lastJob != &job;
    if (changedOwner || scannedRoot != root || scannedProject != projectRoot || scannedProjectFile != projectFile) {
		if (!changedOwner) job.Cancel();
		// Preserve Game's startup snapshot on first use; a root switch invalidates it.
		if (!changedOwner) registry = {};
		selectedGuid.clear(); revealGuid.clear(); scanError.clear(); scanStatus.clear(); readOnly = false;
		folder.clear(); search[0] = '\0';
		pendingMove = {};
		dependencies = {};
		recoveryDialog = false;
        scannedRoot = root; scannedProject = projectRoot; scannedProjectFile = projectFile; lastJob = &job;
		refreshPending = true;
	}
	Engine::Assets::AssetImportJob::Result result;
    if (job.Poll(result) && result.Matches({projectRoot, root, {}, false, projectFile})) {
		lastScan = std::chrono::steady_clock::now();
        if (result.ApplyTo(registry, {projectRoot, root, {}, false, projectFile}, &dependencies)) {
			scanError.clear(); readOnly = result.readOnly;
			scanStatus = "Imported: " + std::to_string(result.imported) + ", unchanged/read-only: " + std::to_string(result.skipped);
			if (result.copied) scanStatus = "Copied " + std::to_string(result.copied) + " file(s). " + scanStatus;
		} else if (result.copied) {
			scanError = "Copied " + std::to_string(result.copied) + " file(s); refresh to load them. " + result.error;
			autoRefresh = false;
		} else if (result.moveCommitted || result.moveRollbackFailed || result.recoveryCommitted) {
			if (result.moveRollbackFailed) { registry = {}; dependencies = {}; }
			scanError = result.error;
			scanStatus = result.recoveryCommitted ? "Recovery completed, but registry refresh did not complete. Refresh again." : result.moveCommitted ? "Asset moved, but registry refresh did not complete. Refresh again."
				: "Asset move rollback failed. Check recovery paths in the error before retrying.";
			autoRefresh = false;
		} else if (result.cancelled) { scanStatus = "Cancelled (previous registry retained)"; }
		else { scanError = result.error; scanStatus = "Failed (previous registry retained)"; }
	}
	// Consume completion even when hidden, but do not schedule new hidden-window work.
	if (open && !*open) return;
    if(requestedReveal && !requestedReveal->empty())ImGui::SetNextWindowFocus();
	ImGui::SetNextWindowSize({620.0f, 420.0f}, ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Asset Browser", open)) { ImGui::End(); return; }
    if(requestedReveal && !requestedReveal->empty()){
        if(const auto* asset=registry.FindByGuid(*requestedReveal)){
            selectedGuid=revealGuid=asset->guid;
            const auto slash=asset->relativePath.find_last_of('/');
            folder=slash==std::string::npos?std::string{}:asset->relativePath.substr(0,slash);
            type=0;
            search[0]='\0';
        }
        requestedReveal->clear();
    }
	ImGui::SetNextItemWidth((std::min)(250.0f,ImGui::GetContentRegionAvail().x * 0.65f));
	ImGui::InputTextWithHint("##AssetSearch","Search in this folder",search,sizeof(search));
	ImGui::SameLine(); ImGui::SetNextItemWidth((std::min)(140.0f,ImGui::GetContentRegionAvail().x));
	ImGui::Combo("##AssetType", &type, typeNames, IM_ARRAYSIZE(typeNames));
	const std::string prefix = folder.empty() ? "" : folder + '/';
	std::set<std::string> subfolders;
	for (const auto& asset : registry.GetAssets()) {
		if (!asset.relativePath.starts_with(prefix)) continue;
		const auto slash = asset.relativePath.find('/',prefix.size());
		if (slash != std::string::npos) subfolders.insert(asset.relativePath.substr(prefix.size(),slash-prefix.size()));
	}
	std::optional<std::string> nextFolder;
	const auto rootLabel = Engine::Base::PathToUtf8(root.filename());
	ImGui::PushID("Asset breadcrumbs");
	if (ImGui::SmallButton(rootLabel.c_str())) nextFolder = "";
	const auto rowSpace = [] {
		return ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x - ImGui::GetItemRectMax().x;
	};
	for (size_t begin=0; begin<folder.size();) {
		const auto end = (std::min)(folder.find('/',begin),folder.size());
		if (rowSpace() > ImGui::CalcTextSize(folder.data()+begin,folder.data()+end).x + 20.0f) ImGui::SameLine();
		ImGui::PushID(static_cast<int>(begin));
		if (ImGui::SmallButton(folder.substr(begin,end-begin).c_str())) nextFolder = folder.substr(0,end);
		ImGui::PopID();
		begin = end+1;
	}
	ImGui::PopID();
	if (rowSpace() > 138.0f) ImGui::SameLine();
	ImGui::SetNextItemWidth(130.0f);
	if (ImGui::BeginCombo("##AssetFolders","Subfolders")) {
		if (subfolders.empty()) ImGui::TextDisabled("No subfolders with assets.");
		for (const auto& child : subfolders) if (ImGui::Selectable(child.c_str())) nextFolder = prefix + child;
		ImGui::EndCombo();
	}
	if (rowSpace() > 155.0f) ImGui::SameLine();
	ImGui::Checkbox("Include subfolders",&includeSubfolders);
	if (nextFolder) folder = std::move(*nextFolder);
	const bool canImport = sharedRegistry && allowRefresh && allowMoves && !readOnly && !job.IsBusy() &&
		!recoveryDialog && pendingMove.guid.empty();
	ImGui::BeginDisabled(!canImport);
	if (ImGui::Button("Import Files...")) {
		scanError.clear();
		auto files = PickImportFiles(root / Engine::Base::PathFromUtf8(folder),scanError);
		if (!files.empty()) {
			if (job.Start({projectRoot,root,{},false,projectFile,std::move(files),folder})) {
				refreshPending = false;
				scanStatus = "Copying selected files...";
			} else scanError = "Cannot start file import worker";
		}
	}
	ImGui::EndDisabled();
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Copy selected files into the current asset folder and register them.");
	if (rowSpace() > 155.0f) ImGui::SameLine();
	const bool importDetails = ImGui::CollapsingHeader("Import and recovery");
	const auto now = std::chrono::steady_clock::now();
	if (importDetails) {
		ImGui::BeginDisabled(job.IsBusy() || !allowRefresh);
		if (ImGui::Button("Refresh")) refreshPending = true;
		ImGui::EndDisabled();
		ImGui::SameLine(); ImGui::Checkbox("Auto refresh (2s)", &autoRefresh);
		ImGui::SameLine();
		ImGui::BeginDisabled(job.IsBusy() || !allowRefresh || !allowMoves || readOnly || !pendingMove.guid.empty());
		if (ImGui::Button("Recover pending move...")) { recoveryDialog = true; ImGui::OpenPopup("Recover Asset Move"); }
		ImGui::EndDisabled();
	}
	if (ImGui::BeginPopupModal("Recover Asset Move", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		if (!recoveryDialog) ImGui::CloseCurrentPopup();
		ImGui::TextWrapped("Asset root: %s", Engine::Base::PathToUtf8(root).c_str());
		ImGui::TextWrapped("Explicitly recover the pending move for this asset root. Original file IDs and GUID metadata must match.");
		ImGui::TextWrapped("A split pair returns to its original path. An already-complete pair remains where it is. Competing, missing, replaced or legacy files stop recovery without deleting them.");
		ImGui::TextWrapped("This is not scene Undo/Redo. Cancellation after recovery commits cannot undo it. Back up the affected files before recovery.");
		ImGui::BeginDisabled(!recoveryDialog || job.IsBusy() || !allowRefresh || !allowMoves || readOnly);
		if (ImGui::Button("Verify and recover")) {
            if (job.Start({projectRoot, root, {}, true, projectFile})) {
				refreshPending = false; autoRefresh = false; scanError.clear(); scanStatus = "Recovering pending move...";
				recoveryDialog = false; ImGui::CloseCurrentPopup();
			} else scanError = "Cannot start asset recovery worker";
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Cancel recovery")) { recoveryDialog = false; ImGui::CloseCurrentPopup(); }
		ImGui::EndPopup();
	}
	if (refreshBlockedReason) ImGui::TextWrapped("%s", refreshBlockedReason);
	const auto* drag = ImGui::GetDragDropPayload();
	const bool placingAsset = drag && drag->IsDataType(AssetGuidPayload);
	if (allowRefresh && !placingAsset && !recoveryDialog && pendingMove.guid.empty() && !job.IsBusy() &&
		(refreshPending || (autoRefresh && now - lastScan > std::chrono::seconds(2)))) {
		lastScan = now;
        if (job.Start({projectRoot, root, {}, false, projectFile})) { refreshPending = false; scanStatus = "Working..."; }
		else scanError = "Cannot start asset worker";
	}
	if (job.IsBusy()) {
		ImGui::SameLine(); if (ImGui::Button("Cancel")) { job.Cancel(); refreshPending = false; autoRefresh = false; }
		const auto progress = job.GetProgress();
		static const char* stages[] = {"Validating paths", "Scanning assets", "Inspecting dependencies", "Moving/recovering asset", "Hashing content/dependencies", "Importing metadata", "Saving registry", "Staging import files", "Finished"};
		ImGui::Text("%s: %llu / %llu", stages[static_cast<int>(progress.stage)],
			static_cast<unsigned long long>(progress.completed), static_cast<unsigned long long>(progress.total));
	}
	if (readOnly) ImGui::TextUnformatted("External asset root: read-only metadata (copy assets into the project to import).");
	if (importDetails) {
		ImGui::Text("Inspected OBJ/MTL/glTF dependencies: %zu edges, %zu diagnostics",dependencies.edges.size(),dependencies.issues.size());
		ImGui::TextWrapped("Inspection covers OBJ mtllib, simple MTL texture paths and glTF core external buffers/images.");
		if (!scanStatus.empty()) ImGui::TextUnformatted(scanStatus.c_str());
	}
	if (!scanError.empty()) { ImGui::TextColored({1.0f, 0.45f, 0.35f, 1.0f}, "%s", scanError.c_str()); }
	ImGui::Separator();
	Engine::Assets::AssetType filter = Engine::Assets::AssetType::Unknown;
	if (type == 1) { filter = Engine::Assets::AssetType::Model; }
	if (type == 2) { filter = Engine::Assets::AssetType::Texture; }
	if (type == 3) { filter = Engine::Assets::AssetType::Shader; }
	if (type == 4) { filter = Engine::Assets::AssetType::Audio; }
	if (type == 5) { filter = Engine::Assets::AssetType::Scene; }
	if (type == 6) { filter = Engine::Assets::AssetType::Script; }
	if (type == 7) { filter = Engine::Assets::AssetType::Material; }
	if (type == 8) { filter = Engine::Assets::AssetType::Prefab; }
	if (type == 9) { filter = Engine::Assets::AssetType::AnimatorController; }
	auto matches = registry.Search(search, filter);
	const std::string scope = folder.empty() ? "" : folder + '/';
	std::erase_if(matches,[&](const auto* asset) {
		return !asset->relativePath.starts_with(scope) ||
			(!includeSubfolders && asset->relativePath.find('/',scope.size()) != std::string::npos);
	});
	std::optional<std::filesystem::path> sceneToOpen;
	const Engine::Assets::AssetRecord* revealAsset = nullptr;
	const auto* selectedAsset = registry.FindByGuid(selectedGuid);
	static bool showAssetDetails = false;
	if (selectedAsset) {
		ImGui::Checkbox("Asset details",&showAssetDetails);
		ImGui::SameLine();
		ImGui::TextUnformatted(selectedAsset->relativePath.c_str());
	}
	const float detailsHeight = selectedAsset && showAssetDetails ? (std::min)(100.0f,ImGui::GetContentRegionAvail().y * 0.4f) : 0.0f;
	ImGui::BeginChild("AssetList", {0.0f, -detailsHeight}, ImGuiChildFlags_Borders);
	if (nextFolder) ImGui::SetScrollY(0);
	if (matches.empty()) ImGui::TextDisabled(job.IsBusy() ? "Scanning assets..." : "No matching assets in this folder.");
	ImGuiListClipper clipper;clipper.Begin(static_cast<int>(matches.size()),68.0f+ImGui::GetStyle().ItemSpacing.y);
	if (!revealGuid.empty()) {
		const auto row = std::find_if(matches.begin(),matches.end(),[&](const auto* asset) {return asset->guid==revealGuid;});
		if (row!=matches.end()) clipper.IncludeItemByIndex(static_cast<int>(row-matches.begin()));
	}
	while(clipper.Step())for(int row=clipper.DisplayStart;row<clipper.DisplayEnd;++row){
		const auto* asset=matches[size_t(row)];
		const std::string label = asset->relativePath + " [" + Engine::Assets::AssetRegistry::GetTypeName(asset->type) + "]";
		ImGui::PushID(asset->guid.c_str());const auto start=ImGui::GetCursorScreenPos();
		const bool canOpen = asset->type == Engine::Assets::AssetType::Scene && bool(openScene);
		if (ImGui::Selectable("##Asset",selectedGuid == asset->guid,canOpen ? ImGuiSelectableFlags_AllowDoubleClick : 0,{0,68})) {
			selectedGuid = asset->guid;
			if (canOpen && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) sceneToOpen = Engine::Base::PathFromUtf8(asset->absolutePath);
		}
		if (asset->guid==revealGuid) ImGui::SetScrollHereY();
		using Type = Engine::Assets::AssetType;
		const auto* dragHint = asset->type == Type::Model ? "Drag into Scene to place, or onto the Inspector to assign" :
			asset->type == Type::Prefab ? "Drag into Scene to place" :
			asset->type == Type::Texture ? "Drag onto the Inspector to assign a Sprite image" : nullptr;
		const bool canDrag = dragHint && sharedRegistry && allowMoves && !job.IsBusy() && !readOnly;
		if (canDrag && ImGui::BeginDragDropSource()) {
			ImGui::SetDragDropPayload(AssetGuidPayload,asset->guid.data(),asset->guid.size());
			ImGui::TextUnformatted(asset->relativePath.c_str());
			ImGui::EndDragDropSource();
		}
		if (canDrag && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
			ImGui::SetTooltip("%s",dragHint);
		if (ImGui::BeginPopupContextItem("AssetActions")) {
			if (asset->type == Engine::Assets::AssetType::Scene && ImGui::MenuItem("Open Scene",nullptr,false,canOpen))
				sceneToOpen = Engine::Base::PathFromUtf8(asset->absolutePath);
			if (ImGui::MenuItem("Copy path")) { ImGui::SetClipboardText(asset->absolutePath.c_str()); }
			if (ImGui::MenuItem("Copy GUID")) { ImGui::SetClipboardText(asset->guid.c_str()); }
			if (ImGui::MenuItem("Rename / Move...", nullptr, false, !job.IsBusy() && !readOnly && allowRefresh && allowMoves)) {
				pendingMove = {asset->relativePath, asset->relativePath, asset->guid};
				strncpy_s(moveDestination, asset->relativePath.c_str(), _TRUNCATE);
				openMoveDialog = true;
			}
			ImGui::EndPopup();
		}
		const auto preview=thumbnails?thumbnails->Request(*asset):AssetThumbnailView{0,0,0,AssetThumbnailView::Status::Unsupported};
		auto* draw=ImGui::GetWindowDrawList();
		const ImVec2 imageMin{start.x+2,start.y+2},imageMax{start.x+66,start.y+66};
		draw->AddRectFilled(imageMin,imageMax,IM_COL32(28,30,35,255));
		if(preview.texture&&preview.width&&preview.height){
			const float scale=(std::min)(64.0f/float(preview.width),64.0f/float(preview.height));
			const ImVec2 size{preview.width*scale,preview.height*scale},minimum{imageMin.x+(64-size.x)/2,imageMin.y+(64-size.y)/2};
			draw->AddImage(static_cast<ImTextureID>(preview.texture),minimum,{minimum.x+size.x,minimum.y+size.y});
		}else{
			const auto* text=preview.status==AssetThumbnailView::Status::Pending?"...":preview.status==AssetThumbnailView::Status::Failed?"Failed":Engine::Assets::AssetRegistry::GetTypeName(asset->type);
			draw->AddText({imageMin.x+3,imageMin.y+23},ImGui::GetColorU32(ImGuiCol_TextDisabled),text);
		}
		draw->AddText({start.x+74,start.y+10},ImGui::GetColorU32(ImGuiCol_Text),label.c_str());
		#if defined(KUBO_APPLICATION_CHECKS)
		if(assetBrowserChecksObserver)assetBrowserChecksObserver(asset->guid.c_str(),preview.texture,{imageMin.x,imageMin.y},{imageMax.x,imageMax.y},selectedGuid==asset->guid);
		#endif
		if(preview.status==AssetThumbnailView::Status::Failed)draw->AddText({start.x+74,start.y+34},ImGui::GetColorU32(ImGuiCol_TextDisabled),"Preview unavailable");
		ImGui::PopID();
	}
	revealGuid.clear();
	ImGui::EndChild();
	if (openMoveDialog) ImGui::OpenPopup("Rename / Move Asset");
	if (ImGui::BeginPopupModal("Rename / Move Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::TextWrapped("Source: %s", pendingMove.sourceRelative.c_str());
		ImGui::TextWrapped("GUID: %s", pendingMove.guid.c_str());
		ImGui::InputText("Destination relative to asset root (UTF-8)", moveDestination, sizeof(moveDestination));
		ImGui::TextWrapped("Moves the file and persistent GUID metadata together. Destination folder must exist; no overwrite or case-only rename.");
        ImGui::TextWrapped("GUID references follow moves after refresh. Inspected Scene/Prefab Model, Script, Audio, Sprite and Particle paths block a move until assigned a GUID or rewritten. Animator Controller and Prefab source GUIDs are tracked too. Relative Model dependencies still need repair.");
		ImGui::TextWrapped("This file operation is not scene Undo/Redo. Cancelling after a completed move does not undo it.");
        auto moveRisk = dependencies.MoveRisk(registry, pendingMove.guid, moveDestination);
        if(moveRisk.empty() && moveGuard)moveRisk=moveGuard(pendingMove.guid);
		if (!moveRisk.empty()) ImGui::TextWrapped("Move blocked: %s", moveRisk.c_str());
        if(!moveRisk.empty())for(size_t i=0;i<dependencies.issues.size() && i<8;++i)ImGui::TextWrapped("Inspection: %s",dependencies.issues[i].message.c_str());
		ImGui::BeginDisabled(!moveRisk.empty() || pendingMove.guid.empty() || moveDestination[0] == '\0' || job.IsBusy() || !allowRefresh || !allowMoves || readOnly);
		if (ImGui::Button("Move file and metadata")) {
			pendingMove.destinationRelative = moveDestination;
            if (job.Start({projectRoot, root, pendingMove, false, projectFile})) {
				refreshPending = false; scanStatus = "Moving asset..."; scanError.clear();
				pendingMove = {}; ImGui::CloseCurrentPopup();
			} else scanError = "Cannot start asset move worker";
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Cancel move")) { pendingMove = {}; ImGui::CloseCurrentPopup(); }
		ImGui::EndPopup();
	}
	if (const auto* selected = selectedAsset; selected && detailsHeight > 0.0f) {
		if (ImGui::BeginChild("AssetDetails",{0,0},ImGuiChildFlags_Borders)) {
		ImGui::TextWrapped("%s", selected->absolutePath.c_str());
		ImGui::Text("GUID: %s", selected->guid.c_str());
		ImGui::Text("Size: %llu bytes", static_cast<unsigned long long>(selected->size));
		const auto outgoing = dependencies.DependenciesOf(selected->guid), incoming = dependencies.DependentsOf(selected->guid);
		ImGui::Text("Dependencies: %llu / Dependents: %llu", static_cast<unsigned long long>(outgoing.size()), static_cast<unsigned long long>(incoming.size()));
		const auto relationship = [&](const char* label,const std::string& path,const Engine::Assets::AssetRecord* target) {
			if (target) {
				if (ImGui::SmallButton("Locate")) revealAsset = target;
				ImGui::SameLine();
			}
			ImGui::TextWrapped("%s%s",label,path.c_str());
		};
		for (size_t i = 0; i < outgoing.size() && i < 12; ++i) {
			ImGui::PushID(outgoing[i]);
			relationship(outgoing[i]->missing ? "Missing: " : "Uses: ",outgoing[i]->targetRelative,
				outgoing[i]->missing ? nullptr : registry.FindByGuid(outgoing[i]->targetGuid));
			ImGui::PopID();
		}
		for (size_t i = 0; i < incoming.size() && i < 12; ++i) {
			const auto* owner = registry.FindByGuid(incoming[i]->ownerGuid);
			ImGui::PushID(incoming[i]);
			relationship(incoming[i]->kind==Engine::Assets::AssetReferenceKind::Guid ? "Used by GUID: " : "Used by path: ",
				owner ? owner->relativePath : incoming[i]->ownerGuid,owner);
			ImGui::PopID();
		}
		if (outgoing.size()>12 || incoming.size()>12) ImGui::TextDisabled("Showing the first 12 entries in each direction.");
		for (const auto& issue : dependencies.issues) if (issue.ownerGuid == selected->guid) ImGui::TextWrapped("Inspection: %s", issue.message.c_str());
		}
		ImGui::EndChild();
	}
	ImGui::End();
	if (revealAsset) {
		const auto slash = revealAsset->relativePath.rfind('/');
		folder = slash == std::string::npos ? "" : revealAsset->relativePath.substr(0,slash);
		selectedGuid = revealGuid = revealAsset->guid;
		search[0] = '\0';
		type = 0;
	}
	if (sceneToOpen) openScene(*sceneToOpen);
}

namespace {

constexpr const char* kRetiredEditorWindows[] = {"シーン設定", "ライト設定", "ウィンドウ表示切り替え", "Window Switcher"};
std::unordered_map<std::string, bool> g_windowVisibility;
std::unordered_set<const bool*> g_appliedWindowPointers;
bool g_windowVisibilityLoaded = false;
bool g_windowVisibilityWritable = true;
bool g_windowVisibilityDirty = false;
std::filesystem::path g_windowVisibilityPath, g_legacyWindowVisibilityPath;

std::filesystem::path GetWindowVisibilityPath()
{
	if(g_windowVisibilityPath.empty()) {
		const Engine::Base::ProjectPaths paths;
		g_windowVisibilityPath=paths.EditorWindows();g_legacyWindowVisibilityPath=paths.Root()/".debug_editor_windows.ini";
	}
	return g_windowVisibilityPath;
}

void EnsureWindowVisibilityLoaded()
{
	if (g_windowVisibilityLoaded) {
		return;
	}
	g_windowVisibilityLoaded = true;
	std::string text,error;
	const auto path=GetWindowVisibilityPath();
	if(!EditorSettingsFile::Read(path,g_legacyWindowVisibilityPath,text,error)) {
		g_windowVisibilityWritable=false;
		Engine::Base::Logger::Log("Editor window settings load failed: "+error,Engine::Base::Logger::Level::Warning);return;
	}
	std::istringstream file(text);
	std::string line;
	while (std::getline(file, line)) {
		const size_t separator = line.find('=');
		if (separator == std::string::npos) {
			continue;
		}
		if(!line.empty() && line.back()=='\r') line.pop_back();
		const auto value=line.substr(separator+1);
		if(separator>0 && separator<=256 && (value=="0" || value=="1")) g_windowVisibility[line.substr(0,separator)]=value=="1";
	}
	for (const auto* name : kRetiredEditorWindows) {
		if (g_windowVisibility.erase(name)) g_windowVisibilityDirty = true;
	}
}

void SaveWindowVisibilityFile()
{
	if(!g_windowVisibilityWritable) return; // Never overwrite unreadable settings with defaults.
	std::ostringstream file;
	for (const auto& [label, open] : g_windowVisibility) {
		file << label << '=' << (open ? '1' : '0') << '\n';
	}
	std::string error;
	if(!EditorSettingsFile::Write(GetWindowVisibilityPath(),file.str(),error))
		Engine::Base::Logger::Log("Editor window settings save failed: "+error,Engine::Base::Logger::Level::Warning);
}

void ApplyPersistedWindowItems(const DebugEditorMenuItem* items, size_t count)
{
	EnsureWindowVisibilityLoaded();
	for (size_t index = 0; index < count; ++index) {
		if (!items[index].label || !items[index].open ||
			g_appliedWindowPointers.find(items[index].open) != g_appliedWindowPointers.end()) {
			continue;
		}
		const auto& item = items[index];
		const auto key = item.settingsId ? item.settingsId : item.label;
		const auto legacy = item.legacyLabel ? item.legacyLabel : item.label;
		auto saved = g_windowVisibility.find(key);
		if (saved == g_windowVisibility.end() && item.settingsId) saved = g_windowVisibility.find(legacy);
		if (saved != g_windowVisibility.end()) *item.open = saved->second;
		g_appliedWindowPointers.insert(items[index].open);
	}
}

bool RecoverFloatingPanel(const char* name)
{
	auto* window = ImGui::FindWindowByName(name);
	auto* settings = window ? nullptr : ImGui::FindWindowSettingsByID(ImHashStr(name));
	if (window ? window->DockId != 0 : !settings || settings->DockId != 0 || settings->WantDelete) return false;

	const auto* viewport = ImGui::GetMainViewport();
	const ImVec2 savedOrigin = settings && settings->ViewportId
		? ImVec2(settings->ViewportPos.x, settings->ViewportPos.y) : viewport->Pos;
	const ImVec2 position = window ? window->Pos
		: ImVec2(savedOrigin.x + settings->Pos.x, savedOrigin.y + settings->Pos.y);
	const ImVec2 size = window ? window->SizeFull : ImVec2(settings->Size.x, settings->Size.y);
	const ImVec2 workMin = viewport->WorkPos;
	const ImVec2 workSize = viewport->WorkSize;
	if (size.x <= 0 || size.y <= 0 || workSize.x <= 0 || workSize.y <= 0) return false;

	const ImVec2 workMax{workMin.x + workSize.x, workMin.y + workSize.y};
	const float titleHeight = ImGui::GetFrameHeight();
	const float visibleWidth = (std::min)(workMax.x, position.x + size.x) - (std::max)(workMin.x, position.x);
	// Moving an oversized panel cannot reveal all of it, even with a reachable title bar.
	const bool fitsWorkspace = size.x <= workSize.x && size.y <= workSize.y;
	if (fitsWorkspace && position.y >= workMin.y && position.y + titleHeight <= workMax.y &&
		visibleWidth >= (std::min)(size.x, titleHeight * 4.0f)) return false;

	const ImVec2 fittedSize{(std::min)(size.x, workSize.x), (std::min)(size.y, workSize.y)};
	const ImVec2 recovered{
		workMin.x + (workSize.x - fittedSize.x) * 0.5f,
		workMin.y + (workSize.y - fittedSize.y) * 0.5f
	};
	if (window) {
		ImGui::SetWindowPos(window, recovered, ImGuiCond_Always);
		ImGui::SetWindowSize(window, fittedSize, ImGuiCond_Always);
	} else {
		settings->ViewportId = 0;
		settings->ViewportPos = {};
		settings->Pos = ImVec2ih(ImVec2(recovered.x - viewport->Pos.x, recovered.y - viewport->Pos.y));
		settings->Size = ImVec2ih(fittedSize);
		ImGui::MarkIniSettingsDirty();
	}
	return true;
}

void DrawMenuItems(const DebugEditorMenuItem* items, size_t count)
{
	for (size_t index = 0; index < count; ++index) {
		const auto& item = items[index];
		if (item.open) {
			std::string label;
			if (item.icon && item.icon[0] != '\0') {
				label += item.icon;
				label += ' ';
			}
			label += item.label ? item.label : "";
			const bool selected = ImGui::MenuItem(label.c_str(), nullptr, *item.open);
#if defined(KUBO_APPLICATION_CHECKS)
			ObserveRenderInspectorChecks(label.c_str());
#endif
			if (selected) {
				const bool recovered = item.label && RecoverFloatingPanel(item.legacyLabel ? item.legacyLabel : item.label);
				*item.open = recovered || !*item.open;
			}
		}
	}
}

constexpr struct {
	SceneEditAction action;
	const char* label;
	const char* shortcut;
} kSceneEditCommands[] = {
	{SceneEditAction::Undo, "Undo Scene Edit", "Ctrl+Z"},
	{SceneEditAction::Redo, "Redo Scene Edit", "Ctrl+Y"},
	{SceneEditAction::Duplicate, "Duplicate Selected Subtrees", "Ctrl+D"},
	{SceneEditAction::Delete, "Delete Selected Subtrees", "Delete (Hierarchy)"},
};

template<class Draw, class Separator>
void DrawFileCommands(const DebugEditorMenuConfig& config, Draw draw, Separator separator)
{
	const char* openUnavailable = config.canOpenScene ? nullptr : config.openSceneUnavailableReason;
	if (config.onNewScene) draw("New Scene",config.onNewScene,config.canOpenScene,openUnavailable);
	if (config.onNewController) draw("New Controller",config.onNewController,config.canNewController,config.newControllerUnavailableReason);
	if (config.onOpenScene) draw("Open Scene...",config.onOpenScene,config.canOpenScene,openUnavailable);
	if (config.saveLabel || config.onSaveSceneAs || config.onRestore) separator();
	if (config.saveLabel) draw(config.saveLabel,config.onSave,bool(config.onSave),config.saveUnavailableReason);
	if (config.onSaveSceneAs) {
		const auto* reason = config.canSaveSceneAs ? nullptr : openUnavailable ? openUnavailable : "Stop playback before saving a copy.";
		draw("Save Scene As...",config.onSaveSceneAs,config.canSaveSceneAs,reason);
	}
	if (config.restoreLabel && config.onRestore) draw(config.restoreLabel,config.onRestore,true,nullptr);
	if (config.onExport || config.onProjectSettings) separator();
	if (config.onExport) draw("Export Game...",config.onExport,true,nullptr);
	if (config.onProjectSettings) draw("Project Settings...",config.onProjectSettings,true,nullptr);
	if (config.onReturnToHub) {
		separator();
		draw("Close Project and Return to Hub",config.onReturnToHub,true,nullptr);
	}
}

const char* SaveShortcut(const DebugEditorMenuConfig& config, const char* name)
{
	return config.saveLabel && std::string_view(name) == config.saveLabel ? "Ctrl+S" : nullptr;
}

void DrawUnavailableReason(bool enabled, const char* reason)
{
	if (!enabled && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		ImGui::SetTooltip("%s", reason ? reason : "Unavailable in the current editor state.");
}

const char* PlaybackStateLabel(DebugPlaybackState state)
{
	switch (state) {
	case DebugPlaybackState::Playing: return "Playing";
	case DebugPlaybackState::Paused: return "Paused";
	case DebugPlaybackState::Stopped: return "Editing";
	case DebugPlaybackState::Unavailable: return "No scene";
	}
	return "No scene";
}

template<class Draw>
void DrawPlaybackCommands(const DebugEditorMenuConfig& config, Draw draw)
{
	if (config.playbackState == DebugPlaybackState::Unavailable) return;
	const bool paused = config.playbackState == DebugPlaybackState::Paused;
	const auto reason = [&](const char* stateReason) {
		return config.playbackUnavailableReason ? config.playbackUnavailableReason : stateReason;
	};
	draw(paused ? "Resume Scene" : "Play Scene", ICON_FA_PLAY "##EditorPlay", config.onPlay,
		reason("The Scene is already playing."));
	draw("Pause Scene", ICON_FA_PAUSE "##EditorPause", config.onPause,
		reason("Play the Scene before pausing."));
	draw("Stop and Restore Scene", ICON_FA_STOP "##EditorStop", config.onStop,
		reason("The Scene is already in Editing mode."));
}

void DrawEditorCommands(const DebugEditorMenuConfig& config)
{
	static ImGuiTextFilter filter;
	if (!ImGui::BeginPopupModal("Editor Commands",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) return;
	if (ImGui::IsWindowAppearing()) { filter.Clear(); ImGui::SetKeyboardFocusHere(); }
	const auto& style = ImGui::GetStyle();
	const float viewportWidth = ImGui::GetMainViewport()->WorkSize.x;
	const float searchWidth = (std::max)(1.0f, (std::min)(ImGui::GetFontSize() * 26.0f,
		viewportWidth - style.WindowPadding.x * 4.0f - ImGui::CalcTextSize("Search").x));
	filter.Draw("Search",searchWidth);
	bool hasMatches = false;
	const auto item = [&](const char* name,bool enabled,const auto& execute,const char* unavailable = nullptr) {
		if (!filter.PassFilter(name)) return;
		hasMatches = true;
		if (ImGui::Selectable(name,false,enabled?ImGuiSelectableFlags_None:ImGuiSelectableFlags_Disabled)) {
			ImGui::CloseCurrentPopup();
			execute();
		}
		DrawUnavailableReason(enabled,unavailable);
	};
	DrawFileCommands(config,[&](const char* name,const auto& action,bool enabled,const char* reason) {
		item(name,enabled,action,reason);
	},[] {});
	for (const auto& command : kSceneEditCommands) {
		const char* unavailable = config.sceneEditUnavailableReason ? config.sceneEditUnavailableReason(command.action) : nullptr;
		item(command.label,bool(config.onSceneEdit) && !unavailable,
			[&] { config.onSceneEdit(command.action); },unavailable);
	}
	item("Reset Dock Layout",bool(config.onResetLayout),[&] { config.onResetLayout(); });
	if (!hasMatches) ImGui::TextDisabled("No matching commands.");
	if (ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
	ImGui::EndPopup();
}

void DrawDocumentStatus(const DebugEditorMenuConfig& config, float available)
{
	if (config.sceneName.empty() || available < ImGui::GetFontSize() * 7.0f) return;
	const ImVec4 tint = config.sceneDirty ? ImVec4{0.99f,0.77f,0.46f,1.0f} : ImGui::GetStyleColorVec4(ImGuiCol_Text);
	const std::string label = config.sceneDirty ? "* " + config.sceneName : config.sceneName;
	const ImVec2 size = ImGui::CalcTextSize(label.c_str());
	const float width = (std::min)(available, size.x);
	const ImVec2 start = ImGui::GetCursorScreenPos();
	const ImVec2 end{start.x + width, start.y + size.y};
	ImGui::Dummy({width, size.y});
#if defined(KUBO_APPLICATION_CHECKS)
	ObserveRenderInspectorChecks(label.c_str());
#endif
	ImGui::PushStyleColor(ImGuiCol_Text,tint);
	ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(),start,end,end.x,label.c_str(),label.c_str()+label.size(),&size);
	ImGui::PopStyleColor();
	if (ImGui::IsItemHovered()) {
		ImGui::BeginTooltip();
		ImGui::TextUnformatted(config.scenePath.empty() ? config.sceneName.c_str() : config.scenePath.c_str());
		ImGui::TextUnformatted(config.sceneDirty ? "Unsaved changes" : "Saved");
		ImGui::EndTooltip();
	}
}

void DrawEditorToolbar(const DebugEditorMenuConfig& config)
{
	if (config.playbackState == DebugPlaybackState::Unavailable) return;
	const char* state = PlaybackStateLabel(config.playbackState);
	const auto& style = ImGui::GetStyle();
	const float buttonWidth = ImGui::GetFrameHeight() * 1.3f;
	const float groupWidth = buttonWidth * 4 + style.ItemSpacing.x * 4 + ImGui::CalcTextSize(state).x;
	const float maxX = ImGui::GetWindowWidth() - style.WindowPadding.x;
	const float start = (std::max)(ImGui::GetCursorPosX() + style.ItemSpacing.x,
		(ImGui::GetWindowWidth() - groupWidth) / 2);
	if (start + groupWidth > maxX) return;
	ImGui::SetCursorPosX(start);
	const auto button = [&](const char* label, const char* icon, const std::function<void()>& action, const char* reason) {
		ImGui::BeginDisabled(!action);
		if (ImGui::Button(icon,{buttonWidth,0}) && action) action();
#if defined(KUBO_APPLICATION_CHECKS)
		ObserveRenderInspectorChecks(label);
#endif
		ImGui::EndDisabled();
		if (action && ImGui::IsItemHovered()) ImGui::SetTooltip("%s",label);
		DrawUnavailableReason(bool(action),reason);
		ImGui::SameLine();
	};
	button("Save Scene (Ctrl+S)", ICON_FA_SAVE "##EditorSave", config.onSave, config.saveUnavailableReason);
	DrawPlaybackCommands(config,button);
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted(state);
	ImGui::SameLine(0, style.ItemSpacing.x * 2.0f);
	ImGui::AlignTextToFramePadding();
	DrawDocumentStatus(config,maxX-ImGui::GetCursorPosX());
}

} // namespace

void DebugEditorManager::SetProjectRoot(const std::filesystem::path& root)
{
	const Engine::Base::ProjectPaths paths(root);
	g_windowVisibilityPath=paths.EditorWindows();g_legacyWindowVisibilityPath=paths.Root()/".debug_editor_windows.ini";
	g_windowVisibility.clear();g_appliedWindowPointers.clear();g_windowVisibilityLoaded=false;g_windowVisibilityWritable=true;
	g_windowVisibilityDirty = false;
}

void DebugEditorManager::DiscardRetiredDockWindows()
{
	for (const auto* name : kRetiredEditorWindows) ImGui::ClearWindowSettings(name);
}

void DebugEditorManager::BuildDefaultDockLayout(unsigned int dockspaceId, bool diagnostics)
{
	ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::DockBuilderRemoveNode(dockspaceId);
	ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
	ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->Size);

	ImGuiID dockMain = dockspaceId;
	ImGuiID dockBottom = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Down, 0.30f, nullptr, &dockMain);
	ImGuiID dockLeft = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, 0.21f, nullptr, &dockMain);
	ImGuiID dockRight = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right, 0.31f, nullptr, &dockMain);
	ImGuiID dockRightTop = diagnostics ? ImGui::DockBuilderSplitNode(dockRight, ImGuiDir_Up, 0.58f, nullptr, &dockRight) : dockRight;

	ImGui::DockBuilderDockWindow("Entity Hierarchy", dockLeft);
	ImGui::DockBuilderDockWindow("Scene", dockMain);
	ImGui::DockBuilderDockWindow("Game", dockMain);
	ImGui::DockBuilderDockWindow("Entity Inspector", dockRightTop);
	ImGui::DockBuilderDockWindow("統計", dockRight);
	ImGui::DockBuilderDockWindow("キー操作デバッグ", dockRight);
	ImGui::DockBuilderDockWindow("Asset Browser", dockBottom);
	ImGui::DockBuilderDockWindow("Console", dockBottom);
	ImGui::DockBuilderDockWindow("オーディオ", dockRight);
	ImGui::DockBuilderDockWindow("シーン固有デバッグ", dockRight);
	ImGui::DockBuilderDockWindow("プレイヤー状態", dockRight);
	ImGui::DockBuilderDockWindow("プレイヤー設定・武器デバッグ", dockRight);
	ImGui::DockBuilderDockWindow("パーティクル・演出", dockRight);
	ImGui::DockBuilderDockWindow("HUD・UI調整", dockRight);
	ImGui::DockBuilderDockWindow("オフスクリーン設定", dockRight);
	ImGui::DockBuilderDockWindow("ギズモ", dockRight);
	ImGui::DockBuilderDockWindow("オブジェクトマネージャ", dockRight);
	ImGui::DockBuilderDockWindow("モーションエディター", dockRight);
	ImGui::DockBuilderDockWindow("コライダー・当たり判定", dockRight);
	ImGui::DockBuilderFinish(dockspaceId);
}

void DebugEditorManager::ApplyDarkEditorStyle()
{
	ImGuiStyle& style = ImGui::GetStyle();
	style.WindowRounding = 4.0f;
	style.FrameRounding = 3.0f;
	style.PopupRounding = 4.0f;
	style.ScrollbarRounding = 4.0f;
	style.GrabRounding = 3.0f;
	style.WindowBorderSize = 1.0f;
	style.FrameBorderSize = 1.0f;
	style.Colors[ImGuiCol_Text] = ImVec4{ 0.94f, 0.94f, 0.94f, 1.0f };
	style.Colors[ImGuiCol_TextDisabled] = ImVec4{ 0.45f, 0.45f, 0.45f, 1.0f };
	style.Colors[ImGuiCol_WindowBg] = ImVec4{ 0.02f, 0.02f, 0.025f, 0.98f };
	style.Colors[ImGuiCol_ChildBg] = ImVec4{ 0.03f, 0.03f, 0.035f, 1.0f };
	style.Colors[ImGuiCol_PopupBg] = ImVec4{ 0.015f, 0.015f, 0.02f, 0.98f };
	style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4{ 0.0f, 0.0f, 0.0f, 0.52f };
	style.Colors[ImGuiCol_Border] = ImVec4{ 0.20f, 0.20f, 0.22f, 1.0f };
	style.Colors[ImGuiCol_FrameBg] = ImVec4{ 0.08f, 0.08f, 0.09f, 1.0f };
	style.Colors[ImGuiCol_FrameBgHovered] = ImVec4{ 0.16f, 0.16f, 0.18f, 1.0f };
	style.Colors[ImGuiCol_FrameBgActive] = ImVec4{ 0.22f, 0.22f, 0.25f, 1.0f };
	style.Colors[ImGuiCol_TitleBg] = ImVec4{ 0.02f, 0.02f, 0.025f, 1.0f };
	style.Colors[ImGuiCol_TitleBgActive] = ImVec4{ 0.04f, 0.04f, 0.05f, 1.0f };
	style.Colors[ImGuiCol_MenuBarBg] = ImVec4{ 0.07f, 0.10f, 0.15f, 1.0f };
	style.Colors[ImGuiCol_Button] = ImVec4{ 0.10f, 0.10f, 0.12f, 1.0f };
	style.Colors[ImGuiCol_ButtonHovered] = ImVec4{ 0.20f, 0.20f, 0.23f, 1.0f };
	style.Colors[ImGuiCol_ButtonActive] = ImVec4{ 0.28f, 0.28f, 0.32f, 1.0f };
	style.Colors[ImGuiCol_Header] = ImVec4{ 0.12f, 0.12f, 0.14f, 1.0f };
	style.Colors[ImGuiCol_HeaderHovered] = ImVec4{ 0.15f, 0.26f, 0.39f, 1.0f };
	style.Colors[ImGuiCol_HeaderActive] = ImVec4{ 0.18f, 0.35f, 0.53f, 1.0f };
	style.Colors[ImGuiCol_Tab] = ImVec4{ 0.06f, 0.06f, 0.07f, 1.0f };
	style.Colors[ImGuiCol_TabHovered] = ImVec4{ 0.18f, 0.18f, 0.22f, 1.0f };
	style.Colors[ImGuiCol_TabActive] = ImVec4{ 0.13f, 0.23f, 0.34f, 1.0f };
}

void DebugEditorManager::DrawMainMenu(const DebugEditorMenuConfig& config)
{
	ApplyPersistedWindowItems(config.windowItems, config.windowItemCount);
	ApplyPersistedWindowItems(config.editItems, config.editItemCount);
	ApplyPersistedWindowItems(config.objectItems, config.objectItemCount);
	if (config.saveLabel && (DocumentShortcuts::TakeSave("Scene") ||
		DocumentShortcuts::TakeSave("Entity Hierarchy") ||
		DocumentShortcuts::TakeSave("Entity Inspector") ||
		DocumentShortcuts::TakeSave("シーン固有デバッグ"))) {
		if (config.onSave) config.onSave();
	}
	if (!ImGui::BeginMainMenuBar()) {
		return;
	}
	const auto& input = ImGui::GetIO();
	const bool canShowCommands = config.playbackState != DebugPlaybackState::Playing && config.playbackState != DebugPlaybackState::Paused;
	bool showCommands = input.KeyCtrl && !input.KeyAlt && !input.KeyShift && !input.KeySuper &&
		canShowCommands &&
		!input.WantTextInput && !ImGui::IsAnyItemActive() &&
		!ImGui::IsMouseDown(ImGuiMouseButton_Right) && !ImGui::GetDragDropPayload() &&
		!ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel) &&
		ImGui::IsKeyPressed(ImGuiKey_K,false);

	const bool fileOpen = ImGui::BeginMenu(ICON_FA_FOLDER_OPEN " File");
#if defined(KUBO_APPLICATION_CHECKS)
	if (!fileOpen) ObserveRenderInspectorChecks("File menu");
#endif
	if (fileOpen) {
		if (!config.sceneName.empty()) {
			ImGui::TextDisabled("%s%s",config.sceneDirty ? "* " : "",config.sceneName.c_str());
			ImGui::Separator();
		}
		DrawFileCommands(config,[&](const char* name,const auto& action,bool enabled,const char* reason) {
			if (ImGui::MenuItem(name,SaveShortcut(config,name),false,enabled)) action();
#if defined(KUBO_APPLICATION_CHECKS)
			ObserveRenderInspectorChecks(name);
#endif
			DrawUnavailableReason(enabled,reason);
		},[] { ImGui::Separator(); });
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Edit")) {
		for (const auto& command : kSceneEditCommands) {
			const char* unavailable = config.sceneEditUnavailableReason ? config.sceneEditUnavailableReason(command.action) : nullptr;
			if (ImGui::MenuItem(command.label,command.shortcut,false,bool(config.onSceneEdit) && !unavailable))
				config.onSceneEdit(command.action);
			DrawUnavailableReason(bool(config.onSceneEdit) && !unavailable,unavailable);
		}
		if (!config.onSceneEdit) ImGui::TextDisabled("Available in an idle, editable Scene.");
		ImGui::EndMenu();
	}
	const bool viewOpen = ImGui::BeginMenu(ICON_FA_EYE " View");
#if defined(KUBO_APPLICATION_CHECKS)
	if (!viewOpen) ObserveRenderInspectorChecks("View menu");
#endif
	if (viewOpen) {
		const bool windowsOpen = ImGui::BeginMenu(ICON_FA_WINDOW_RESTORE " Windows");
#if defined(KUBO_APPLICATION_CHECKS)
		if (!windowsOpen) ObserveRenderInspectorChecks("Windows menu");
#endif
		if (windowsOpen) {
			DrawMenuItems(config.windowItems, config.windowItemCount);
			ImGui::EndMenu();
		}
		if (config.onApplyWorkspace && ImGui::BeginMenu("Workspace")) {
			if (ImGui::MenuItem("Production layout")) config.onApplyWorkspace(false);
			if (ImGui::MenuItem("Diagnostics layout")) config.onApplyWorkspace(true);
			ImGui::EndMenu();
		}
		if (config.onResetLayout) {
			const bool resetLayout = ImGui::MenuItem("Reset Dock Layout");
#if defined(KUBO_APPLICATION_CHECKS)
			ObserveRenderInspectorChecks("Reset Dock Layout");
#endif
			if (resetLayout) config.onResetLayout();
		}
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu(ICON_FA_TOOLS " Tools")) {
		DrawMenuItems(config.editItems, config.editItemCount);
		ImGui::EndMenu();
	}
	if (config.objectItemCount && ImGui::BeginMenu(ICON_FA_CUBE " Object")) {
		DrawMenuItems(config.objectItems, config.objectItemCount);
		ImGui::EndMenu();
	}
	if ((config.playbackState != DebugPlaybackState::Unavailable || config.onGoGame || config.onGoResult) &&
		ImGui::BeginMenu(ICON_FA_GAMEPAD " Game")) {
		if (config.playbackState != DebugPlaybackState::Unavailable) {
			ImGui::TextDisabled("%s",PlaybackStateLabel(config.playbackState));
			ImGui::Separator();
			DrawPlaybackCommands(config,[](const char* label,const char*,const auto& action,const char* reason) {
				if (ImGui::MenuItem(label,nullptr,false,bool(action))) action();
				DrawUnavailableReason(bool(action),reason);
			});
		}
		if (config.onGoGame && ImGui::MenuItem(ICON_FA_PLAY " Game")) {
			config.onGoGame();
		}
		if (config.onGoResult && ImGui::MenuItem(ICON_FA_FLAG_CHECKERED " Result")) {
			config.onGoResult();
		}
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu(ICON_FA_QUESTION_CIRCLE " Help")) {
		if (ImGui::MenuItem("Editor Commands...", "Ctrl+K",false,canShowCommands)) showCommands = true;
		if (!canShowCommands && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("Stop playback to open Editor Commands.");
		if (config.helpText) {
			ImGui::TextUnformatted(config.helpText);
		}
		ImGui::EndMenu();
	}
	DrawEditorToolbar(config);
	if (showCommands) ImGui::OpenPopup("Editor Commands");
	DrawEditorCommands(config);

	ImGui::EndMainMenuBar();
}

void DebugEditorManager::SaveWindowItems(const DebugEditorMenuItem* items, size_t itemCount)
{
	EnsureWindowVisibilityLoaded();
	if (!g_windowVisibilityWritable) return;
	for (size_t index = 0; index < itemCount; ++index) {
		const auto& item = items[index];
		if (!item.label || !item.open) continue;
		const auto key = item.settingsId ? item.settingsId : item.label;
		const auto saved = g_windowVisibility.find(key);
		if (saved == g_windowVisibility.end() || saved->second != *item.open) {
			g_windowVisibility[key] = *item.open;
			g_windowVisibilityDirty = true;
		}
		const auto legacy = item.legacyLabel ? item.legacyLabel : item.label;
		if (std::string_view(legacy) != key && g_windowVisibility.erase(legacy)) g_windowVisibilityDirty = true;
	}
	if (!g_windowVisibilityDirty) return;
	SaveWindowVisibilityFile();
	g_windowVisibilityDirty = false;
}

DebugSceneViewportState DebugEditorManager::DrawSceneViewport(bool* open, std::function<void()> toolbar, const char* title, uint64_t textureHandle, std::function<void(Vector2)> onAvailable, std::function<bool(const DebugSceneViewportState&)> overlay)
{
	DebugSceneViewportState state{};
	if (open && !*open) {
		return state;
	}

	ImGui::SetNextWindowPos(ImVec2(textureHandle ? 720.0f : 288.0f, 12.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(430.0f, 300.0f), ImGuiCond_FirstUseEver);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	constexpr ImGuiWindowFlags kSceneWindowFlags =
		ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoScrollWithMouse;
	if (!ImGui::Begin(title, open, kSceneWindowFlags)) {
		ImGui::End();
		ImGui::PopStyleVar();
		return state;
	}

	if (toolbar) toolbar();
	if (Engine::Base::OffscreenRenderManager* offscreen = Engine::Base::OffscreenRenderManager::GetInstance();
		offscreen && offscreen->HasImGuiSceneTexture()) {
		const ImVec2 availableSize = ImGui::GetContentRegionAvail();
		state.available = {availableSize.x, availableSize.y};
		if (availableSize.x < 1 || availableSize.y < 1) {
			ImGui::End(); ImGui::PopStyleVar(); return state;
		}
		if (onAvailable) onAvailable(state.available);
		const auto imageHandle = textureHandle ? textureHandle : (offscreen->HasEditorView() ? offscreen->GetEditorViewHandle().ptr : offscreen->GetImGuiSceneTextureHandle().ptr);
		const float kSceneAspect = !textureHandle && offscreen->HasEditorView() ?
			offscreen->GetEditorViewExtent().Aspect() : offscreen->GetGameOutputExtent().Aspect();
		ImVec2 imageSize{ availableSize.x, availableSize.x / kSceneAspect };
		if (imageSize.y > availableSize.y) {
			imageSize.y = availableSize.y;
			imageSize.x = imageSize.y * kSceneAspect;
		}

		const ImVec2 contentMin = ImGui::GetCursorScreenPos();
		const ImVec2 contentMax{
			contentMin.x + availableSize.x,
			contentMin.y + availableSize.y,
		};
		ImGui::GetWindowDrawList()->AddRectFilled(
			contentMin,
			contentMax,
			ImGui::GetColorU32(ImGuiCol_WindowBg));
		const ImVec2 imageMin{
			contentMin.x + (availableSize.x - imageSize.x) * 0.5f,
			contentMin.y + (availableSize.y - imageSize.y) * 0.5f,
		};
		const ImVec2 imageMax{
			imageMin.x + imageSize.x,
			imageMin.y + imageSize.y,
		};
		state.drawn = imageSize.x > 0.0f && imageSize.y > 0.0f;
		state.min = { imageMin.x, imageMin.y };
		state.size = { imageSize.x, imageSize.y };
		ImGui::GetWindowDrawList()->AddImage(
			static_cast<ImTextureID>(imageHandle),
			imageMin,
			imageMax,
			ImVec2(0.0f, 0.0f),
			ImVec2(1.0f, 1.0f));
		const ImVec2 mouse = ImGui::GetMousePos();
		const bool imageHovered =
			mouse.x >= imageMin.x && mouse.x <= imageMax.x &&
			mouse.y >= imageMin.y && mouse.y <= imageMax.y;
		state.inputActive = imageHovered && ImGui::IsWindowHovered() && !ImGui::GetIO().WantTextInput;
		if (state.drawn && overlay && overlay(state)) state.inputActive = false;
	}

	ImGui::End();
	ImGui::PopStyleVar();
	return state;
}

} // namespace Engine::Editor
