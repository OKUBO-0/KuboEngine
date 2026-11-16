#pragma once
#include "ExportAssetDependencies.h"
#include "SpriteReference.h"
#include "../base/ProjectContext.h"
#include <set>

namespace Engine::Assets {
struct ExportSpriteClosure {size_t sprites=0,textures=0,unassigned=0;};
// Read-only dependency admission, not image decoding. Disabled/inactive assigned
// Sprites still need their dependencies (they can be enabled after startup).
inline ExportSpriteClosure ValidateExportSpriteDependencies(const Scene::EntityWorld& world,const AssetRegistry& registry,
    const std::filesystem::path& projectRoot,const std::vector<std::filesystem::path>& copyRoots) {
    const auto root=std::filesystem::absolute(projectRoot).lexically_normal();AssertExportOrdinaryPath(root);
    const auto copied=[&](const std::filesystem::path& source){
        AssertExportOrdinaryPath(source);AssertExportPortableRelative(source.lexically_relative(root));
        for(const auto& relative:copyRoots){AssertExportPortableRelative(relative);if(ExportPathWithin(source,(root/relative).lexically_normal()))return;}
        throw std::runtime_error("Export Sprite source/metadata outside copied directories: "+Base::PathToUtf8(source));
    };
    const auto less=[](const std::wstring& a,const std::wstring& b){
        const int order=CompareStringOrdinal(a.c_str(),-1,b.c_str(),-1,TRUE);
        if(!order)throw std::runtime_error("Cannot compare Sprite export paths");
        return order==CSTR_LESS_THAN;
    };
    ExportSpriteClosure result;std::set<std::wstring,decltype(less)> textures(less),buttonScenes(less);
    for(const auto& entity:world.GetEntities())if(entity.hasSprite){
        if(!entity.sprite.IsValid())throw std::runtime_error("Export Sprite has invalid authored fields");
        if(!entity.sprite.buttonSceneGuid.empty()){
            std::string error;
            const auto destination=ResolveAssetReference({},entity.sprite.buttonSceneGuid,AssetType::Scene,registry,root,error);
            if(destination.empty())throw std::runtime_error("Export UI Button destination refused: "+error);
            copied(destination);copied(AssetRegistry::MetadataPath(destination));
            if(buttonScenes.insert(destination.native()).second){
                Scene::EntityWorld scene;
                if(!scene.Load(destination,error))throw std::runtime_error("Export UI Button needs an Entity Scene: "+error);
            }
        }
        if(!entity.sprite.HasImage()){
            if(entity.sprite.text.empty())++result.unassigned;
            continue;
        }
        std::string error;const auto source=ResolveSpriteReference(entity.sprite,registry,root,error);
        if(source.empty())throw std::runtime_error("Export Sprite reference refused: "+error);
        copied(source);if(!entity.sprite.assetGuid.empty())copied(AssetRegistry::MetadataPath(source));
        textures.insert(source.native());++result.sprites;
    }
    result.textures=textures.size();return result;
}
inline ExportSpriteClosure ValidateExportStartupSpriteDependencies(const std::filesystem::path& projectFile,
    const std::vector<std::filesystem::path>& copyRoots) {
    Base::ProjectContext project;std::string error;if(!project.Open(projectFile,error))throw std::runtime_error(error);
    const auto root=project.GetRootPath(),source=project.ResolvePath(project.GetSettings().startupScene),assetRoot=project.ResolvePath(project.GetSettings().assetDirectory);
    AssertExportOrdinaryPath(root);AssertExportPortableRelative(source.lexically_relative(root));AssertExportOrdinaryPath(source);
    Scene::SceneValue document;if(!Scene::SceneDocument::Read(source,document,error))throw std::runtime_error(error);
    if(!document.contains("entities"))return {}; // Same legacy typed Scene admission as RuntimeGame.
    Scene::EntityWorld world;if(!world.Restore(document,error))throw std::runtime_error("Export UI Scene: "+error);
    AssertExportOrdinaryPath(assetRoot);size_t entries=0;
    for(auto it=std::filesystem::recursive_directory_iterator(assetRoot);it!=std::filesystem::recursive_directory_iterator();++it){
        if(++entries>65536)throw std::runtime_error("Export Sprite inventory exceeds 65536 entries");
        const auto relative=it->path().lexically_relative(assetRoot);const auto first=relative.begin();
        if(first!=relative.end() && (*first==L".kubo" || *first==L".git" || *first==L".vs" || *first==L"Library" || *first==L"Build" || *first==L"Builds" || *first==L"generated")){it.disable_recursion_pending();continue;}
        AssertExportOrdinaryPath(it->path());
    }
    AssetRegistry registry;if(!registry.Scan(assetRoot,error,AssetRegistry::MetadataPolicy::ReadOnly))throw std::runtime_error(error);
    return ValidateExportSpriteDependencies(world,registry,root,copyRoots);
}
}
