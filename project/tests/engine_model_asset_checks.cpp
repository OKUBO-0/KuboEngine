#include "../engine/3d/model/ModelAssetResolver.h"

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
    std::cout << "PASS: sample models, explicit roots, relative paths, fallback texture, missing root\n";
}
