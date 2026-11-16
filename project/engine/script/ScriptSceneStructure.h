#pragma once
#include "ScriptSceneView.h"
#include "ScriptProperty.h"
#include "../scene/entity/EntityWorld.h"
#include "../scene/prefab/RuntimePrefab.h"
#include <optional>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace Engine::Scripting {
// Structural transaction core, used by ScriptSceneCommands' lazy promotion.
// Runtime-thread/nonreentrant host contract: no live World mutation between
// Begin and publication. Publication invalidates EVERY borrowed record pointer.
// Prepare precedes any audio admission; PublishPrepared only swaps storage.
// Clear/disposal of the retired World takes place outside that publication step.
class ScriptSceneStructure final {
    friend class ScriptSceneCommands;
public:
    static constexpr size_t MaxOperations=64,MaxEntities=10000,MaxNameBytes=256;
    enum class Result { Success,Missing,Invalid,Unavailable,Limit,AllocationFailure };
    ScriptSceneStructure()=default;
    ScriptSceneStructure(const ScriptSceneStructure&)=delete;
    ScriptSceneStructure& operator=(const ScriptSceneStructure&)=delete;
    Result Begin(const ScriptSceneView* view) noexcept {
        Clear();view_=view;
        if(view){world_=view->writeWorld_;token_=view->scopeToken_;}
        if(!Context())return Result::Unavailable;
        if(world_->GetEntities().size()>MaxEntities)return Result::Limit;
        try {
            if(!Valid(*world_)){poisoned_=true;return Result::Invalid;}
            candidate_.emplace(*world_);return Result::Success;
        } catch(...) {poisoned_=true;return Result::AllocationFailure;}
    }
    void Clear() noexcept {
        candidate_.reset();view_=nullptr;world_=nullptr;token_=0;
        calls_=removed_=created_=0;prepared_=poisoned_=published_=false;
    }
    const Scene::EntityWorld* Candidate() const noexcept {
        return Context() && candidate_ && !poisoned_ && !published_ ? &*candidate_ : nullptr;
    }
    size_t OperationCount() const noexcept {return calls_;}
    size_t RemovedCount() const noexcept {return removed_;}
    size_t CreatedCount() const noexcept {return created_;}
    Result Spawn(std::string_view name,Scene::EntityId parent,Scene::EntityId& created) noexcept {
        if(!ValidScriptText(name,MaxNameBytes,true) || parent>Scene::EntityWorld::MaximumEntityId)return Result::Invalid;
        if(!Editable())return Result::Unavailable;
        if(parent && !candidate_->Find(parent))return Result::Missing;
        if(calls_==MaxOperations || created_==MaxOperations || candidate_->GetEntities().size()==MaxEntities)return Result::Limit;
        try {
            auto work=*candidate_;
            const auto id=work.CreateEntity(std::string(name),parent);
            if(!id)return Result::Invalid; // Includes invalid Prefab reservations/exhaustion.
            candidate_->Swap(work);++calls_;++created_;created=id;return Result::Success;
        } catch(...) {poisoned_=true;return Result::AllocationFailure;}
    }
    Result SpawnModel(std::string_view name,const Scene::ModelComponent& model,Scene::EntityId parent,Scene::EntityId& created) noexcept {
        if(!ValidScriptText(name,MaxNameBytes,true) || model.assetPath.empty() || parent>Scene::EntityWorld::MaximumEntityId)return Result::Invalid;
        if(!Editable())return Result::Unavailable;
        if(parent && !candidate_->Find(parent))return Result::Missing;
        if(calls_==MaxOperations || created_==MaxOperations || candidate_->GetEntities().size()==MaxEntities)return Result::Limit;
        try{
            auto work=*candidate_;const auto id=work.CreateEntity(std::string(name),parent);
            if(!id || !work.SetModel(id,model.assetPath,model.assetGuid))return Result::Invalid;
            candidate_->Swap(work);++calls_;++created_;created=id;return Result::Success;
        }catch(...){poisoned_=true;return Result::AllocationFailure;}
    }
    // Definition is already project-admitted by ScriptSceneCommands. Still
    // validate the value in the pure factory; native callers cannot bypass it.
    Result SpawnPrefab(const Prefab::PrefabValue& definition,Scene::EntityId parent,Scene::EntityId& created) noexcept {
        if(parent>Scene::EntityWorld::MaximumEntityId)return Result::Invalid;
        if(!Editable())return Result::Unavailable;
        if(parent && !candidate_->Find(parent))return Result::Missing;
        if(calls_==MaxOperations || created_==MaxOperations)return Result::Limit;
        try{
            if(!definition.is_object() || !definition.contains("world") || !definition["world"].is_object() || !definition["world"].contains("entities") || !definition["world"]["entities"].is_array()){poisoned_=true;return Result::Invalid;}
            const auto count=definition["world"]["entities"].get<crude_json::array>().size();
            if(count>MaxOperations-created_ || count>MaxEntities-candidate_->GetEntities().size())return Result::Limit;
            std::string error;
            if(!Prefab::InstantiateRuntimePrefab(*candidate_,definition,parent,created,error,MaxOperations-created_)){poisoned_=true;return Result::Invalid;}
            ++calls_;created_+=count;return Result::Success;
        }catch(...){poisoned_=true;return Result::AllocationFailure;}
    }
    // Runtime copy, not Editor linked duplication. Local TRS/component values
    // and asset references are retained; IDs are fresh and runtime instances
    // are not copied. Aggregate creations (including empty Spawn) are capped.
    Result Clone(Scene::EntityId source,Scene::EntityId parent,Scene::EntityId& created) noexcept {
        if(!source || source>Scene::EntityWorld::MaximumEntityId || parent>Scene::EntityWorld::MaximumEntityId)return Result::Invalid;
        if(!Editable())return Result::Unavailable;
        if(!candidate_->Find(source) || (parent && !candidate_->Find(parent)))return Result::Missing;
        if(calls_==MaxOperations || created_==MaxOperations)return Result::Limit;
        try {
            std::vector<Scene::EntityId> order{source};
            for(size_t index=0;index<order.size();++index){
                for(const auto& record:candidate_->GetEntities())if(record.parent==order[index]){
                    if(order.size()>=MaxOperations-created_)return Result::Limit;
                    order.push_back(record.id);
                }
            }
            if(order.size()>MaxEntities-candidate_->GetEntities().size())return Result::Limit;
            // Validate only the selected baked subtree, not unrelated runtime
            // authoring links. Strip Prefab links and external root ancestry.
            Scene::EntityWorld definition;
            for(const auto id:order){
                auto copy=*candidate_->Find(id);
                if(!ValidScriptText(copy.name,Scene::SceneDocument::MaxBytes))return Result::Invalid;
                if(id==source)copy.parent=0;
                copy.prefabLink=Scene::SceneValue{};
                definition.GetEntities().push_back(std::move(copy));
            }
            const auto document=definition.Capture();
            if(document.dump().size()>Scene::SceneDocument::MaxBytes)return Result::Limit;
            Scene::EntityWorld checked;std::string error;
            // Restore converts some native exceptions into validation failure.
            // Fail closed either way: earlier commands cannot be published from
            // a transaction whose template validation did not complete.
            if(!checked.Restore(document,error)){poisoned_=true;return Result::Invalid;}
            auto work=*candidate_;std::unordered_map<Scene::EntityId,Scene::EntityId> remapped;
            Scene::EntityId root=0;
            for(const auto id:order){
                auto copy=*definition.Find(id);
                const auto destinationParent=id==source ? parent : remapped.at(copy.parent);
                const auto newId=work.CreateEntity(copy.name,destinationParent);
                if(!newId)return Result::Invalid;
                copy.id=newId;copy.parent=destinationParent;
                copy.script.started=false;copy.rigidBody.grounded=false;
                // Owning field assignments may allocate, but only private work
                // storage is touched. No record moves occur at publication.
                *work.Find(newId)=std::move(copy);
                remapped.emplace(id,newId);if(id==source)root=newId;
            }
            candidate_->Swap(work);++calls_;created_+=order.size();created=root;
            return Result::Success;
        } catch(...) {poisoned_=true;return Result::AllocationFailure;}
    }
    // Single deletion preserves existing DestroyEntity semantics: direct
    // children become roots, with LOCAL TRS unchanged (not keep-world).
    // Subtree deletion is explicit. Aggregate removed Entities are capped at 64.
    Result Destroy(Scene::EntityId id,bool subtree=false) noexcept {
        if(!id || id>Scene::EntityWorld::MaximumEntityId)return Result::Invalid;
        if(!Editable())return Result::Unavailable;
        if(!candidate_->Find(id))return Result::Missing;
        if(calls_==MaxOperations || removed_==MaxOperations)return Result::Limit;
        try {
            std::unordered_set<Scene::EntityId> ids{id};
            if(subtree){
                // Graph is validated at Begin and only these operations edit it.
                for(const auto& record:candidate_->GetEntities()){
                    for(auto parent=record.parent;parent;parent=candidate_->Find(parent)->parent){
                        if(parent==id){ids.insert(record.id);break;}
                    }
                    if(ids.size()>MaxOperations-removed_)return Result::Limit;
                }
            }
            auto work=*candidate_;
            // Rebuild rather than erasing live owning records. Any allocation
            // failure poisons the private transaction; the live World is intact.
            auto& records=work.GetEntities();
            std::vector<Scene::EntityRecord> retained;retained.reserve(records.size()-ids.size());
            for(const auto& record:records)if(!ids.contains(record.id)){
                retained.push_back(record);
                if(ids.contains(retained.back().parent))retained.back().parent=0;
            }
            records.swap(retained);candidate_->Swap(work);
            ++calls_;removed_+=ids.size();return Result::Success;
        } catch(...) {poisoned_=true;return Result::AllocationFailure;}
    }
    bool Prepare() noexcept {return PrepareCandidate(false);}
private:
    bool PrepareCandidate(bool allowEmpty) noexcept {
        prepared_=false;
        if(!Editable() || (!calls_ && !allowEmpty))return false;
        // Candidate is const to consumers. Still validate before admission so
        // future operations cannot accidentally publish a malformed hierarchy.
        try {prepared_=Valid(*candidate_);return prepared_;}
        catch(...) {poisoned_=true;return false;}
    }
public:
    bool PublishPrepared() noexcept {
        if(!prepared_ || !Context() || poisoned_ || published_ || !candidate_)return false;
        world_->Swap(*candidate_);prepared_=false;published_=true;return true;
    }
private:
    bool Context() const noexcept {
        return view_ && world_ && token_ && view_->world_==world_ &&
            view_->writeWorld_==world_ && view_->scopeToken_==token_;
    }
    bool Editable() const noexcept {return Candidate() && !prepared_;}
    static bool Valid(const Scene::EntityWorld& world) {
        const auto& records=world.GetEntities();if(records.size()>MaxEntities)return false;
        std::unordered_map<Scene::EntityId,Scene::EntityId> parents;
        for(const auto& record:records)if(!record.id || record.id>Scene::EntityWorld::MaximumEntityId ||
            !parents.emplace(record.id,record.parent).second)return false;
        // Three-color iterative traversal: linear graph work, no recursion.
        std::unordered_map<Scene::EntityId,unsigned> colors;
        for(const auto& record:records){
            auto id=record.id;
            while(id){
                if(!parents.contains(id))return false;
                auto& color=colors[id];if(color==1)return false;if(color==2)break;
                color=1;id=parents.at(id);
            }
            id=record.id;
            while(id && colors[id]==1){colors[id]=2;id=parents.at(id);}
        }
        return true;
    }
    const ScriptSceneView* view_=nullptr;Scene::EntityWorld* world_=nullptr;
    uint64_t token_=0;size_t calls_=0,removed_=0,created_=0;
    bool prepared_=false,poisoned_=false,published_=false;
    std::optional<Scene::EntityWorld> candidate_;
};
}
