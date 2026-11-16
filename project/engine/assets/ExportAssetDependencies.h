#pragma once
#include "AssetDependencies.h"
#include <unordered_set>
#include <queue>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace Engine::Assets {
inline bool ExportPathWithin(const std::filesystem::path& path, const std::filesystem::path& root) {
    auto nativePath=path;auto nativeRoot=root;nativePath.make_preferred();nativeRoot.make_preferred();
    auto part=nativePath.begin();
    for(const auto& component:nativeRoot) {
        if(part==nativePath.end() || CompareStringOrdinal(component.c_str(),-1,part->c_str(),-1,TRUE)!=CSTR_EQUAL) return false;
        ++part;
    }
    return part!=nativePath.end();
}
inline void AssertExportOrdinaryPath(const std::filesystem::path& path) {
    for(auto current=path;!current.empty();) {
        const auto attributes=GetFileAttributesW(current.c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES || (attributes&FILE_ATTRIBUTE_REPARSE_POINT))
            throw std::runtime_error("Export dependency path missing or linked: "+Base::PathToUtf8(path));
        const auto parent=current.parent_path();if(parent==current) break;current=parent;
    }
}
inline void AssertExportPortableRelative(const std::filesystem::path& relative) {
    if(relative.empty() || relative.is_absolute() || relative.has_root_name() || relative.has_root_directory())
        throw std::runtime_error("Export dependency path must be project-relative");
    for(const auto& part:relative) {
        const auto text=part.wstring();
        if(text.empty() || text==L".." || text.back()==L'.' || text.back()==L' ' ||
            text.find_first_of(L"<>\":|?*")!=std::wstring::npos || text.find(L'\0')!=std::wstring::npos)
            throw std::runtime_error("Export dependency has a non-portable path");
    }
}
struct ExportDependencyClosure { size_t files=0,edges=0,uninspectedModels=0; };
// Reuse the Editor's actual parser; do not import or create sidecars during export.
inline ExportDependencyClosure ValidateExportModelDependencies(const std::filesystem::path& projectRoot,
    const std::vector<std::filesystem::path>& models,const std::vector<std::filesystem::path>& copyRoots) {
    const auto root=std::filesystem::absolute(projectRoot).lexically_normal();
    AssertExportOrdinaryPath(root);
    // Refuse junctions before recursive traversal/Scan and bound the source tree.
    size_t entries=0;
    for(auto it=std::filesystem::recursive_directory_iterator(root);it!=std::filesystem::recursive_directory_iterator();++it) {
        if(++entries>65536) throw std::runtime_error("Export dependency source tree exceeds 65536 entries");
        const auto relative=it->path().lexically_relative(root);const auto first=relative.begin();
        if(first!=relative.end() && (*first==L".kubo" || *first==L".git" || *first==L".vs" || *first==L"Library" ||
            *first==L"Build" || *first==L"Builds" || *first==L"generated")) {it.disable_recursion_pending();continue;}
        const auto attributes=GetFileAttributesW(it->path().c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES || (attributes&FILE_ATTRIBUTE_REPARSE_POINT))
            throw std::runtime_error("Export dependency tree contains a missing/linked entry");
    }
    AssetRegistry registry;std::string error;
    if(!registry.Scan(root,error,AssetRegistry::MetadataPolicy::ReadOnly)) throw std::runtime_error(error);
    const auto graph=InspectAssetDependencies(registry);
    std::queue<std::string> pending;std::unordered_set<std::string> visited,modelGuids;
    for(const auto& model:models) {
        AssertExportPortableRelative(model);
        const auto* record=registry.FindByPath((root/model).lexically_normal());
        if(!record) throw std::runtime_error("Export Model source missing from read-only registry");
        modelGuids.insert(record->guid);
        pending.push(record->guid);
    }
    ExportDependencyClosure result;
    while(!pending.empty()) {
        const auto guid=pending.front();pending.pop();if(!visited.insert(guid).second) continue;
        const auto* record=registry.FindByGuid(guid);
        if(!record) throw std::runtime_error("Export dependency identity missing");
        const auto source=Base::PathFromUtf8(record->absolutePath);
        const auto relative=source.lexically_relative(root);AssertExportPortableRelative(relative);
        AssertExportOrdinaryPath(source);
        bool copied=false;
        for(const auto& copyRoot:copyRoots) {
            AssertExportPortableRelative(copyRoot);
            if(ExportPathWithin(source,(root/copyRoot).lexically_normal())) {copied=true;break;}
        }
        if(!copied) {
            std::string roots;for(const auto& copyRoot:copyRoots) roots+=" ["+Base::PathToUtf8(root/copyRoot)+"]";
            throw std::runtime_error("Export Model dependency is outside copied directories: "+Base::PathToUtf8(source)+"; copied roots:"+roots);
        }
        for(const auto& issue:graph.issues) if(issue.ownerGuid==guid)
            throw std::runtime_error("Export dependency inspection failed: "+record->relativePath+": "+issue.message);
        ++result.files;
        auto extension=Base::PathToUtf8(source.extension());
        std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
        if(modelGuids.contains(guid) && extension!=".obj" && extension!=".gltf" && extension!=".glb") ++result.uninspectedModels;
        for(const auto* edge:graph.DependenciesOf(guid)) {
            ++result.edges;
            if(edge->missing || edge->targetGuid.empty()) throw std::runtime_error("Export Model dependency missing: "+edge->targetRelative);
            pending.push(edge->targetGuid);
        }
    }
    return result;
}
}
