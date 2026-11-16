#pragma once
#include "ModelReference.h"
#include "../base/AssetPath.h"

namespace Engine::Assets {
// GUID authority uses the shared live metadata check; a bad GUID never falls
// back to its path hint. No implicit white/checkerboard texture substitution.
inline std::filesystem::path ResolveSpriteReference(const Scene::SpriteComponent& sprite,
    const AssetRegistry& registry,const std::filesystem::path& projectRoot,std::string& error) {
    error.clear();
    try {
        if(!sprite.IsValid()){error="Invalid Sprite reference/settings";return {};}
        if(sprite.assetPath.empty() && sprite.assetGuid.empty()){error="Unassigned Sprite image";return {};}
        auto path=ResolveAssetReference(sprite.assetPath,sprite.assetGuid,AssetType::Texture,registry,projectRoot,error);
        if(path.empty())return {};
        if(AssetRegistry::Classify(path)!=AssetType::Texture){error="Sprite source is not a texture asset";return {};}
        const auto root=std::filesystem::canonical(projectRoot),resolved=std::filesystem::canonical(path);
        auto part=resolved.begin();
        for(const auto& component:root){
            if(part==resolved.end() || _wcsicmp(part->c_str(),component.c_str())){error="Sprite source escaped project root";return {};}
            ++part;
        }
        if(part==resolved.end()){error="Sprite source is not a project file";return {};}
        // Canonical containment is an admission-time check, not a held-file or
        // hostile concurrent-writer guarantee. TextureManager opens it later.
        return resolved;
    }catch(const std::exception& exception){error=exception.what();return {};}
}
}
