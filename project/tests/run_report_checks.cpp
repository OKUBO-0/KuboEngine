#include "../engine/base/core/RunReport.h"
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

static void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
static std::vector<std::filesystem::path> Reports(const std::filesystem::path& root) {
    std::vector<std::filesystem::path> paths;
    for (const auto& entry : std::filesystem::directory_iterator(root))
        if (entry.path().filename().wstring().find(L".kubo-run-error-") == 0) paths.push_back(entry.path());
    return paths;
}
static std::string Read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
int wmain(int argc, wchar_t** argv) {
    try {
        Check(argc == 1 || argc == 2, "optional owned root argument only");
        const auto root = argc == 2 ? std::filesystem::absolute(argv[1]) :
            std::filesystem::temp_directory_path() /
            (L"kubo-run-report-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
        Check(!std::filesystem::exists(root), "fixture must be fresh");
        const auto original = root / L"project-\u65E5\u672C\u8A9E";
        const auto moved = root / L"moved-\u65E5\u672C\u8A9E";
        const auto unrelated = root / L"unrelated-cwd";
        std::filesystem::create_directories(original);
        std::filesystem::create_directories(unrelated);
        const auto operation = std::make_exception_ptr(std::runtime_error("operation\nline\\end"));
        const auto cleanup = std::make_exception_ptr(std::runtime_error("first cleanup"));
        Engine::Base::RunReport report(original);
        std::filesystem::current_path(unrelated);
        Check(report.Save(operation, cleanup, false, 3), "first save failed");
        auto paths = Reports(original); Check(paths.size() == 1, "first report count");
        const auto first = Read(paths[0]);
        Check(first.find("shutdown_complete=0\nshutdown_attempts=3") != std::string::npos, "fatal metadata");
        Check(first.find("operation\\x0Aline\\x5Cend") != std::string::npos, "escaped operation");
        Check(first.find("first cleanup") != std::string::npos, "cleanup missing");
        Check(first.ends_with("END KuboEngine run report\n"), "end marker missing");
        std::filesystem::rename(original, moved);
        std::filesystem::create_directory(original);
        Check(report.Save(operation, cleanup, true, 2), "rename save failed");
        Check(Reports(original).empty() && Reports(unrelated).empty(), "wrong destination");
        paths = Reports(moved); Check(paths.size() == 2, "unique report count");
        Check(Read(moved / paths[0].filename()) == first || Read(moved / paths[1].filename()) == first, "previous report changed");
        std::string longMessage(2000, 'A'); longMessage[0] = static_cast<char>(0xFF);
        Check(report.Save(std::make_exception_ptr(std::runtime_error(longMessage)), {}, true, 1), "long message save");
        bool sawBounded = false;
        for (const auto& path : Reports(moved)) {
            const auto text = Read(path);
            if (text.find("[truncated after 1024 bytes]") != std::string::npos) {
                Check(text.find("\\xFF") != std::string::npos && text.size() < 5000, "byte encoding/bound");
                sawBounded = true;
            }
        }
        Check(sawBounded, "truncation not tested");
        Engine::Base::RunReport missing(root / L"missing");
        Check(!missing.Save(operation, cleanup, false, 3), "missing root fallback");
        std::wprintf(L"Retained report fixture: %ls\n", root.c_str());
        std::puts("PASS RunReport: captured Unicode directory, changed cwd, renamed/replaced path, unique files, paired errors, byte escaping, truncation, missing-root refusal");
        return 0;
    } catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
