#include "DebugHotReload.h"
#include "DataPaths.h"
#include <algorithm>

#ifdef _DEBUG
#include <imgui.h>
#endif

namespace DirectXGame {

namespace {

bool TryGetLastWriteTime(
	const std::string& path,
	std::filesystem::file_time_type& outTime)
{
	std::error_code error;
	outTime = std::filesystem::last_write_time(path, error);
	return !error;
}

}

void DebugHotReload::InitializeDefaultFiles()
{
	if (initialized_) {
		return;
	}
	initialized_ = true;
	AddWatch("Debug tuning", DataPaths::kDebugTuning, DebugHotReloadCategory::Tuning);
	AddWatch("Player status", DataPaths::kPlayerStatus, DebugHotReloadCategory::GameplayData);
	AddWatch("Character stats", DataPaths::kCharacterStats, DebugHotReloadCategory::GameplayData);
	AddWatch("Weapon upgrades", DataPaths::kWeaponUpgradeSettings, DebugHotReloadCategory::GameplayData);
	AddWatch("Enemy types", DataPaths::kEnemyTypes, DebugHotReloadCategory::GameplayData);
	AddWatch("Enemy spawn", DataPaths::kEnemySpawnSettings, DebugHotReloadCategory::GameplayData);
	AddWatch("Levelup weights", DataPaths::kLevelupWeights, DebugHotReloadCategory::GameplayData);
	AddWatch("HUD layout", DataPaths::kHudLayout, DebugHotReloadCategory::Layout);
	AddWatch("Pause layout", DataPaths::kPauseLayout, DebugHotReloadCategory::Layout);
	AddWatch("Levelup layout", DataPaths::kLevelupLayout, DebugHotReloadCategory::Layout);
	AddWatch("Resource manifest", DataPaths::kResourceManifest, DebugHotReloadCategory::Manifest);
	Poll();
	ClearChangedFlags();
}

void DebugHotReload::AddWatch(
	std::string_view label,
	std::string_view path,
	DebugHotReloadCategory category)
{
	WatchEntry entry{};
	entry.label = std::string(label);
	entry.path = std::string(path);
	entry.resolvedPath = ResolvePath(path);
	entry.category = category;
	entry.exists = TryGetLastWriteTime(entry.resolvedPath, entry.lastWriteTime);
	entries_.push_back(std::move(entry));
}

void DebugHotReload::Poll()
{
	for (WatchEntry& entry : entries_) {
		std::filesystem::file_time_type currentTime{};
		const bool exists = TryGetLastWriteTime(entry.resolvedPath, currentTime);
		if (exists != entry.exists ||
			(exists && entry.exists && currentTime != entry.lastWriteTime)) {
			entry.exists = exists;
			entry.lastWriteTime = currentTime;
			entry.changed = true;
			++entry.changeCount;
			++totalChangeCount_;
		}
	}
}

DebugHotReloadRequest DebugHotReload::ConsumeAutoReloadRequest()
{
	if (!autoReloadEnabled_) {
		return DebugHotReloadRequest::None;
	}

	bool needsTuning = false;
	bool needsGameplay = false;
	for (const WatchEntry& entry : entries_) {
		if (!entry.changed) {
			continue;
		}
		if (entry.category == DebugHotReloadCategory::Tuning ||
			entry.category == DebugHotReloadCategory::Layout) {
			needsTuning = true;
		}
		if (entry.category == DebugHotReloadCategory::GameplayData) {
			needsGameplay = true;
		}
	}

	if (needsTuning && needsGameplay) {
		ClearChangedFlags();
		return DebugHotReloadRequest::ReloadAll;
	}
	if (needsGameplay) {
		ClearChangedFlags();
		return DebugHotReloadRequest::ReloadGameplayData;
	}
	if (needsTuning) {
		ClearChangedFlags();
		return DebugHotReloadRequest::ReloadTuning;
	}
	return DebugHotReloadRequest::None;
}

DebugHotReloadRequest DebugHotReload::DrawPanel(bool* open)
{
#ifdef _DEBUG
	if (!open || !*open) {
		return DebugHotReloadRequest::None;
	}

	DebugHotReloadRequest request = DebugHotReloadRequest::None;
	ImGui::SetNextWindowPos(ImVec2(760.0f, 12.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(520.0f, 440.0f), ImGuiCond_FirstUseEver);
	ImGui::Begin("調整・ホットリロード", open);
	ImGui::Checkbox("Auto reload changed CSV", &autoReloadEnabled_);
	ImGui::SameLine();
	if (ImGui::Button("Check now")) {
		Poll();
	}
	ImGui::Text("Detected changes: %u", totalChangeCount_);

	if (ImGui::Button("Reload tuning / UI")) {
		request = DebugHotReloadRequest::ReloadTuning;
	}
	ImGui::SameLine();
	if (ImGui::Button("Reload gameplay CSV")) {
		request = DebugHotReloadRequest::ReloadGameplayData;
	}
	ImGui::SameLine();
	if (ImGui::Button("Reload all")) {
		request = DebugHotReloadRequest::ReloadAll;
	}

	if (ImGui::BeginTable(
			"DebugHotReloadFiles",
			5,
			ImGuiTableFlags_BordersInnerV |
			ImGuiTableFlags_RowBg |
			ImGuiTableFlags_SizingStretchProp)) {
		ImGui::TableSetupColumn("State");
		ImGui::TableSetupColumn("Category");
		ImGui::TableSetupColumn("File");
		ImGui::TableSetupColumn("Changes");
		ImGui::TableSetupColumn("Path");
		ImGui::TableHeadersRow();
		for (const WatchEntry& entry : entries_) {
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted(
				entry.changed ? "changed" : (entry.exists ? "ok" : "missing"));
			ImGui::TableSetColumnIndex(1);
			ImGui::TextUnformatted(CategoryName(entry.category));
			ImGui::TableSetColumnIndex(2);
			ImGui::TextUnformatted(entry.label.c_str());
			ImGui::TableSetColumnIndex(3);
			ImGui::Text("%u", entry.changeCount);
			ImGui::TableSetColumnIndex(4);
			ImGui::TextUnformatted(entry.path.c_str());
		}
		ImGui::EndTable();
	}

	if (ImGui::Button("Clear change marks")) {
		ClearChangedFlags();
	}
	ImGui::End();
	return request;
#else
	(void)open;
	return DebugHotReloadRequest::None;
#endif
}

void DebugHotReload::ClearChangedFlags()
{
	for (WatchEntry& entry : entries_) {
		entry.changed = false;
	}
}

std::string DebugHotReload::ResolvePath(std::string_view path)
{
	if (path.starts_with("Resources/") || path.starts_with("Resources\\")) {
		return std::filesystem::path(path).generic_string();
	}
	return DataPaths::Resolve(path);
}

const char* DebugHotReload::CategoryName(DebugHotReloadCategory category)
{
	switch (category) {
	case DebugHotReloadCategory::Tuning:
		return "Tuning";
	case DebugHotReloadCategory::GameplayData:
		return "Gameplay";
	case DebugHotReloadCategory::Layout:
		return "Layout";
	case DebugHotReloadCategory::Manifest:
		return "Manifest";
	}
	return "Unknown";
}

}
