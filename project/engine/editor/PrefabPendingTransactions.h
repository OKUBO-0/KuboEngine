#pragma once
#include "ProjectPrefab.h"
#include <algorithm>
#include <vector>

namespace Engine::Editor {
enum class PrefabPendingKind { History, Publication, Incomplete, Unknown };
struct PrefabPendingEntry {std::filesystem::path directory;std::string name,guid;PrefabPendingKind kind=PrefabPendingKind::Unknown;};
// Bounded namespace inventory only; contents are untrusted until native recovery
// inspection. Any entry, including partial/unknown data, blocks authoring.
class PrefabPendingTransactions final {
public:
    static constexpr size_t MaxEntries=128;
    static bool Scan(const Base::ProjectContext& project,std::vector<PrefabPendingEntry>& output,std::string& error){
        try {
            if(!project.IsOpen())throw std::runtime_error("Pending Prefab scan requires an open project");
            const auto root=std::filesystem::weakly_canonical(project.ResolvePath(project.GetSettings().assetDirectory));
            if(!ProjectPrefabDetail::Within(root,std::filesystem::canonical(project.GetRootPath())))throw std::runtime_error("Pending Prefab asset root escapes project");
            const auto rootAttributes=GetFileAttributesW(root.c_str());
            if(rootAttributes!=INVALID_FILE_ATTRIBUTES && !(rootAttributes&FILE_ATTRIBUTE_DIRECTORY))throw std::runtime_error("Pending Prefab asset root is not a directory");
            const auto pending=root/".kubo/prefab-transactions";std::vector<PrefabPendingEntry> candidate;
            for(const auto& path:{root/".kubo",pending}){
                const auto attributes=GetFileAttributesW(path.c_str());
                if(attributes==INVALID_FILE_ATTRIBUTES){const auto code=GetLastError();if(code==ERROR_FILE_NOT_FOUND || code==ERROR_PATH_NOT_FOUND){output=std::move(candidate);error.clear();return true;}throw std::runtime_error("Cannot inspect pending Prefab namespace");}
                if(!(attributes&FILE_ATTRIBUTE_DIRECTORY) || (attributes&FILE_ATTRIBUTE_REPARSE_POINT) || !ProjectPrefabDetail::Within(std::filesystem::canonical(path),root))
                    throw std::runtime_error("Pending Prefab namespace is linked/not a directory/unsafe");
            }
            for(const auto& entry:std::filesystem::directory_iterator(pending)){
                if(candidate.size()==MaxEntries)throw std::runtime_error("Pending Prefab inventory exceeds 128 entries; preserve data and resolve manually");
                PrefabPendingEntry item;item.directory=entry.path();item.name=Base::PathToUtf8(entry.path().filename());
                if(Scene::ModelComponent::IsValidGuid(item.name))item.guid=item.name;
                const auto attributes=GetFileAttributesW(entry.path().c_str());
                if(attributes==INVALID_FILE_ATTRIBUTES)throw std::runtime_error("Pending Prefab entry changed during inventory");
                if((attributes&FILE_ATTRIBUTE_DIRECTORY) && !(attributes&FILE_ATTRIBUTE_REPARSE_POINT) && !item.guid.empty()){
                    const auto exists=[&](const wchar_t* name){const auto attr=GetFileAttributesW((entry.path()/name).c_str());if(attr==INVALID_FILE_ATTRIBUTES){const auto code=GetLastError();if(code==ERROR_FILE_NOT_FOUND || code==ERROR_PATH_NOT_FOUND)return false;throw std::runtime_error("Pending Prefab marker cannot be inspected");}return true;};
                    const bool history=exists(L"history.json"),publication=exists(L"pending.json");
                    item.kind=history&&!publication?PrefabPendingKind::History:publication&&!history?PrefabPendingKind::Publication:!history&&!publication?PrefabPendingKind::Incomplete:PrefabPendingKind::Unknown;
                }
                candidate.push_back(std::move(item));
            }
            std::sort(candidate.begin(),candidate.end(),[](const auto& a,const auto& b){return a.name<b.name;});
            output=std::move(candidate);error.clear();return true;
        }catch(const std::exception& exception){error=exception.what();return false;}
    }
};
}
