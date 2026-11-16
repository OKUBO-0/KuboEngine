#pragma once
#include "../entity/EntityWorld.h"
#include "../../base/AtomicFile.h"
#include <unordered_map>
#include <unordered_set>
#include <cmath>

namespace Engine::Prefab {
using PrefabValue=Scene::SceneValue;
inline bool PrefabBounds(const PrefabValue& value,size_t depth,size_t& structures,size_t& textBytes,std::string& error) {
    if(depth>64){error="Prefab nesting exceeds 64 levels";return false;}
    if(value.is_object() || value.is_array())if(++structures>65536){error="Prefab structure budget exceeded";return false;}
    const auto countText=[&](const std::string& text){if(text.size()>Scene::SceneDocument::MaxBytes-textBytes){error="Prefab text budget exceeded";return false;}
        if(text.find('\0')!=std::string::npos || (!text.empty() && MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0)==0)){error="Prefab strings must be valid UTF-8 without NUL";return false;}textBytes+=text.size();return true;};
    if(value.is_string())return countText(value.get<crude_json::string>());
    if(value.is_object())for(const auto& [key,child]:value.get<crude_json::object>()){
        if(!countText(key) || !PrefabBounds(child,depth+1,structures,textBytes,error))return false;
    }
    if(value.is_array())for(const auto& child:value.get<crude_json::array>())if(!PrefabBounds(child,depth+1,structures,textBytes,error))return false;
    if(value.is_number() && !std::isfinite(value.get<crude_json::number>())){error="Prefab number is not finite";return false;}
    if(value.is_discarded()){error="Invalid Prefab JSON value";return false;}return true;
}
inline bool PrefabKnownKeys(const PrefabValue& input,const PrefabValue& canonical,std::string& error,const std::string& path) {
    if(!input.is_object() || !canonical.is_object())return true;
    for(const auto& [key,child]:input.get<crude_json::object>()) {
        if(!canonical.contains(key)){error="Unsupported Prefab field: "+path+"/"+key;return false;}
        if(!PrefabKnownKeys(child,canonical[key],error,path+"/"+key))return false;
    }
    return true;
}
inline bool ValidatePrefabDocument(const PrefabValue& document,PrefabValue& normalized,std::string& error) {
    size_t structures=0,textBytes=0;if(!PrefabBounds(document,0,structures,textBytes,error))return false;
    const auto reject=[&](const char* why){error=why;return false;};
    if(document.dump().size()>Scene::SceneDocument::MaxBytes)return reject("Prefab exceeds 4 MiB");
    if(!document.is_object() || document.get<crude_json::object>().size()!=3 || !document.contains("format") ||
        !document["format"].is_string() || document["format"].get<crude_json::string>()!="kubo-prefab-1" ||
        !document.contains("root") || !document["root"].is_number() || !document.contains("world"))return reject("Invalid Prefab format/root/world");
    const double rootNumber=document["root"].get<crude_json::number>();
    if(rootNumber<1 || rootNumber>9007199254740991.0 || std::floor(rootNumber)!=rootNumber)return reject("Invalid JSON-exact Prefab root ID");
    const auto& serialized=document["world"];
    if(!serialized.is_object() || serialized.get<crude_json::object>().size()!=2 || !serialized.contains("version") || !serialized.contains("entities"))return reject("Unsupported Prefab World fields");
    if(!serialized["entities"].is_array() || serialized["entities"].get<crude_json::array>().empty() || serialized["entities"].get<crude_json::array>().size()>10000)return reject("Prefab must contain 1..10000 Entities");
    for(const auto& node:serialized["entities"].get<crude_json::array>())if(node.is_object() && node.contains("prefabLink"))return reject("Prefab definitions must not contain instance links");
    Scene::EntityWorld checked;if(!checked.Restore(serialized,error))return false;
    const auto root=Scene::EntityId(rootNumber);const auto* rootEntity=checked.Find(root);
    if(!rootEntity || rootEntity->parent)return reject("Prefab root must exist and have no external parent");
    for(const auto& entity:checked.GetEntities())if(!entity.parent && entity.id!=root)return reject("Prefab must be one connected subtree");
    const auto canonical=checked.Capture();const auto& inputNodes=serialized["entities"].get<crude_json::array>();const auto& canonicalNodes=canonical["entities"].get<crude_json::array>();
    for(size_t index=0;index<inputNodes.size();++index)if(!PrefabKnownKeys(inputNodes[index],canonicalNodes[index],error,"entities/"+std::to_string(checked.GetEntities()[index].id)))return false;
    PrefabValue candidate;candidate["format"]="kubo-prefab-1";candidate["root"]=rootNumber;candidate["world"]=canonical;
    if(candidate.dump().size()>Scene::SceneDocument::MaxBytes)return reject("Normalized Prefab exceeds 4 MiB");
    normalized=std::move(candidate);error.clear();return true;
}
inline bool CapturePrefabSubtree(const Scene::EntityWorld& world,Scene::EntityId root,PrefabValue& output,std::string& error) {
    const auto full=world.Capture();Scene::EntityWorld checked;if(!checked.Restore(full,error))return false;
    if(!world.Find(root)){error="Prefab capture root does not exist";return false;}
    std::unordered_map<Scene::EntityId,std::vector<Scene::EntityId>> children;
    for(const auto& entity:world.GetEntities())children[entity.parent].push_back(entity.id);
    std::vector<Scene::EntityId> order{root};std::unordered_set<Scene::EntityId> members{root};
    for(size_t index=0;index<order.size();++index)for(const auto child:children[order[index]])if(members.insert(child).second){
        if(order.size()>=10000){error="Prefab subtree exceeds 10000 Entities";return false;}order.push_back(child);
    }
    PrefabValue candidate;candidate["format"]="kubo-prefab-1";candidate["root"]=double(root);candidate["world"]["version"]=1.0;candidate["world"]["entities"]=crude_json::array{};
    for(const auto& entity:full["entities"].get<crude_json::array>())if(members.contains(Scene::EntityId(entity["id"].get<crude_json::number>()))) {
        auto node=entity;node.erase("prefabLink");if(node["id"].get<crude_json::number>()==double(root))node["parent"]=0.0;candidate["world"]["entities"].push_back(std::move(node));
    }
    Scene::RetagEntityDocument(candidate["world"]);return ValidatePrefabDocument(candidate,output,error);
}
inline bool ReadPrefabDocument(const std::filesystem::path& path,PrefabValue& output,std::string& error) {
    PrefabValue read;if(!Scene::SceneDocument::Read(path,read,error))return false;return ValidatePrefabDocument(read,output,error);
}
inline bool SavePrefabDocument(const std::filesystem::path& path,const PrefabValue& document,std::string& error,bool replaceExisting=false) {
    PrefabValue normalized;if(!ValidatePrefabDocument(document,normalized,error))return false;
    return Base::AtomicFile::Write(path,normalized.dump(),Scene::SceneDocument::MaxBytes,error,replaceExisting);
}
}
