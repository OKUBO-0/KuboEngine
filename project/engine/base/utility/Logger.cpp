#include "Logger.h"
#include "Windows.h"
#include <deque>
#include <mutex>

namespace Engine::Base::Logger {
	namespace {
	struct History {
		std::mutex mutex;
		std::deque<Entry> entries;
		uint64_t nextSequence = 1;
	};
	History& GetHistory()
	{
		static History history;
		return history;
	}
	}

	void Log(const std::string& message)
	{
		Log(message, Level::Info);
	}

	void Log(const std::string& message, Level level)
	{
		Log(message, level, {});
	}

	void Log(const std::string& message, Level level, const std::filesystem::path& sourceFile)
	{
		std::string stored = message.substr(0, kMaxMessageBytes);
		if (message.size() > kMaxMessageBytes) {
			size_t end = kMaxMessageBytes - 16;
			while (end > 0 && (static_cast<unsigned char>(stored[end]) & 0xc0) == 0x80) {
				--end;
			}
			stored.resize(end);
			stored += "... [truncated]";
		}
		auto& history = GetHistory();
		{
			std::lock_guard lock(history.mutex);
			if (history.entries.size() == kMaxEntries) {
				history.entries.pop_front();
			}
			const bool hasLocation = sourceFile.is_absolute() && sourceFile.has_filename() &&
				sourceFile.native().size() < 32768 && sourceFile.native().find(std::filesystem::path::value_type{}) == std::filesystem::path::string_type::npos;
			history.entries.push_back({ history.nextSequence++, level, stored, hasLocation ? sourceFile : std::filesystem::path{} });
		}
		const std::string output = "[" + std::string(GetLevelName(level)) + "] " + stored +
			(stored.empty() || stored.back() != '\n' ? "\n" : "");
		OutputDebugStringA(output.c_str());
	}

	std::vector<Entry> GetEntries()
	{
		auto& history = GetHistory();
		std::lock_guard lock(history.mutex);
		return { history.entries.begin(), history.entries.end() };
	}

	void Clear()
	{
		auto& history = GetHistory();
		std::lock_guard lock(history.mutex);
		history.entries.clear();
	}

	const char* GetLevelName(Level level)
	{
		switch (level) {
		case Level::Warning: return "Warning";
		case Level::Error: return "Error";
		default: return "Info";
		}
	}

}
