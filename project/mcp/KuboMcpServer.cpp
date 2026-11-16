#include "../engine/services/McpProtocolSession.h"
#include "../engine/services/EditorMcpSession.h"
#include "../engine/base/ProjectContext.h"
#include <iostream>
#include <io.h>
#include <fcntl.h>

int wmain(int argc, wchar_t** argv)
{
    std::filesystem::path manifest = std::filesystem::current_path() / "KuboEngine.kubo";
    for (int index = 1; index < argc; ++index) {
        if (std::wstring(argv[index]) != L"--project" || index + 1 >= argc) {
            std::cerr << "Usage: KuboMcpServer.exe --project <project.kubo>\n";
            return 1;
        }
        manifest = argv[++index];
    }
    Engine::Base::ProjectContext project;
    std::string error;
    if (!project.Open(manifest, error)) { std::cerr << error << '\n'; return 1; }
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    using Session = Engine::Services::McpProtocolSession;
    Session session([&project](const Session::Value& request) {
        return Engine::Services::EditorMcpSession::Call(project.GetRootPath(), request);
    });
    std::string line;
    bool oversized = false;
    while (Session::ReadFrame(std::cin, line, oversized)) {
        if (line.empty() && !oversized) continue;
        const auto response = oversized ? Session::Error(nullptr, -32700, "MCP frame exceeds 64 KiB") : session.HandleText(line);
        if (!response.is_null()) std::cout << response.dump() << '\n' << std::flush;
    }
    return 0;
}
