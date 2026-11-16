#pragma once
#include "ExportAssetDependencies.h"
#include "AnimatorControllerAsset.h"
namespace Engine::Assets {
inline size_t ValidateExportControllerDependencies(const std::filesystem::path& projectFile,
    const std::vector<std::string>& guids,const std::vector<std::filesystem::path>& copyRoots) {
    if(guids.size()>65536)throw std::runtime_error("Too many export Controller references");
    Base::ProjectContext project;std::string error;
    if(!project.Open(projectFile,error))throw std::runtime_error(error);
    const auto root=project.GetRootPath();AssertExportOrdinaryPath(root);
    AssertControllerExportResolved(project.ResolvePath(project.GetSettings().assetDirectory));
    AssetRegistry registry;
    if(!registry.Scan(project.ResolvePath(project.GetSettings().assetDirectory),error,AssetRegistry::MetadataPolicy::ReadOnly))
        throw std::runtime_error(error);
    std::unordered_set<std::string> visited;
    for(const auto& guid:guids) {
        if(!visited.insert(guid).second)continue;
        HeldAnimatorControllerAsset lease;
        if(!lease.Open(project,registry,guid,error))throw std::runtime_error("Export Controller: "+error);
        for(const auto& source:{lease.Snapshot()->source,lease.Snapshot()->metadata}) {
            AssertExportOrdinaryPath(source);
            AssertExportPortableRelative(source.lexically_relative(root));
            bool copied=false;
            for(const auto& copyRoot:copyRoots) {
                AssertExportPortableRelative(copyRoot);
                if(ExportPathWithin(source,(root/copyRoot).lexically_normal())){copied=true;break;}
            }
            if(!copied)throw std::runtime_error("Export Controller source/metadata outside copied directories");
        }
    }
    AssertControllerExportResolved(project.ResolvePath(project.GetSettings().assetDirectory));
    return visited.size();
}
}
