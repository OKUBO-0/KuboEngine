#pragma once

#include "ExportAssetDependencies.h"
#include "MaterialAsset.h"

namespace Engine::Assets {

inline size_t ValidateExportMaterialDependencies(const Scene::EntityWorld& world,const AssetRegistry& registry,
    const std::filesystem::path& root,const std::vector<std::filesystem::path>& copyRoots) {
    std::unordered_set<std::string> visited;
    const auto copied=[&](const std::filesystem::path& source){
        AssertExportOrdinaryPath(source);
        AssertExportPortableRelative(source.lexically_relative(root));
        for(const auto& relative:copyRoots){
            AssertExportPortableRelative(relative);
            if(ExportPathWithin(source,(root/relative).lexically_normal()))return;
        }
        throw std::runtime_error("Export Material dependency outside copied directories: "+Base::PathToUtf8(source));
    };
    for(const auto& entity:world.GetEntities()){
        const auto& guid=entity.material.assetGuid;
        if(!entity.hasMaterial || guid.empty() || !visited.insert(guid).second)continue;
        const auto label="Export Material on Entity "+std::to_string(entity.id)+" ("+guid+"): ";
        Scene::MaterialComponent settings;
        std::string error;
        const auto path=ResolveMaterialAsset(guid,registry,error);
        if(path.empty() || !ReadMaterialAsset(path,settings,error))throw std::runtime_error(label+error);
        copied(path);
        copied(AssetRegistry::MetadataPath(path));
    }
    return visited.size();
}

} // namespace Engine::Assets
