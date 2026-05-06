#pragma once

#include "Vector2.h"
#include "Vector3.h"
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace DirectXGame::UILayoutIO {

struct Entry {
	std::string key;
	std::vector<float> values;
};

using LayoutMap = std::unordered_map<std::string, std::vector<float>>;

LayoutMap Load(std::string_view filePath);
LayoutMap LoadOrDefault(std::string_view filePath, const std::vector<Entry>& defaultEntries);
bool Save(std::string_view filePath, const std::vector<Entry>& entries);

float GetFloat(const LayoutMap& layout, std::string_view key, float fallback);
Vector2 GetVector2(const LayoutMap& layout, std::string_view key, const Vector2& fallback);
Vector3 GetVector3(const LayoutMap& layout, std::string_view key, const Vector3& fallback);

}
