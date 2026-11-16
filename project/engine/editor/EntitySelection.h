#pragma once
#include "../scene/entity/EntityWorld.h"
#include <algorithm>
#include <span>
#include <unordered_set>
#include <utility>

namespace Engine::Editor {
// Transient authoring selection. Hierarchy can supply its displayed row order.
class EntitySelection final {
    using Id=Scene::EntityId;
    std::vector<Id> ids_;
    std::unordered_set<Id> members_;
    Id primary_=0,anchor_=0;
    void Add(Id id) {ids_.erase(std::remove(ids_.begin(),ids_.end(),id),ids_.end());ids_.push_back(id);members_.insert(id);primary_=id;}
public:
    struct Snapshot {
        std::vector<Id> ids;
        Id primary=0,anchor=0;
        bool operator==(const Snapshot&) const = default;
        size_t Bytes() const {return ids.size()*sizeof(Id)+2*sizeof(Id);}
    };
    Snapshot Capture() const {return {ids_,primary_,anchor_};}
    bool Restore(const Scene::EntityWorld& world,const Snapshot& snapshot,std::string& error) {
        if(snapshot.ids.size()>10000 || (snapshot.ids.empty()?snapshot.primary!=0:
            std::find(snapshot.ids.begin(),snapshot.ids.end(),snapshot.primary)==snapshot.ids.end())) {
            error="Invalid selection snapshot size/primary";return false;
        }
        EntitySelection candidate;std::unordered_set<Id> live;
        for(const auto& entity:world.GetEntities())live.insert(entity.id);
        if(snapshot.anchor && !live.contains(snapshot.anchor)){error="Selection anchor is missing";return false;}
        for(const auto id:snapshot.ids) {
            if(!id || !live.contains(id) || !candidate.members_.insert(id).second){error="Selection contains missing or duplicate Entity";return false;}
            candidate.ids_.push_back(id);
        }
        candidate.primary_=snapshot.primary;candidate.anchor_=snapshot.anchor;
        *this=std::move(candidate);error.clear();return true;
    }
    const std::vector<Id>& Ids() const {return ids_;}
    Id Primary() const {return primary_;}
    bool Contains(Id id) const {return members_.contains(id);}
    bool Assign(const Scene::EntityWorld& world,const std::vector<Id>& ids,Id primary,std::string& error) {
        if(ids.empty() || ids.size()>10000 || std::find(ids.begin(),ids.end(),primary)==ids.end()) {error="Invalid selection size/primary";return false;}
        EntitySelection candidate;std::unordered_set<Id> live;for(const auto& entity:world.GetEntities())live.insert(entity.id);
        for(const auto id:ids) {
            if(!id || !live.contains(id) || !candidate.members_.insert(id).second) {error="Selection contains missing or duplicate Entity";return false;}
            candidate.ids_.push_back(id);
        }
        candidate.Add(primary);candidate.anchor_=primary;*this=std::move(candidate);error.clear();return true;
    }
    void Replace(Id id) {ids_.clear();members_.clear();primary_=anchor_=id;if(id){ids_.push_back(id);members_.insert(id);}}
    void Synchronize(const Scene::EntityWorld& world,Id& externalPrimary) {
        std::unordered_set<Id> live;live.reserve(world.GetEntities().size());for(const auto& entity:world.GetEntities())live.insert(entity.id);
        if(externalPrimary!=primary_) Replace(live.contains(externalPrimary)?externalPrimary:0);
        ids_.erase(std::remove_if(ids_.begin(),ids_.end(),[&](Id id){if(live.contains(id))return false;members_.erase(id);return true;}),ids_.end());
        if(!Contains(primary_)) primary_=ids_.empty()?0:ids_.back();
        if(!live.contains(anchor_)) anchor_=primary_;
        externalPrimary=primary_;
    }
    bool Click(const Scene::EntityWorld& world,Id id,bool additive,bool range,std::string& error,std::span<const Id> displayed={}) {
        if(!id) {if(!additive && !range)Replace(0);error.clear();return true;}
        if(!world.Find(id)) {error="Selection Entity does not exist";return false;}
        if(range && world.Find(anchor_)) {
            std::vector<Id> storageOrder;
            if(displayed.empty()) {
                storageOrder.reserve(world.GetEntities().size());
                for(const auto& entity:world.GetEntities())storageOrder.push_back(entity.id);
                displayed=storageOrder;
            }
            const auto start=std::find(displayed.begin(),displayed.end(),anchor_);
            const auto end=std::find(displayed.begin(),displayed.end(),id);
            if(start==displayed.end() || end==displayed.end()) {
                // An anchor hidden by search or a collapsed branch starts a new range.
                return Click(world,id,additive,false,error);
            }
            if(!additive){ids_.clear();members_.clear();}
            for(auto row=(std::min)(start,end);row<=(std::max)(start,end);++row)
                if(members_.insert(*row).second)ids_.push_back(*row);
            Add(id);
        } else if(additive) {
            anchor_=id;
            if(Contains(id)) {ids_.erase(std::remove(ids_.begin(),ids_.end(),id),ids_.end());members_.erase(id);primary_=ids_.empty()?0:ids_.back();}
            else Add(id);
        } else Replace(id);
        error.clear();return true;
    }
};
}
