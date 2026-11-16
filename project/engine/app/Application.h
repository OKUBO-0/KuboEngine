#pragma once

#include <filesystem>

namespace Engine::App {

/// @brief Shared process bootstrap for the editor and player applications.
class Application {
public:
    explicit Application(std::filesystem::path projectPath);
    virtual ~Application() = default;

    int Run();
    void SetMcpAtStartup(bool enabled) { mcpAtStartup_ = enabled; }
    void SetDiagnosticOutput(bool enabled) { diagnosticOutput_ = enabled; }
#ifndef KUBO_RUNTIME_ONLY
    void SetEditorReadyEvent(void* handle) noexcept { editorReadyEvent_ = handle; }
#endif

protected:
    virtual bool IsEditorApplication() const = 0;

private:
    static std::filesystem::path GetExecutablePath();

    std::filesystem::path projectPath_;
    bool mcpAtStartup_ = false;
    bool diagnosticOutput_ = false;
#ifndef KUBO_RUNTIME_ONLY
    void* editorReadyEvent_ = nullptr; // Borrowed from the process entry point.
#endif
};

#ifndef KUBO_RUNTIME_ONLY
class EditorApplication final : public Application {
public:
    using Application::Application;

protected:
    bool IsEditorApplication() const override { return true; }
};
#endif

class PlayerApplication final : public Application {
public:
    using Application::Application;

protected:
    bool IsEditorApplication() const override { return false; }
};

} // namespace Engine::App
