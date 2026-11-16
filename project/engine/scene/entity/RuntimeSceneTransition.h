#pragma once

#include "../../assets/AssetMove.h"
#include "../../assets/ModelReference.h"
#include "../../script/ScriptSceneTransition.h"

namespace Engine::Scene {

inline bool LoadRuntimeScene(const Scripting::ScriptSceneTransition& request,
    const std::filesystem::path& projectRoot,const std::filesystem::path& assetRoot,
    const Assets::AssetRegistry& assets,EntityWorld& candidate,std::filesystem::path& destination,
    std::string& error)
{
    namespace fs = std::filesystem;
    const auto inside = [](const fs::path& path,const fs::path& base) {
        auto part = path.begin();
        for (const auto& component : base) {
            if (part==path.end() || CompareStringOrdinal(part->c_str(),-1,component.c_str(),-1,TRUE)!=CSTR_EQUAL)
                return false;
            ++part;
        }
        return true;
    };
    try {
        if (projectRoot.empty() || assetRoot.empty()) throw std::runtime_error("Scene transitions need a project asset root");
        const auto project = fs::canonical(projectRoot);
        const auto root = fs::canonical(assetRoot);
        if (!inside(root,project)) throw std::runtime_error("Scene transitions need a project-local asset root");
        fs::path path;
        if (request.destination.starts_with("guid:")) {
            path = Assets::ResolveAssetReference({},request.destination.substr(5),Assets::AssetType::Scene,
                assets,project,error);
            if (path.empty()) return false;
        } else {
            path = root / Assets::MoveDetail::Relative(request.destination);
        }
        if (Assets::AssetRegistry::Classify(path)!=Assets::AssetType::Scene)
            throw std::runtime_error("Scene destination must be a .scene or .json asset");
        path = fs::canonical(path);
        if (!inside(path,root) || path==root || !fs::is_regular_file(path))
            throw std::runtime_error("Scene destination is outside the asset root or unavailable");
        if (!candidate.Load(path,error)) return false;
        destination.swap(path);
        return true;
    } catch (const std::exception& exception) {
        error=exception.what();
        return false;
    }
}

} // namespace Engine::Scene
