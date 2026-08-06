#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace DirectXGame {

enum class DebugHotReloadCategory {
	Tuning,
	GameplayData,
	Layout,
	Manifest,
};

enum class DebugHotReloadRequest {
	None,
	ReloadTuning,
	ReloadGameplayData,
	ReloadAll,
};

class DebugHotReload final {
public:
	struct WatchEntry {
		std::string label;
		std::string path;
		std::string resolvedPath;
		DebugHotReloadCategory category = DebugHotReloadCategory::Tuning;
		bool exists = false;
		bool changed = false;
		uint32_t changeCount = 0;
		std::filesystem::file_time_type lastWriteTime{};
	};

	void InitializeDefaultFiles();
	void AddWatch(
		std::string_view label,
		std::string_view path,
		DebugHotReloadCategory category);
	void Poll();
	DebugHotReloadRequest ConsumeAutoReloadRequest();
	DebugHotReloadRequest DrawPanel(bool* open);
	void ClearChangedFlags();

	bool IsAutoReloadEnabled() const { return autoReloadEnabled_; }
	void SetAutoReloadEnabled(bool enabled) { autoReloadEnabled_ = enabled; }
	const std::vector<WatchEntry>& Entries() const { return entries_; }
	uint32_t GetTotalChangeCount() const { return totalChangeCount_; }

private:
	static std::string ResolvePath(std::string_view path);
	static const char* CategoryName(DebugHotReloadCategory category);

	std::vector<WatchEntry> entries_;
	bool autoReloadEnabled_ = false;
	bool initialized_ = false;
	uint32_t totalChangeCount_ = 0;
};

}
