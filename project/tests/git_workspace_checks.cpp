#include "../engine/services/GitWorkspace.h"
#include <iostream>
#include <stdexcept>
#include <Windows.h>

void Require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
struct RestorePath {
    std::wstring previous;
    RestorePath() {
        const auto size = GetEnvironmentVariableW(L"PATH", nullptr, 0);
        previous.resize(size);
        const auto count = GetEnvironmentVariableW(L"PATH", previous.data(), size);
        previous.resize(count);
    }
    ~RestorePath() { SetEnvironmentVariableW(L"PATH", previous.c_str()); }
};
int wmain(int argc, wchar_t** argv)
{
    using Engine::Services::GitWorkspace;
    try {
        std::vector<Engine::Services::GitChange> changes;
        std::string error;
        std::string bytes = " M 日本語 & file.txt";
        bytes += '\0';
        bytes += "R  new name.txt"; bytes += '\0';
        bytes += "old -> name.txt"; bytes += '\0';
        bytes += "?? newline\nfile.txt"; bytes += '\0';
        Require(GitWorkspace::ParsePorcelain(bytes, changes, error), error.c_str());
        Require(changes.size() == 3 && changes[0].path == "日本語 & file.txt" &&
            changes[1].path == "new name.txt" && changes[1].originalPath == "old -> name.txt" &&
            changes[2].path == "newline\nfile.txt", "NUL status parsing lost filename data");
        Require(!GitWorkspace::ParsePorcelain("R  unfinished", changes, error) && changes.size() == 3,
            "Malformed parsing replaced prior output");
        Require(!GitWorkspace::ParsePorcelain(std::string("XX file\0", 8), changes, error), "Invalid status code accepted");
        Require(GitWorkspace::IsAvailable(), "Git missing");
        const auto current = GitWorkspace::ReadStatus(std::filesystem::current_path());
        Require(current.repository ? (current.error.empty() && !current.repositoryRoot.empty()) : !current.error.empty(), "Workspace Git result is neither a repository nor an explicit error");
        Require(current.branch.find('\n') == std::string::npos, "Branch was not normalized");
        {
            RestorePath restore;
            Require(SetEnvironmentVariableW(L"PATH", L".") != 0, "Cannot set isolated test PATH");
            Require(!GitWorkspace::IsAvailable(), "Relative PATH entry was trusted for Git discovery");
            const auto missing = GitWorkspace::ReadStatus(std::filesystem::current_path());
            Require(!missing.repository && !missing.error.empty(), "Missing Git error was not reported");
        }
        const auto nonRepository = std::filesystem::temp_directory_path() /
            (L"kubo-non-git-日本語 & %-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
        std::filesystem::create_directories(nonRepository);
        const auto absent = GitWorkspace::ReadStatus(nonRepository);
        std::filesystem::remove(nonRepository); // Empty, test-owned directory only.
        Require(!absent.repository && !absent.error.empty(), "Non-repository error was not reported");
        if (argc > 1) {
            const auto fixture = GitWorkspace::ReadStatus(std::filesystem::path(argv[1]));
            Require(fixture.repository && fixture.error.empty(), fixture.error.c_str());
            if (argc > 2) Require(fixture.branch.empty() && fixture.detached, "Detached HEAD was not reported");
            else Require(fixture.branch == "main" && !fixture.detached, "Fixture branch mismatch");
            bool rename = false, untracked = false;
            for (const auto& change : fixture.changes) {
                rename |= change.index == 'R' && change.path == "renamed.txt" && change.originalPath == "tracked.txt";
                untracked |= change.index == '?' && change.path == "untracked.txt";
            }
            Require(rename && untracked, "Live fixture changes missing");
        }
        std::cout << "PASS: Git read-only status, NUL records, Unicode/special paths, rename parsing and failed-parse isolation\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
