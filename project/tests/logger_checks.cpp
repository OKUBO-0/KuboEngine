#include "../engine/base/utility/Logger.h"
#include <iostream>
#include <stdexcept>
#include <thread>

namespace Logger = Engine::Base::Logger;
void Require(bool condition, const char* message)
{
    if (!condition) { throw std::runtime_error(message); }
}
int main()
{
    Logger::Clear();
    Logger::Log("old API");
    Logger::Log("warning", Logger::Level::Warning);
    const auto source = std::filesystem::absolute(L"素材/失敗.hlsl");
    Logger::Log("100% error\nsecond line", Logger::Level::Error, source);
    const auto snapshot = Logger::GetEntries();
    Require(snapshot.size() == 3 && snapshot[0].level == Logger::Level::Info, "Legacy Log must remain usable");
    Require(snapshot[2].message == "100% error\nsecond line", "Messages must be literal and preserve newlines");
    Require(snapshot[0].sourceFile.empty() && snapshot[2].sourceFile == source, "Log source location was not preserved");
    Logger::Log("relative path", Logger::Level::Error, L"relative.hlsl");
    Require(Logger::GetEntries().back().sourceFile.empty(), "Relative source path accepted");
    Logger::Clear();
    Require(Logger::GetEntries().empty() && snapshot.size() == 3, "Clear must not invalidate snapshots");
    const auto project=Logger::NewScope(),scene=Logger::NewScope();
    const std::string guid(32,'a');
    Logger::LogDiagnostic("asset problem",Logger::Level::Error,{project,scene,7,guid},source);
    const auto stamped=Logger::GetEntries().back();
    Require(project && scene && project!=scene && stamped.target.project==project &&
        stamped.target.scene==scene && stamped.target.entity==7 && stamped.target.assetGuid==guid &&
        stamped.sourceFile==source,"Diagnostic target or source location was not preserved");
    Logger::LogDiagnostic("malformed",Logger::Level::Warning,{project,0,7,"untrusted"});
    const auto sanitized=Logger::GetEntries().back().target;
    Require(sanitized.project==project && !sanitized.scene && !sanitized.entity &&
        sanitized.assetGuid.empty(),"Unscoped Entity or malformed GUID accepted");
    std::vector<std::thread> writers;
    for (int i = 0; i < 4; ++i) {
        writers.emplace_back([] { for (int n = 0; n < 400; ++n) { Logger::Log("parallel"); } });
    }
    for (auto& writer : writers) { writer.join(); }
    const auto entries = Logger::GetEntries();
    Require(entries.size() == Logger::kMaxEntries, "History must be bounded");
    for (size_t i = 1; i < entries.size(); ++i) {
        Require(entries[i].sequence == entries[i - 1].sequence + 1, "Concurrent logs must preserve sequence");
    }
    Logger::Log(std::string(Logger::kMaxMessageBytes * 2, 'x'));
    const auto truncated = Logger::GetEntries().back().message;
    Require(truncated.size() <= Logger::kMaxMessageBytes && truncated.ends_with("[truncated]"),
        "Individual messages must also be bounded");
    Logger::Clear();
    std::cout << "PASS: logger compatibility, severity, snapshot, concurrent writes, bounded history/messages\n";
}
