#pragma once

#include "ModelReference.h"
#include "../base/AtomicFile.h"
#include "../base/BoundedJson.h"

namespace Engine::Assets {

inline constexpr size_t MaterialAssetMaximumBytes=16*1024;

inline bool ReadMaterialAsset(const std::filesystem::path& source,Scene::MaterialComponent& material,
    std::string& error,std::string* revision=nullptr) {
    try {
        const auto size=std::filesystem::file_size(source);
        if(size>MaterialAssetMaximumBytes)throw std::runtime_error("Material asset exceeds 16 KiB");
        std::ifstream input(source,std::ios::binary);
        std::string bytes(static_cast<size_t>(size),'\0');
        if(!input.read(bytes.data(),static_cast<std::streamsize>(size)) || input.peek()!=EOF)
            throw std::runtime_error("Cannot read complete Material asset");
        if(!Base::BoundedJson::Validate(bytes,{MaterialAssetMaximumBytes,4,32},error))return false;
        const auto document=Scene::SceneValue::parse(bytes);
        Scene::MaterialComponent candidate;
        if(!Scene::RenderComponentDetail::Keys(document,{"version","material"}) ||
            !document.contains("version") || !document["version"].is_number() ||
            document["version"].get<crude_json::number>()!=1 || !document.contains("material") ||
            !Scene::DecodeMaterial(document["material"],candidate) || !candidate.assetGuid.empty())
            throw std::runtime_error("Invalid Material asset version or settings");
        material=std::move(candidate);
        if(revision)revision->swap(bytes);
        error.clear();
        return true;
    } catch(const std::exception& exception) {error=exception.what();return false;}
}

inline std::filesystem::path ResolveMaterialAsset(const std::string& guid,const AssetRegistry& registry,
    std::string& error) {
    if(guid.empty()){error="Missing Material GUID";return {};}
    return ResolveAssetReference({},guid,AssetType::Material,registry,{},error);
}

inline bool WriteMaterialAsset(const std::filesystem::path& source,const Scene::MaterialComponent& material,
    std::string& error,bool replaceExisting) {
    try {
        if(!material.IsValid() || !material.assetGuid.empty() || AssetRegistry::Classify(source)!=AssetType::Material)
            throw std::runtime_error("Invalid shared Material settings or file extension");
        Scene::SceneValue document;
        document["version"]=1.0;
        document["material"]=Scene::EncodeMaterial(material);
        return Base::AtomicFile::Write(source,document.dump(2),MaterialAssetMaximumBytes,error,replaceExisting);
    } catch(const std::exception& exception) {error=exception.what();return false;}
}

} // namespace Engine::Assets
