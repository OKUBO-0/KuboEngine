#include "game/directxgame/core/CsvReader.h"
#include "game/directxgame/core/DataPaths.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace DirectXGame::CsvReader {

namespace {

std::string ResolvePath(std::string_view filePath)
{
	if (filePath.starts_with("Resources/") || filePath.starts_with("Resources\\")) {
		return std::filesystem::path(filePath).generic_string();
	}

	return DataPaths::Resolve(filePath);
}

std::string Trim(std::string text)
{
	const auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
	text.erase(text.begin(), std::find_if(text.begin(), text.end(), [&](unsigned char c) { return !isSpace(c); }));
	text.erase(std::find_if(text.rbegin(), text.rend(), [&](unsigned char c) { return !isSpace(c); }).base(), text.end());
	return text;
}

bool IsCommentLine(const std::string& line)
{
	const std::string trimmed = Trim(line);
	return trimmed.starts_with('#') || trimmed.starts_with("//");
}

}

bool Exists(std::string_view filePath)
{
	return std::filesystem::exists(ResolvePath(filePath));
}

CsvTable LoadRows(std::string_view filePath)
{
	CsvTable table;

	std::ifstream file(ResolvePath(filePath));
	if (!file.is_open()) {
		return table;
	}

	std::string line;
	while (std::getline(file, line)) {
		if (line.empty() || IsCommentLine(line)) {
			continue;
		}

		std::stringstream stream(line);
		CsvRow row;
		std::string cell;
		while (std::getline(stream, cell, ',')) {
			row.push_back(Trim(cell));
		}

		if (!row.empty() && !row.front().empty()) {
			table.push_back(std::move(row));
		}
	}

	return table;
}

KeyValueMap LoadKeyValueMap(std::string_view filePath)
{
	KeyValueMap map;
	for (const CsvRow& row : LoadRows(filePath)) {
		if (row.size() < 2 || row[0].empty()) {
			continue;
		}
		map[row[0]] = row[1];
	}

	return map;
}

FloatArrayMap LoadFloatArrayMap(std::string_view filePath)
{
	FloatArrayMap map;
	for (const CsvRow& row : LoadRows(filePath)) {
		if (row.size() < 2 || row[0].empty()) {
			continue;
		}

		std::vector<float> values;
		values.reserve(row.size() - 1);
		for (size_t index = 1; index < row.size(); ++index) {
			if (row[index].empty()) {
				continue;
			}
			std::ostringstream context;
			context << ResolvePath(filePath)
				<< " key=\"" << row[0]
				<< "\" column=" << index;
			values.push_back(ParseFloat(
				row[index],
				context.str()));
		}

		map[row[0]] = std::move(values);
	}

	return map;
}

float ParseFloat(
	std::string_view value,
	std::string_view context)
{
	const std::string text(value);
	size_t parsedLength = 0;
	try {
		const float result = std::stof(text, &parsedLength);
		if (parsedLength != text.size() || !std::isfinite(result)) {
			throw std::invalid_argument("not a finite float");
		}
		return result;
	} catch (const std::exception& error) {
		throw std::runtime_error(
			"Invalid float for " + std::string(context) +
			": \"" + text + "\" (" + error.what() + ')');
	}
}

int32_t ParseInt32(
	std::string_view value,
	std::string_view context)
{
	const std::string text(value);
	size_t parsedLength = 0;
	try {
		const long result = std::stol(text, &parsedLength, 10);
		if (parsedLength != text.size() ||
			result < (std::numeric_limits<int32_t>::min)() ||
			result > (std::numeric_limits<int32_t>::max)()) {
			throw std::out_of_range("not a 32-bit integer");
		}
		return static_cast<int32_t>(result);
	} catch (const std::exception& error) {
		throw std::runtime_error(
			"Invalid integer for " + std::string(context) +
			": \"" + text + "\" (" + error.what() + ')');
	}
}

}
