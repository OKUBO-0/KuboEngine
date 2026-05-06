#include "game/directxgame/core/UILayoutIO.h"
#include "game/directxgame/core/CsvReader.h"
#include "game/directxgame/core/DirectXGameDataPaths.h"
#include <filesystem>
#include <fstream>

namespace DirectXGame::UILayoutIO {

namespace {

std::string ResolvePath(std::string_view filePath)
{
	if (filePath.starts_with("Resources/") || filePath.starts_with("Resources\\")) {
		return std::filesystem::path(filePath).generic_string();
	}

	return DataPaths::Resolve(filePath);
}

std::vector<Entry> ToEntries(const LayoutMap& layout)
{
	std::vector<Entry> entries;
	entries.reserve(layout.size());
	for (const auto& [key, values] : layout) {
		entries.push_back({ key, values });
	}
	return entries;
}

}

LayoutMap Load(std::string_view filePath)
{
	return CsvReader::LoadFloatArrayMap(filePath);
}

LayoutMap LoadOrDefault(std::string_view filePath, const std::vector<Entry>& defaultEntries)
{
	const bool fileExists = CsvReader::Exists(filePath);
	LayoutMap layout = Load(filePath);
	bool insertedDefaults = false;

	for (const Entry& entry : defaultEntries) {
		if (layout.contains(entry.key)) {
			continue;
		}
		layout.emplace(entry.key, entry.values);
		insertedDefaults = true;
	}

#ifdef _DEBUG
	if (!fileExists || insertedDefaults) {
		Save(filePath, ToEntries(layout));
	}
#endif

	return layout;
}

bool Save(std::string_view filePath, const std::vector<Entry>& entries)
{
#ifndef _DEBUG
	static_cast<void>(filePath);
	static_cast<void>(entries);
	return false;
#else
	const std::string resolvedPath = ResolvePath(filePath);
	std::filesystem::create_directories(std::filesystem::path(resolvedPath).parent_path());

	LayoutMap merged = Load(resolvedPath);
	for (const Entry& entry : entries) {
		merged[entry.key] = entry.values;
	}

	std::ofstream file(resolvedPath, std::ios::trunc);
	if (!file.is_open()) {
		return false;
	}

	for (const auto& [key, values] : merged) {
		file << key;
		for (float value : values) {
			file << ',' << value;
		}
		file << '\n';
	}

	return true;
#endif
}

float GetFloat(const LayoutMap& layout, std::string_view key, float fallback)
{
	const auto it = layout.find(std::string(key));
	if (it == layout.end() || it->second.empty()) {
		return fallback;
	}

	return it->second[0];
}

Vector2 GetVector2(const LayoutMap& layout, std::string_view key, const Vector2& fallback)
{
	const auto it = layout.find(std::string(key));
	if (it == layout.end() || it->second.size() < 2) {
		return fallback;
	}

	return { it->second[0], it->second[1] };
}

Vector3 GetVector3(const LayoutMap& layout, std::string_view key, const Vector3& fallback)
{
	const auto it = layout.find(std::string(key));
	if (it == layout.end() || it->second.size() < 3) {
		return fallback;
	}

	return { it->second[0], it->second[1], it->second[2] };
}

}
