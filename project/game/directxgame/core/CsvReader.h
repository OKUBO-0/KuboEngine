#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace DirectXGame::CsvReader {

using CsvRow = std::vector<std::string>;
using CsvTable = std::vector<CsvRow>;
using KeyValueMap = std::unordered_map<std::string, std::string>;
using FloatArrayMap = std::unordered_map<std::string, std::vector<float>>;

bool Exists(std::string_view filePath);
CsvTable LoadRows(std::string_view filePath);
KeyValueMap LoadKeyValueMap(std::string_view filePath);
FloatArrayMap LoadFloatArrayMap(std::string_view filePath);
float ParseFloat(
	std::string_view value,
	std::string_view context);
int32_t ParseInt32(
	std::string_view value,
	std::string_view context);

}
