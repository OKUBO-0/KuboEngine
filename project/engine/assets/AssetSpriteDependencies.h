#pragma once
#include "AssetDependencies.h"
#include <filesystem>
#include <functional>
namespace Engine::Scene {class EntityWorld;}
namespace Engine::Assets {
// Worker-owned, bounded Scene/Prefab Sprite reference inspection. Definitions
// are separate so including AssetImportJob does not instantiate its parsers.
void AppendProjectSpriteDependencies(AssetDependencies& graph,const AssetRegistry& registry,
    const std::filesystem::path& projectRoot,const std::filesystem::path& projectFile={},
    const std::function<bool()>& cancelled={});
std::string OpenWorldSpriteMoveRisk(const Scene::EntityWorld& world,const AssetRegistry& registry,
    const std::filesystem::path& projectRoot,const std::string& guid);
}
