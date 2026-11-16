#pragma once
#include "AssetDependencies.h"
#include <filesystem>
#include <functional>
namespace Engine::Scene {class EntityWorld;}
namespace Engine::Assets {
enum class SceneAssetReferenceScope {All,SpriteOnly};
struct WorldAssetIssue {
    uint64_t entity=0;
    std::string guid,message;
    std::filesystem::path source;
};
inline constexpr size_t MaximumWorldAssetIssues=128;
std::vector<WorldAssetIssue> InspectWorldAssetReferences(const Scene::EntityWorld& world,const AssetRegistry& registry,
    const std::filesystem::path& projectRoot);
// Bounded reference inspection, not complete component/document validation.
void AppendProjectAssetDependencies(AssetDependencies& graph,const AssetRegistry& registry,
    const std::filesystem::path& projectRoot,const std::filesystem::path& projectFile={},
    const std::function<bool()>& cancelled={},SceneAssetReferenceScope scope=SceneAssetReferenceScope::All);
std::string OpenWorldAssetMoveRisk(const Scene::EntityWorld& world,const AssetRegistry& registry,
    const std::filesystem::path& projectRoot,const std::string& guid,
    SceneAssetReferenceScope scope=SceneAssetReferenceScope::All);
}
