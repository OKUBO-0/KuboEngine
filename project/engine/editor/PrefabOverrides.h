#pragma once
#include "PrefabInstanceCapture.h"
#include <optional>
#include <type_traits>

namespace Engine::Editor {
enum class PrefabOverrideKind { Field, InstanceName, EntityAdded, EntityRemoved };
struct PrefabOverrideEntry {
    PrefabOverrideKind kind=PrefabOverrideKind::Field;
    Scene::EntityId local=0,scene=0;
    std::string name,path;
    // Missing fields are distinct from JSON null. Arrays are atomic, like merge.
    std::optional<std::string> baseline,instance;
};
struct PrefabOverrideReport {
    Scene::EntityId root=0;
    std::string sourceGuid;
    std::vector<PrefabOverrideEntry> entries;
};
static_assert(std::is_nothrow_move_assignable_v<PrefabOverrideReport>);
// Input strings have already passed UTF-8 validation. Never cut a code point
// when limiting the rendered JSON text (the retained report is not truncated).
inline size_t PrefabOverrideDisplayPrefix(const std::string& text,size_t limit) {
    auto bytes=(std::min)(text.size(),limit);
    if(bytes<text.size())while(bytes && (static_cast<unsigned char>(text[bytes])&0xc0)==0x80)--bytes;
    return bytes;
}
inline std::string PrefabOverridePointerToken(const std::string& key) {
    std::string result;
    for(const auto character:key) {
        if(character=='~')result+="~0";
        else if(character=='/')result+="~1";
        else result+=character;
    }
    return result;
}
// Read-only comparison with the persisted instance baseline, not the current
// source file. Root external parent and Scene IDs are intentionally not fields.
inline bool InspectPrefabOverrides(const Scene::EntityWorld& world,Scene::EntityId root,
    PrefabOverrideReport& output,std::string& error) {
    PrefabInstanceCapture captured;if(!CapturePrefabInstance(world,root,captured,error))return false;
    using Id=Scene::EntityId;
    std::map<Id,const PrefabValue*> baseline,instance;
    for(const auto& node:captured.baseline["world"]["entities"].get<crude_json::array>())baseline.emplace(Id(node["id"].get<crude_json::number>()),&node);
    for(const auto& node:captured.instance["world"]["entities"].get<crude_json::array>())instance.emplace(Id(node["id"].get<crude_json::number>()),&node);
    PrefabOverrideReport candidate;candidate.root=root;
    candidate.sourceGuid=world.Find(root)->prefabLink["sourceGuid"].get<crude_json::string>();
    size_t visits=0,text=0;
    const auto add=[&](PrefabOverrideKind kind,Id id,const std::string& name,const std::string& path,const PrefabValue* old,const PrefabValue* mine) {
        PrefabOverrideEntry entry;entry.kind=kind;entry.local=id;entry.scene=captured.sceneByLocal.at(id);entry.name=name;entry.path=path;
        if(old)entry.baseline=old->dump();if(mine)entry.instance=mine->dump();
        const auto bytes=entry.name.size()+entry.path.size()+(entry.baseline?entry.baseline->size():0)+(entry.instance?entry.instance->size():0);
        if(bytes>2*Scene::SceneDocument::MaxBytes-text || candidate.entries.size()>=65536){error="Prefab override report budget exceeded";return false;}
        text+=bytes;candidate.entries.push_back(std::move(entry));return true;
    };
    const auto compare=[&](auto&& self,Id id,const std::string& name,const std::string& path,const PrefabValue* old,const PrefabValue* mine,size_t depth)->bool {
        if(depth>64 || ++visits>65536){error="Prefab override field budget exceeded";return false;}
        if(old && mine && old->dump()==mine->dump())return true;
        if(!old && !mine)return true;
        if(old && mine && old->is_object() && mine->is_object()) {
            std::set<std::string> keys;
            for(const auto* value:{old,mine})for(const auto& [key,unused]:value->get<crude_json::object>())keys.insert(key);
            for(const auto& key:keys) {
                const auto* oldField=old->contains(key)?&(*old)[key]:nullptr;
                const auto* mineField=mine->contains(key)?&(*mine)[key]:nullptr;
                if(!self(self,id,name,path+"/"+PrefabOverridePointerToken(key),oldField,mineField,depth+1))return false;
            }
            return true;
        }
        return add(path=="/name"?PrefabOverrideKind::InstanceName:PrefabOverrideKind::Field,id,name,path,old,mine);
    };
    std::set<Id> ids;for(const auto* nodes:{&baseline,&instance})for(const auto& [id,unused]:*nodes)ids.insert(id);
    for(const auto id:ids) {
        const auto* old=baseline.contains(id)?baseline.at(id):nullptr;
        const auto* mine=instance.contains(id)?instance.at(id):nullptr;
        const auto name=(*(mine?mine:old))["name"].get<crude_json::string>();
        if(!old || !mine) {
            if(!add(old?PrefabOverrideKind::EntityRemoved:PrefabOverrideKind::EntityAdded,id,name,"",old,mine))return false;
        } else if(!compare(compare,id,name,"",old,mine,0))return false;
    }
    output=std::move(candidate);error.clear();return true;
}
}
