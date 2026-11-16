#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <atomic>

namespace Engine::Services {

struct GitChange {
    char index = ' ';
    char worktree = ' ';
    std::string path;
    std::string originalPath;
};

struct GitStatus {
    bool repository = false;
    std::string branch;
    std::string porcelain;
    std::string error;
    std::string repositoryRoot;
    bool detached = false;
    std::vector<GitChange> changes;
    // Admission tokens, not a lock or a byte-level snapshot of working files.
    std::string head, indexRevision;
};

struct GitCommitEntry { std::string hash,author,date,subject; };
struct GitHistory { std::string head;size_t skip=0;bool more=false;std::vector<GitCommitEntry> entries; };

/// @brief Explicit project-scoped Git status, index and staged-commit operations.
class GitWorkspace final {
public:
    static GitStatus ReadStatus(const std::filesystem::path& projectRoot);
    static bool IsAvailable();
    static bool CloneHttps(const std::wstring& url, const std::filesystem::path& directory,
        const std::atomic_bool& cancelled, std::string& error);
    static bool ParsePorcelain(const std::string& bytes, std::vector<GitChange>& changes, std::string& error);
    // Stage current disk content / unstage selected entry only. No worktree
    // restore, commit or network command. Requires a fresh status selection.
    static bool UpdateIndex(const std::filesystem::path& projectRoot,const GitStatus& expected,
        const GitChange& selected,bool stage,std::string& error);
    // Commit the existing index only, after global staged-path scope and fresh
    // HEAD/index admission checks. Trusted hooks/configuration still run.
    static bool Commit(const std::filesystem::path& projectRoot,const GitStatus& expected,
        const std::string& message,std::string& error);
    // Read-only selected staged (HEAD/index) or unstaged (index/disk) display.
    // Output is escaped for display, not an applyable patch; preserved on failure.
    static bool ReadDiff(const std::filesystem::path& projectRoot,const GitStatus& expected,
        const GitChange& selected,bool staged,std::string& display,std::string& error);
    static bool ParseHistory(const std::string& bytes,std::vector<GitCommitEntry>& entries,std::string& error);
    // 50-entry pages of path-limited history anchored to the confirmed HEAD.
    // Maximum offset 10000; output is preserved on failure. No checkout/reset.
    static bool ReadHistory(const std::filesystem::path& projectRoot,const GitStatus& expected,
        size_t skip,GitHistory& history,std::string& error);
};

} // namespace Engine::Services
