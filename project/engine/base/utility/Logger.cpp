#include "Logger.h"
#include "Windows.h"
#include <atomic>
#include <deque>
#include <utility>
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

	uint64_t NewScope() noexcept
	{
		static std::atomic_uint64_t next{1};
		return next.fetch_add(1,std::memory_order_relaxed);
	}

	void Log(const std::string& message, Level level, const std::filesystem::path& sourceFile)
	{
		LogDiagnostic(message,level,{},sourceFile);
	}

	void LogDiagnostic(const std::string& message,Level level,Target target,const std::filesystem::path& sourceFile)
	{
        if(!target.project)target={};
        if(!target.scene)target.entity=0;
        if((target.assetGuid.size()!=16 && target.assetGuid.size()!=32) ||
            target.assetGuid.find_first_not_of("0123456789abcdef")!=std::string::npos)target.assetGuid.clear();
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
			history.entries.push_back({ history.nextSequence++, level, stored, hasLocation ? sourceFile : std::filesystem::path{}, std::move(target) });
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
