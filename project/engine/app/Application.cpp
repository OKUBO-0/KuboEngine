#include "Application.h"

#include "D3DResourceLeakChecker.h"
#ifdef KUBO_RUNTIME_ONLY
#include "RuntimeGame.h"
#else
#include "Game.h"
#endif
#include "ProjectContext.h"

#include <Windows.h>
#include <memory>
#include <string>
#include <cstdio>

namespace Engine::App {

Application::Application(std::filesystem::path projectPath)
    : projectPath_(std::move(projectPath))
{
}

int Application::Run()
{
    Engine::Base::ProjectContext project;
    std::string projectError;
    if (projectPath_.empty()) {
        projectPath_ = Engine::Base::ProjectContext::FindManifestUpwards(GetExecutablePath());
    }
    if (!projectPath_.empty() && !project.Open(projectPath_, projectError)) {
        MessageBoxA(nullptr, projectError.c_str(), "KuboEngine project error", MB_OK | MB_ICONERROR);
        return 1;
    }
    if (project.IsOpen()) {
        std::error_code directoryError;
        std::filesystem::current_path(project.GetRootPath(), directoryError);
        if (directoryError) {
            MessageBoxA(nullptr, directoryError.message().c_str(), "KuboEngine project error", MB_OK | MB_ICONERROR);
            return 1;
        }
    }

    Engine::Base::D3DResourceLeakChecker leakCheck;
    OutputDebugStringA(IsEditorApplication() ? "KuboEditor starting\n" : "KuboPlayer starting\n");

    #ifdef KUBO_RUNTIME_ONLY
    auto game = std::make_unique<RuntimeGame>();
    #else
    auto game = std::make_unique<Engine::Scene::Game>();
    game->SetEditorMode(IsEditorApplication());
    game->SetMcpAtStartup(mcpAtStartup_ && IsEditorApplication());
    if (IsEditorApplication()) game->SetHubReadyEvent(editorReadyEvent_);
    #endif
    if (project.IsOpen()) {
        game->SetProjectRoot(project.GetRootPath());
        game->SetGameOutputExtent(project.GetSettings().gameOutput);
        if(!game->ConfigureLuaResourceLimits(project.GetSettings().luaLimits,projectError)) {
            MessageBoxA(nullptr,projectError.c_str(),"KuboEngine Lua configuration error",MB_OK|MB_ICONERROR);return 1;
        }
        game->SetProjectFile(project.GetProjectFile());
        const auto& directory = project.GetSettings().assetDirectory;
        game->SetProjectAssetRoot(project.ResolvePath(std::filesystem::path(std::u8string(directory.begin(), directory.end()))));
    }
    if (project.IsOpen() && !project.GetSettings().startupScene.empty() && !game->ConfigureStartupScene(project.ResolvePath(project.GetSettings().startupScene), projectError)) {
        MessageBoxA(nullptr, projectError.c_str(), "KuboEngine startup scene error", MB_OK | MB_ICONERROR);
        return 1;
    }
    if(diagnosticOutput_) {
        const auto applied=game->GetLuaResourceLimits();
        std::fprintf(stdout,"KUBO_LUA_LIMITS memoryBytes=%zu states=%zu frameInstructions=%zu\n",applied.memoryBytes,applied.states,applied.frameInstructions);std::fflush(stdout);
    }
    #ifdef KUBO_RUNTIME_ONLY
    game->SetDiagnosticOutput(diagnosticOutput_);
    #endif
    game->Run();
    return 0;
}

std::filesystem::path Application::GetExecutablePath()
{
    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    return length == 0 ? std::filesystem::current_path() : std::filesystem::path(path, path + length);
}

} // namespace Engine::App
