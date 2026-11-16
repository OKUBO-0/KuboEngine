#include "AssetSpriteDependencies.h"
#include "AssetSceneDependencies.h"
namespace Engine::Assets {
void AppendProjectSpriteDependencies(AssetDependencies& graph,const AssetRegistry& registry,
    const std::filesystem::path& root,const std::filesystem::path& file,const std::function<bool()>& cancelled){
    AppendProjectAssetDependencies(graph,registry,root,file,cancelled,SceneAssetReferenceScope::SpriteOnly);
}
std::string OpenWorldSpriteMoveRisk(const Scene::EntityWorld& world,const AssetRegistry& registry,
    const std::filesystem::path& root,const std::string& guid){
    return OpenWorldAssetMoveRisk(world,registry,root,guid,SceneAssetReferenceScope::SpriteOnly);
}
}
