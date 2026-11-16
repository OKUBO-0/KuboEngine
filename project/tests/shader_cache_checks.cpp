#include "ShaderCompiler.h"
#include "engine/base/EngineResourcePath.h"
#include <Windows.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void RequireShader(bool value, const char* message)
{
    if (!value) { throw std::runtime_error(message); }
}
void WriteShader(const std::filesystem::path& path, const std::string& source)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << source;
    output.close();
    RequireShader(!output.fail(), "Cannot write shader test fixture");
}
struct ShaderFixtures {
    std::filesystem::path directory = std::filesystem::temp_directory_path() /
        (L"KuboShaderChecks-日本語-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
            std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count()));
    ShaderFixtures() { std::filesystem::create_directories(directory / "nested"); }
    ~ShaderFixtures() { std::error_code error; std::filesystem::remove_all(directory, error); }
};
}

void CheckShaderCache()
{
    RequireShader(GetACP() == CP_UTF8, "Engine UTF-8 process manifest was not embedded");
    namespace fs = std::filesystem;
    ShaderFixtures fixtures;
    const auto main = fixtures.directory / "main.hlsl";
    const auto header = fixtures.directory / "nested/value.hlsli";
    WriteShader(fixtures.directory / "shared.hlsli", "#include \"nested/value.hlsli\"\n");
    WriteShader(header, "#define VALUE float4(1,0,0,1)\n");
    WriteShader(main, "#include \"shared.hlsli\"\nfloat4 main() : SV_Target { return VALUE; }\n");
    Engine::Base::ShaderCompiler compiler;
    compiler.Initialize();
    const auto original = compiler.Compile(main.wstring(), L"ps_6_0");
    RequireShader(original && compiler.GetCompileCount() == 1, "First request must compile");
    const auto alias = compiler.Compile((fixtures.directory / "nested/../main.hlsl").wstring(), L"ps_6_0");
    RequireShader(alias.Get() == original.Get() && compiler.GetCacheHitCount() == 1, "Path aliases must hit cache");
    const auto stamp = fs::last_write_time(header);
    WriteShader(header, "#define VALUE float4(0,1,0,1)\n");
    fs::last_write_time(header, stamp);
    const auto edited = compiler.Compile(main.wstring(), L"ps_6_0");
    RequireShader(edited.Get() != original.Get() && compiler.GetCompileCount() == 2,
        "Nested include changes with unchanged size/timestamp must recompile");
    WriteShader(header, "#define VALUE missing_symbol\n");
    bool failed = false;
    try { compiler.Compile(main.wstring(), L"ps_6_0"); } catch (const std::runtime_error&) { failed = true; }
    RequireShader(failed && compiler.GetCacheSize() == 1, "Compile failure must not replace last successful cache entry");
    WriteShader(header, "#define VALUE float4(0,1,0,1)\n");
    RequireShader(compiler.Compile(main.wstring(), L"ps_6_0").Get() == edited.Get(), "Repaired source must recover");
    fs::remove(header);
    failed = false;
    try { compiler.Compile(main.wstring(), L"ps_6_0"); } catch (const std::runtime_error&) { failed = true; }
    RequireShader(failed, "Missing include must not silently return stale bytecode");
    WriteShader(header, "#define VALUE float4(0,1,0,1)\n");
    compiler.Compile(main.wstring(), L"ps_6_1");
    RequireShader(compiler.GetCacheSize() == 2, "Profiles must have separate cache entries");
    WriteShader(main, "#include \"shared.hlsli\"\nfloat4 main() : SV_Target { return VALUE * 0.5; }\n");
    RequireShader(compiler.Compile(main.wstring(), L"ps_6_0").Get() != edited.Get(), "Main source edits must recompile");
    compiler.ClearCache();
    RequireShader(compiler.GetCacheSize() == 0 && compiler.GetCompileCount() == 0 && original->GetBufferSize() > 0,
        "Clearing cache must not invalidate caller-owned bytecode");
    compiler.Compile(main.wstring(), L"ps_6_0");
    RequireShader(compiler.GetCompileCount() == 1, "Cleared cache must compile again");
    failed = false;
    try { compiler.Compile(main.wstring(), nullptr); } catch (const std::invalid_argument&) { failed = true; }
    RequireShader(failed, "Null profile must be rejected");
    const auto bundledRelative=fs::path("Resources/Shaders/sprite/Sprite.PS.hlsl");
    const auto bundled=Engine::Base::ResolveEngineResource(bundledRelative);
    RequireShader(fs::is_regular_file(bundled),"Bundled shader missing from runner installation");
    fs::create_directories((fixtures.directory/bundledRelative).parent_path());
    WriteShader(fixtures.directory/bundledRelative,"invalid cwd shadow shader; must never compile\n");
    {
        struct CwdRestore {fs::path path;~CwdRestore(){std::error_code error;fs::current_path(path,error);}} restore{fs::current_path()};
        fs::current_path(fixtures.directory);
        Engine::Base::ShaderCompiler installedCompiler;installedCompiler.Initialize();
        const auto installedBlob=installedCompiler.Compile(bundledRelative.wstring(),L"ps_6_0");
        RequireShader(installedBlob && installedCompiler.Compile(bundled.wstring(),L"ps_6_0").Get()==installedBlob.Get() && installedCompiler.GetCacheHitCount()==1,
            "DXC used cwd shadow instead of shared installation-relative/absolute cache entry");
    }
    std::cout << "PASS: DXC cache aliases, nested includes, same timestamp edits, failure/recovery, profiles, clear\n";
    std::cout << "PASS: real DXC compiles bundled shader despite invalid cwd shadow and reuses explicit installation alias\n";
}
