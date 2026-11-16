#pragma once
#include "PrefabDocument.h"
#include "../base/ProjectContext.h"
#include "../assets/AssetRegistry.h"
#include <stdexcept>

namespace Engine::Editor {
namespace ProjectPrefabDetail {
inline bool Within(const std::filesystem::path& path,const std::filesystem::path& root) {
    auto part=path.begin();
    for(const auto& component:root) {
        if(part==path.end() || _wcsicmp(part->c_str(),component.c_str())!=0)return false;
        ++part;
    }
    return part!=path.end();
}
// Validate both portable names and canonical containment. This is not a held
// filesystem transaction against concurrent junction replacement.
inline std::filesystem::path Path(const Base::ProjectContext& project,const std::filesystem::path& selection) {
    namespace fs=std::filesystem;
    if(!project.IsOpen() || selection.empty() || selection.native().find(L'\0')!=std::wstring::npos)
        throw std::runtime_error("Prefab requires an open project and a file path");
    const auto root=project.ResolvePath(project.GetSettings().assetDirectory).lexically_normal();
    const auto owner=fs::canonical(project.GetRootPath());
    const auto canonicalRoot=fs::canonical(root);
    if(!Within(canonicalRoot,owner))throw std::runtime_error("Prefab asset root must be inside the project");
    const auto file=selection.is_absolute()?selection.lexically_normal():project.ResolveAssetPath(selection).lexically_normal();
    if(!Within(file,root) || _wcsicmp(file.extension().c_str(),L".prefab")!=0)
        throw std::runtime_error("Prefab must be a .prefab file inside the active asset root");
    const auto relative=file.lexically_relative(root);
    if(Base::PathToUtf8(relative).size()>4096)throw std::runtime_error("Prefab path exceeds 4096 UTF-8 bytes");
    for(const auto& part:relative) {
        const auto name=part.native();
        if(name.empty() || name==L"." || name==L".." || name.back()==L'.' || name.back()==L' ' || name.find_first_of(L":<>\"|?*")!=std::wstring::npos)
            throw std::runtime_error("Invalid Prefab path component");
        for(const auto character:name)if(character<32)throw std::runtime_error("Control character in Prefab path");
        auto device=name.substr(0,name.find(L'.'));
        for(auto& c:device)if(c>=L'a' && c<=L'z')c-=L'a'-L'A';
        if(device==L"CON" || device==L"PRN" || device==L"AUX" || device==L"NUL" || device==L"CONIN$" || device==L"CONOUT$" ||
            (device.size()==4 && (device.starts_with(L"COM") || device.starts_with(L"LPT")) &&
                ((device[3]>=L'0' && device[3]<=L'9') || device[3]==L'\u00b9' || device[3]==L'\u00b2' || device[3]==L'\u00b3')))
            throw std::runtime_error("Reserved device name in Prefab path");
        if(_wcsicmp(name.c_str(),L".kubo")==0 || _wcsicmp(name.c_str(),L".git")==0 || _wcsicmp(name.c_str(),L".vs")==0 ||
            _wcsicmp(name.c_str(),L"Library")==0 || _wcsicmp(name.c_str(),L"Build")==0 || _wcsicmp(name.c_str(),L"Builds")==0 || _wcsicmp(name.c_str(),L"generated")==0)
            throw std::runtime_error("Prefab cannot use a protected asset directory");
    }
    if(!Within(fs::weakly_canonical(file),canonicalRoot))throw std::runtime_error("Prefab resolves outside the asset root");
    if(fs::exists(root/".kubo/asset-move.pending.json"))throw std::runtime_error("Recover the pending asset move before Prefab authoring");
    return file;
}
}
// New-source capture only: no overwrite, implicit linking, identity generation,
// registry publication or Scene history changes. Existing import workers own
// GUID metadata/cache/database publication after the file has been saved.
inline bool SaveProjectPrefab(const Base::ProjectContext& project,const Scene::EntityWorld& world,
    Scene::EntityId root,const std::filesystem::path& selection,std::filesystem::path& savedPath,std::string& error) {
    try {
        const auto file=ProjectPrefabDetail::Path(project,selection);
        if(std::filesystem::exists(Assets::AssetRegistry::MetadataPath(file)))throw std::runtime_error("Prefab destination has existing identity metadata; choose a fresh path");
        PrefabValue definition;if(!CapturePrefabSubtree(world,root,definition,error))return false;
        if(!SavePrefabDocument(file,definition,error,false))return false;
        savedPath=file;error.clear();return true;
    } catch(const std::exception& exception){error=exception.what();return false;}
}
// Resolve the selected GUID against a fresh read-only scan, requiring existing
// identity metadata. Missing metadata must not manufacture a fallback identity.
inline bool ReadProjectPrefab(const Base::ProjectContext& project,const Assets::AssetRegistry& registry,
    const std::string& guid,PrefabValue& definition,std::string& error) {
    try {
        const auto* record=registry.FindByGuid(guid);
        if(!record || record->type!=Assets::AssetType::Prefab)throw std::runtime_error("Select a registered Prefab GUID");
        const auto file=ProjectPrefabDetail::Path(project,Base::PathFromUtf8(record->absolutePath));
        if(!std::filesystem::is_regular_file(Assets::AssetRegistry::MetadataPath(file)))throw std::runtime_error("Prefab identity metadata is missing; refresh assets");
        Assets::AssetRegistry current;
        const auto assetRoot=project.ResolvePath(project.GetSettings().assetDirectory);
        if(!current.Scan(assetRoot,error,Assets::AssetRegistry::MetadataPolicy::ReadOnly))return false;
        const auto* fresh=current.FindByGuid(guid);
        if(!fresh || fresh->type!=Assets::AssetType::Prefab || Base::PathFromUtf8(fresh->absolutePath)!=file)
            throw std::runtime_error("Prefab selection is stale; refresh assets and select it again");
        return ReadPrefabDocument(file,definition,error);
    } catch(const std::exception& exception){error=exception.what();return false;}
}
}
