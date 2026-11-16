#pragma once
#include "PrefabOverrides.h"

namespace Engine::Editor {
enum class PrefabThreeWayChange { Unchanged, InstanceOnly, SourceOnly, Converged, Conflict };
enum class PrefabThreeWayKind { Field, InstanceName, EntityPresence };
struct PrefabThreeWayEntry {
    PrefabThreeWayKind kind=PrefabThreeWayKind::Field;
    PrefabThreeWayChange change=PrefabThreeWayChange::Unchanged;
    Scene::EntityId local=0,scene=0;
    std::string name,path;
    // Canonical JSON text, not rendered/truncated text. Missing != JSON null.
    std::optional<std::string> baseline,source,instance;
};
struct PrefabThreeWayReport {
    Scene::EntityId root=0;
    std::string sourceGuid;
    std::vector<PrefabThreeWayEntry> entries;
};
static_assert(std::is_nothrow_move_assignable_v<PrefabThreeWayReport>);
inline PrefabThreeWayChange ClassifyPrefabThreeWayValues(const std::optional<std::string>& baseline,
    const std::optional<std::string>& source,const std::optional<std::string>& instance) noexcept {
    if(source==baseline)return instance==baseline?PrefabThreeWayChange::Unchanged:PrefabThreeWayChange::InstanceOnly;
    if(instance==baseline)return PrefabThreeWayChange::SourceOnly;
    return source==instance?PrefabThreeWayChange::Converged:PrefabThreeWayChange::Conflict;
}
// Inspection only: no merge, resolution, baseline advance or source writes.
// The caller owns source revision freshness/metadata; this checks definition,
// source GUID binding and stable-local root identity, not native file identity.
inline bool InspectPrefabThreeWay(const Scene::EntityWorld& world,Scene::EntityId root,
    const PrefabValue& currentSource,const std::string& guid,PrefabThreeWayReport& output,std::string& error) {
    try {
        PrefabValue source;if(!ValidatePrefabDocument(currentSource,source,error))return false;
        PrefabInstanceCapture capture;if(!CapturePrefabInstance(world,root,capture,error,&source))return false;
        if(world.Find(root)->prefabLink["sourceGuid"].get<crude_json::string>()!=guid){error="Three-way source GUID mismatch";return false;}
        using Id=Scene::EntityId;using Nodes=std::map<Id,const PrefabValue*>;
        Nodes baseNodes,sourceNodes,mineNodes;
        const auto index=[](const PrefabValue& document,Nodes& nodes){
            for(const auto& node:document["world"]["entities"].get<crude_json::array>())nodes.emplace(Id(node["id"].get<crude_json::number>()),&node);
        };
        index(capture.baseline,baseNodes);index(source,sourceNodes);index(capture.instance,mineNodes);
        PrefabThreeWayReport candidate;candidate.root=root;candidate.sourceGuid=guid;
        size_t visits=0,text=0;constexpr size_t TextBudget=3*Scene::SceneDocument::MaxBytes;
        const auto serialize=[](const PrefabValue* value)->std::optional<std::string>{if(value)return value->dump();return std::nullopt;};
        const auto compare=[&](auto&& self,Id id,const std::string& name,const std::string& path,
            const PrefabValue* base,const PrefabValue* next,const PrefabValue* mine,bool presence,size_t depth)->bool {
            if(depth>64 || ++visits>65536){error="Three-way field budget exceeded";return false;}
            const auto baseline=serialize(base),incoming=serialize(next),authored=serialize(mine);
            const auto change=ClassifyPrefabThreeWayValues(baseline,incoming,authored);
            if(change==PrefabThreeWayChange::Unchanged)return true;
            if(!presence && base && next && mine && base->is_object() && next->is_object() && mine->is_object()) {
                std::set<std::string> keys;for(const auto* value:{base,next,mine})for(const auto& [key,unused]:value->get<crude_json::object>())keys.insert(key);
                for(const auto& key:keys)if(!self(self,id,name,path+"/"+PrefabOverridePointerToken(key),
                    base->contains(key)?&(*base)[key]:nullptr,next->contains(key)?&(*next)[key]:nullptr,
                    mine->contains(key)?&(*mine)[key]:nullptr,false,depth+1))return false;
                return true;
            }
            PrefabThreeWayEntry entry;entry.kind=presence?PrefabThreeWayKind::EntityPresence:path=="/name"?PrefabThreeWayKind::InstanceName:PrefabThreeWayKind::Field;
            entry.change=change;entry.local=id;entry.scene=capture.sceneByLocal.contains(id)?capture.sceneByLocal.at(id):0;
            entry.name=name;entry.path=path;entry.baseline=baseline;entry.source=incoming;entry.instance=authored;
            const auto bytes=name.size()+path.size()+(baseline?baseline->size():0)+(incoming?incoming->size():0)+(authored?authored->size():0);
            if(bytes>TextBudget-text || candidate.entries.size()>=65536){error="Three-way report budget exceeded";return false;}
            text+=bytes;candidate.entries.push_back(std::move(entry));return true;
        };
        std::set<Id> ids;for(const auto* nodes:{&baseNodes,&sourceNodes,&mineNodes})for(const auto& [id,unused]:*nodes)ids.insert(id);
        for(const auto id:ids) {
            const auto* base=baseNodes.contains(id)?baseNodes.at(id):nullptr;
            const auto* next=sourceNodes.contains(id)?sourceNodes.at(id):nullptr;
            const auto* mine=mineNodes.contains(id)?mineNodes.at(id):nullptr;
            const auto name=(*(mine?mine:next?next:base))["name"].get<crude_json::string>();
            if(!compare(compare,id,name,"",base,next,mine,!base || !next || !mine,0))return false;
        }
        output=std::move(candidate);error.clear();return true;
    }catch(const std::exception& exception){error=exception.what();return false;}
}
}
