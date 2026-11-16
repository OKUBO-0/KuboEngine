#pragma once
#include <string>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>
namespace Engine::Base::Logger
{
	void Log(const std::string& message);
	enum class Level { Info, Warning, Error };
	struct Entry {
		uint64_t sequence;
		Level level;
		std::string message;
		std::filesystem::path sourceFile;
	};
	inline constexpr size_t kMaxEntries = 1024;
	inline constexpr size_t kMaxMessageBytes = 8192;
	void Log(const std::string& message, Level level);
	void Log(const std::string& message, Level level, const std::filesystem::path& sourceFile);
	std::vector<Entry> GetEntries();
	void Clear();
	const char* GetLevelName(Level level);


};

