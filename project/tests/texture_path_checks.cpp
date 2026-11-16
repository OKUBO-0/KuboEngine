#include "engine/base/directx/TextureAssetResolver.h"
#include "engine/base/ProjectPaths.h"
#include <Windows.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void Require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
void Write(const std::filesystem::path& path) {
    std::filesystem::create_directories(path.parent_path());std::ofstream file(path);file<<"owned fixture";Require(bool(file),"Fixture write failed");
}
}
int main() {
    namespace fs=std::filesystem;using namespace Engine::Base;
    const auto previous=fs::current_path();
    const auto fixture=fs::temp_directory_path()/(L"kubo-texture-path-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try {
        Require(!fs::exists(fixture),"Fixture collision");fs::create_directories(fixture);
        struct Cleanup {fs::path root,cwd;~Cleanup(){std::error_code error;fs::current_path(cwd,error);fs::remove_all(root,error);}} cleanup{fixture,previous};
        const auto project=fixture/L"所有プロジェクト";const auto engine=fixture/"Engine";const auto unrelated=fixture/"Unrelated";
        const auto relative=PathFromUtf8("Assets/色.png");
        Write(project/relative);Write(engine/relative);Write(unrelated/relative);
        Write(project/"Resources/local.png");Write(engine/"Resources/local.png");Write(engine/"Resources/builtin.png");
        Write(engine/"Assets/missing.png");Write(unrelated/"Assets/missing.png");
        const ProjectPaths owner(project);fs::current_path(unrelated);
        Require(PathFromUtf8(ResolveTextureAssetPath(PathToUtf8(relative),engine,owner.Root()))==project/relative,"Texture followed cwd instead of explicit project owner");
        Require(PathFromUtf8(ResolveTextureAssetPath("Resources/local.png",engine,owner.Root()))==project/"Resources/local.png","Bundled texture shadowed authored project Resources");
        Require(PathFromUtf8(ResolveTextureAssetPath("./Resources/builtin.png",engine,owner.Root()))==engine/"Resources/builtin.png","Scoped bundled Resources fallback failed");
        Require(PathFromUtf8(ResolveTextureAssetPath("Assets/missing.png",engine,owner.Root()))==project/"Assets/missing.png","Missing authored texture substituted installation/cwd asset");
        Require(PathFromUtf8(ResolveTextureAssetPath(PathToUtf8(project/relative),engine,owner.Root()))==project/relative,"Absolute Unicode texture changed");
        Require(PathFromUtf8(ResolveTextureAssetPath(PathToUtf8(project/"missing.png"),engine,owner.Root()))==project/"missing.png","Missing absolute texture was redirected");
        fs::current_path(engine);
        Require(PathFromUtf8(ResolveTextureAssetPath(PathToUtf8(relative),engine,owner.Root()))==project/relative,"Texture ownership changed after second cwd switch");
        Require(ResolveTextureAssetPath("",engine,owner.Root()).empty(),"Empty texture became a directory");
        std::cout<<"PASS: project-owned Unicode texture paths, cwd/installation competitor isolation, scoped Resources fallback and absolute/missing references\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
