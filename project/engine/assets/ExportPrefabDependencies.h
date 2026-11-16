#pragma once
#include "ExportControllerDependencies.h"
#include "ModelReference.h"
#include "ExportSpriteDependencies.h"
#include "ExportParticleDependencies.h"
#include "../script/ScriptPrefabAdmission.h"
#include "../audio/WaveFile.h"

namespace Engine::Assets {
struct ExportPrefabClosure {size_t prefabs=0,opaqueScripts=0;ExportDependencyClosure models;ExportSpriteClosure sprites;ExportParticleClosure particles;};
// Inspect every registered exported template: Lua can choose paths dynamically.
// Match startup Scene's compatibility contract for path-only native Script keys.
inline ExportPrefabClosure ValidateExportPrefabDependencies(const std::filesystem::path& projectFile,
    const std::vector<std::filesystem::path>& prefabs,const std::vector<std::filesystem::path>& copyRoots){
    using Admission=Scripting::ScriptPrefabAdmission;
    if(prefabs.size()>65536)throw std::runtime_error("Too many export Prefabs");
    Base::ProjectContext project;std::string error;if(!project.Open(projectFile,error))throw std::runtime_error(error);
    const auto root=project.GetRootPath(),assetRoot=project.ResolvePath(project.GetSettings().assetDirectory);
    AssertExportOrdinaryPath(root);AssertExportOrdinaryPath(assetRoot);
    size_t entries=0;
    for(auto it=std::filesystem::recursive_directory_iterator(assetRoot);it!=std::filesystem::recursive_directory_iterator();++it){
        if(++entries>65536)throw std::runtime_error("Export Prefab inventory exceeds 65536 entries");
        const auto relative=it->path().lexically_relative(assetRoot);const auto first=relative.begin();
        if(first!=relative.end() && (*first==L".kubo" || *first==L".git" || *first==L".vs" || *first==L"Library" || *first==L"Build" || *first==L"Builds" || *first==L"generated")){it.disable_recursion_pending();continue;}
        AssertExportOrdinaryPath(it->path());
    }
    AssetRegistry registry;if(!registry.Scan(assetRoot,error,AssetRegistry::MetadataPolicy::ReadOnly))throw std::runtime_error(error);
    const auto copied=[&](const std::filesystem::path& source){
        AssertExportOrdinaryPath(source);AssertExportPortableRelative(source.lexically_relative(root));
        for(const auto& relative:copyRoots){AssertExportPortableRelative(relative);if(ExportPathWithin(source,(root/relative).lexically_normal()))return;}
        throw std::runtime_error("Export Prefab dependency outside copied directories: "+Base::PathToUtf8(source));
    };
    std::unordered_set<std::string> visited,modelSet,waves,controllers;
    std::vector<std::filesystem::path> models;ExportPrefabClosure result;
    const auto sourceRef=[&](const std::string& hint,const std::string& guid,AssetType type){
        if(!hint.empty())AssertExportPortableRelative(Base::PathFromUtf8(hint));
        std::filesystem::path path;
        if(!guid.empty()){
            Admission::Reference ref;if(Admission::ReferenceAsset(registry,root,hint,guid,type,ref)!=Admission::Result::Success)throw std::runtime_error("Export Prefab typed GUID reference refused");
            path=std::move(ref.source);copied(AssetRegistry::MetadataPath(path));
        }else{
            path=ResolveAssetReference(hint,"",type,registry,root,error);if(path.empty())throw std::runtime_error("Export Prefab source: "+error);
        }
        copied(path);return path;
    };
    for(const auto& relative:prefabs){
        AssertExportPortableRelative(relative);const auto source=(root/relative).lexically_normal();
        Admission::Reference ref;if(Admission::ReferenceAsset(registry,root,Base::PathToUtf8(relative),"",AssetType::Prefab,ref)!=Admission::Result::Success)throw std::runtime_error("Export Prefab source/identity refused");
        const auto key=Base::PathToUtf8(ref.source);if(!visited.insert(key).second)continue;copied(ref.source);if(!ref.guid.empty())copied(AssetRegistry::MetadataPath(ref.source));
        Prefab::PrefabValue value;if(Admission::ReadDefinition(source,value)!=Admission::Result::Success)throw std::runtime_error("Export Prefab strict definition refused");
        Scene::EntityWorld world;if(!world.Restore(value["world"],error))throw std::runtime_error(error);
        const auto sprites=ValidateExportSpriteDependencies(world,registry,root,copyRoots);
        result.sprites.sprites+=sprites.sprites;result.sprites.textures+=sprites.textures;result.sprites.unassigned+=sprites.unassigned;
        const auto particles=ValidateExportParticleDependencies(world,registry,root,copyRoots);result.particles.emitters+=particles.emitters;result.particles.textures+=particles.textures;result.particles.unassigned+=particles.unassigned;
        for(const auto& entity:world.GetEntities()){
            if(entity.hasModel){const auto path=sourceRef(entity.model.assetPath,entity.model.assetGuid,AssetType::Model);const auto relativeModel=Base::PathToUtf8(path.lexically_relative(root));if(modelSet.insert(relativeModel).second)models.emplace_back(Base::PathFromUtf8(relativeModel));}
            if(entity.hasScript){if(entity.script.assetGuid.empty())++result.opaqueScripts;else{const auto path=sourceRef(entity.script.assetPath,entity.script.assetGuid,AssetType::Script);if(std::filesystem::file_size(path)>1024*1024)throw std::runtime_error("Export Prefab Script exceeds 1 MiB");}}
            if(entity.hasAudio && (!entity.audio.assetPath.empty() || !entity.audio.assetGuid.empty())){
                const auto path=sourceRef(entity.audio.assetPath,entity.audio.assetGuid,AssetType::Audio);
                if(waves.insert(Base::PathToUtf8(path)).second){AudioSystem::SoundData data;if(!AudioSystem::WaveFile::Load(path,data,error))throw std::runtime_error("Export Prefab Wave: "+error);}
            }
            if(entity.hasAnimator && !entity.animator.controllerGuid.empty())controllers.insert(entity.animator.controllerGuid);
        }
    }
    // Catch caller enumeration omissions without scanning into ignored archives.
    for(const auto& record:registry.GetAssets())if(record.type==AssetType::Prefab && !visited.contains(Base::PathToUtf8(registry.ResolveGuid(record.guid).lexically_normal())))throw std::runtime_error("Export Prefab inventory omitted a registered template");
    result.prefabs=visited.size();
    if(!models.empty())result.models=ValidateExportModelDependencies(root,models,copyRoots);
    if(!controllers.empty())ValidateExportControllerDependencies(projectFile,std::vector<std::string>(controllers.begin(),controllers.end()),copyRoots);
    return result;
}
}
