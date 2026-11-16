#pragma once
#include "PrefabDocument.h"
#include <map>
#include <optional>
#include <set>

namespace Engine::Editor {
struct PrefabMergeReport {
    std::vector<std::string> inherited,overrides;
    std::vector<Scene::EntityId> added,removed,retainedRemoved;
};
inline bool PrefabEqual(const PrefabValue* a,const PrefabValue* b){return a && b?a->dump()==b->dump():a==b;}
inline const PrefabValue* PrefabField(const PrefabValue* value,const std::string& key){return value && value->is_object() && value->contains(key)?&(*value)[key]:nullptr;}
inline bool MergePrefabField(const PrefabValue* oldBase,const PrefabValue* newBase,const PrefabValue* mine,
    std::optional<PrefabValue>& output,PrefabMergeReport& report,const std::string& path,size_t depth,size_t& visits,std::string& error) {
    if(depth>64 || ++visits>65536){error="Prefab merge field budget exceeded";return false;}
    if(PrefabEqual(mine,oldBase)) {
        if(!PrefabEqual(oldBase,newBase))report.inherited.push_back(path);
        if(newBase)output=*newBase;else output.reset();return true;
    }
    if(oldBase && newBase && mine && oldBase->is_object() && newBase->is_object() && mine->is_object()) {
        std::set<std::string> keys;for(const auto* value:{oldBase,newBase,mine})for(const auto& [key,unused]:value->get<crude_json::object>())keys.insert(key);
        PrefabValue merged=crude_json::object{};
        for(const auto& key:keys){std::optional<PrefabValue> field;if(!MergePrefabField(PrefabField(oldBase,key),PrefabField(newBase,key),PrefabField(mine,key),field,report,path+"/"+key,depth+1,visits,error))return false;if(field)merged[key]=std::move(*field);}
        output=std::move(merged);return true;
    }
    report.overrides.push_back(path);if(mine)output=*mine;else output.reset();return true;
}
// Pure normalized-local-ID merge. Does not mutate a live Scene or write assets.
inline bool MergePrefabDocuments(const PrefabValue& oldInput,const PrefabValue& newInput,const PrefabValue& mineInput,
    PrefabValue& output,PrefabMergeReport& report,std::string& error) {
    PrefabValue oldBase,newBase,mine;
    if(!ValidatePrefabDocument(oldInput,oldBase,error) || !ValidatePrefabDocument(newInput,newBase,error) || !ValidatePrefabDocument(mineInput,mine,error))return false;
    if(oldBase["root"].get<crude_json::number>()!=newBase["root"].get<crude_json::number>() || oldBase["root"].get<crude_json::number>()!=mine["root"].get<crude_json::number>()){error="Prefab root local identity changed";return false;}
    using Id=Scene::EntityId;using Nodes=std::map<Id,const PrefabValue*>;
    const auto index=[](const PrefabValue& document){Nodes result;for(const auto& node:document["world"]["entities"].get<crude_json::array>())result.emplace(Id(node["id"].get<crude_json::number>()),&node);return result;};
    const auto oldNodes=index(oldBase),newNodes=index(newBase),mineNodes=index(mine);
    std::set<Id> ids;for(const auto* nodes:{&oldNodes,&newNodes,&mineNodes})for(const auto& [id,unused]:*nodes)ids.insert(id);
    const auto find=[](const Nodes& nodes,Id id)->const PrefabValue*{const auto found=nodes.find(id);return found==nodes.end()?nullptr:found->second;};
    PrefabValue candidate=mine;candidate["world"]["entities"]=crude_json::array{};PrefabMergeReport candidateReport;size_t visits=0;
    for(const auto id:ids) {
        const auto* old=find(oldNodes,id);const auto* next=find(newNodes,id);const auto* instance=find(mineNodes,id);
        const auto path="entities/"+std::to_string(id);
        if(!old) {
            if(next && instance){error="Prefab source/instance addition local-ID collision at "+path;return false;}
            candidate["world"]["entities"].push_back(next?*next:*instance);candidateReport.added.push_back(id);continue;
        }
        if(!instance){candidateReport.overrides.push_back(path+"/(deleted)");candidateReport.removed.push_back(id);continue;}
        if(!next) {
            auto oldComparable=*old,mineComparable=*instance;oldComparable.erase("name");mineComparable.erase("name");
            if(oldComparable.dump()==mineComparable.dump()){candidateReport.removed.push_back(id);continue;}
            candidate["world"]["entities"].push_back(*instance);candidateReport.retainedRemoved.push_back(id);candidateReport.overrides.push_back(path+"/(source-deleted)");continue;
        }
        // Identity and names are instance-owned, not source-inherited fields.
        // Merge other fields individually so the report cannot claim a source rename
        // was inherited when the resulting instance deliberately retains its name.
        PrefabValue merged=crude_json::object{};
        merged["id"]=double(id);merged["name"]=(*instance)["name"];
        std::set<std::string> keys;
        for(const auto* node:{old,next,instance})for(const auto& [key,unused]:node->get<crude_json::object>())if(key!="id" && key!="name")keys.insert(key);
        for(const auto& key:keys){std::optional<PrefabValue> field;if(!MergePrefabField(PrefabField(old,key),PrefabField(next,key),PrefabField(instance,key),field,candidateReport,path+"/"+key,1,visits,error))return false;if(field)merged[key]=std::move(*field);}
        candidate["world"]["entities"].push_back(std::move(merged));
    }
    Scene::RetagEntityDocument(candidate["world"]);PrefabValue normalized;if(!ValidatePrefabDocument(candidate,normalized,error)){error="Prefab merge structural/schema conflict: "+error;return false;}
    output=std::move(normalized);report=std::move(candidateReport);error.clear();return true;
}
}
