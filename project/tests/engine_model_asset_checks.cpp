#include "../engine/3d/model/ModelAssetResolver.h"
#include "../engine/base/AssetPath.h"
#include "../engine/base/EngineResourcePath.h"
#include <fstream>

#include <filesystem>
#include <iostream>
#include <stdexcept>

int main()
{
    using Engine::Graphics3D::ResolveModelAssetPath;
    using Engine::Graphics3D::ResolveModelResourcePath;
    namespace fs = std::filesystem;
    const auto require = [](bool passed, const char* message) {
        if (!passed) {
            throw std::runtime_error(message);
        }
    };
    require(fs::is_directory("Resources/models"), "Run from the project directory");
    for (const char* name : {"axis.obj", "plane.gltf", "sphere.obj", "terrain.obj",
             "animationfly.gltf", "sphere.gltf", "player.gltf", "walk.gltf", "testanimation.gltf"}) {
        require(fs::is_regular_file(ResolveModelAssetPath("Resources", name)), name);
    }
    require(fs::equivalent(ResolveModelAssetPath("Resources/models/axis", "axis.obj"),
        "Resources/models/axis/axis.obj"), "Explicit resource root must work");
    require(fs::equivalent(ResolveModelAssetPath("Resources", "models/axis/axis.obj"),
        "Resources/models/axis/axis.obj"), "Explicit relative path must work");
    require(fs::equivalent(ResolveModelResourcePath("Resources/textures/common", "white.png"),
        "Resources/textures/common/white.png"), "Fallback texture must exist");
    require(ResolveModelAssetPath("missing-root", "missing.obj") ==
        "missing-root/models/missing.obj", "Missing root must return the requested candidate");
    using Engine::Base::NormalizeAssetPath;
    const auto modelKey = NormalizeAssetPath(ResolveModelAssetPath("Resources", "axis.obj"));
    require(modelKey == NormalizeAssetPath(ResolveModelAssetPath("./Resources/", "models/axis/../axis/axis.obj")),
        "Short and explicit model paths must have the same identity");
    require(modelKey == NormalizeAssetPath(fs::absolute("Resources/models/axis/axis.obj")),
        "Absolute and relative paths must have the same identity");
    require(modelKey == NormalizeAssetPath("Resources\\models\\axis\\axis.obj"),
        "Windows path separators must have the same identity");
    require(NormalizeAssetPath("Resources/textures/common/white.png") ==
        NormalizeAssetPath("./Resources/textures/common/../common/white.png"),
        "Texture path aliases must have the same identity");
    require(NormalizeAssetPath("missing-root/./missing.obj") ==
        NormalizeAssetPath("missing-root/missing.obj"), "Missing paths must normalize without throwing");
    require(NormalizeAssetPath("").empty(), "Empty paths must stay empty");
    const auto originalCwd=fs::current_path();
    const auto token=L"kubo-owned-model-fallback-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
    const auto unrelated=fs::temp_directory_path()/token;
    const auto builtin=Engine::Base::ResolveEngineResource("Resources")/"models"/token;
    require(!fs::exists(unrelated) && !fs::exists(builtin),"Owned fallback fixture collision");
    struct Cleanup {fs::path cwd,local,resource;~Cleanup(){std::error_code ignored;fs::current_path(cwd,ignored);fs::remove_all(local,ignored);fs::remove_all(resource,ignored);}} cleanup{originalCwd,unrelated,builtin};
    fs::create_directories(unrelated);fs::create_directories(builtin);
    const auto filename=Engine::Base::PathToUtf8(fs::path(token+L".obj"));
    {std::ofstream file(builtin/fs::path(token+L".obj"));file<<"owned resolver fixture";require(bool(file),"Cannot write fallback fixture");}
    fs::current_path(unrelated);
    require(fs::equivalent(Engine::Base::PathFromUtf8(ResolveModelAssetPath("Resources",filename)),builtin/fs::path(token+L".obj")),"Legacy default root did not find bundled grouped model from unrelated cwd");
    require(fs::equivalent(Engine::Base::PathFromUtf8(ResolveModelResourcePath("Resources",filename)),builtin/fs::path(token+L".obj")),"Legacy material/resource search did not use bundled root");
    require(ResolveModelAssetPath("CustomRoot",filename)=="CustomRoot/models/"+filename,"Explicit root silently fell back to bundled content");
    require(ResolveModelResourcePath("CustomRoot",filename)=="CustomRoot/"+filename,"Missing explicit resource root did not remain fail-closed");
    std::cout << "PASS: sample models, explicit roots, relative paths, fallback texture, missing root\n";
    std::cout<<"PASS: actual executable-relative legacy model/resource fallback from unrelated cwd, explicit custom-root isolation\n";
}
