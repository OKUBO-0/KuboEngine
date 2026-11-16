#pragma once
#include "OwnedSoundBank.h"
#include "../assets/AudioReference.h"
#include <map>
#include <functional>

namespace Engine::AudioSystem {
class EntityAudioHost final {
public:
    static constexpr size_t kMaxSources=64,kMaxBytes=64*1024*1024;
    using Reporter=std::function<void(uint64_t,const std::string&)>;
    void Synchronize(const Scene::EntityWorld& world,const Assets::AssetRegistry& registry,
        const std::filesystem::path& projectRoot,bool enabled,bool paused=false,const Reporter& report={}) {
        if(!enabled) {Clear();return;}
        std::erase_if(sources_,[&](const auto& item) {
            const auto* entity=world.Find(item.first);
            return !entity || !entity->hasAudio || !entity->audio.enabled || !entity->audio.playOnAwake || !Active(world,*entity);
        });
        for(const auto& entity:world.GetEntities()) {
            if(!entity.hasAudio || !entity.audio.enabled || !entity.audio.playOnAwake || !Active(world,entity)) continue;
            try {
                std::string error;
                const auto path=Assets::ResolveAudioReference(entity.audio,registry,projectRoot,error);
                if(path.empty()) {sources_.erase(entity.id);RecordError(entity.id,error,report);continue;}
                const auto source=std::filesystem::absolute(path).lexically_normal();
                const auto key=entity.audio.assetGuid.empty()?entity.audio.assetPath:"guid:"+entity.audio.assetGuid;
                const auto size=std::filesystem::file_size(source);const auto modified=std::filesystem::last_write_time(source);
                auto found=sources_.find(entity.id);
                if(found!=sources_.end() && (found->second.source!=source || found->second.key!=key || found->second.loop!=entity.audio.loop || found->second.size!=size || found->second.modified!=modified)) {
                    sources_.erase(found);found=sources_.end();
                }
                if(found==sources_.end()) {
                    if(sources_.size()>=kMaxSources) throw std::runtime_error("Entity Audio source budget exceeded");
                    found=sources_.try_emplace(entity.id).first;
                    auto& entry=found->second;entry.source=source;entry.key=key;entry.loop=entity.audio.loop;entry.size=size;entry.modified=modified;
                    // Retain a failed attempt until settings/source metadata change.
                    auto relative=source.lexically_relative(std::filesystem::absolute(projectRoot).lexically_normal());
                    if(!entry.bank.Load(Engine::Base::PathToUtf8(relative),entry.sound,error)) {entry.failed=true;RecordError(entity.id,error,report);continue;}
                    if(BufferBytes()>kMaxBytes) {entry.bank.Clear();entry.failed=true;RecordError(entity.id,"Entity Audio total buffer budget exceeded",report);continue;}
                    if(!entry.bank.Play(entry.sound,entity.audio.loop,entity.audio.volume,error)) {entry.bank.Clear();entry.failed=true;RecordError(entity.id,error,report);continue;}
                    entry.volume=entity.audio.volume;
                }
                auto& entry=found->second;
                if(entry.failed) continue;
                if(entry.volume!=entity.audio.volume) {
                    if(!entry.bank.SetVolume(entry.sound,entity.audio.volume,error)) {RecordError(entity.id,error,report);continue;}
                    entry.volume=entity.audio.volume;
                }
                if(paused) entry.bank.PauseAll();else if(paused_) entry.bank.ResumeAll();
            } catch(const std::exception& exception) {sources_.erase(entity.id);RecordError(entity.id,exception.what(),report);}
        }
        paused_=paused;
    }
    void Clear() {sources_.clear();errors_.clear();paused_=false;}
    size_t SourceCount() const {return sources_.size();}
    size_t BufferBytes() const {size_t total=0;for(const auto& [id,entry]:sources_) {(void)id;total+=entry.bank.BufferBytes();}return total;}
    size_t VoiceCount() const {size_t total=0;for(const auto& [id,entry]:sources_) {(void)id;total+=entry.bank.VoiceCount();}return total;}
    uint64_t PlayedSamples() const {uint64_t total=0;for(const auto& [id,entry]:sources_) {(void)id;total+=entry.bank.PlayedSamples();}return total;}
    bool QueuedVoicesHaveVolume(uint64_t id,float volume) const {const auto found=sources_.find(id);return found!=sources_.end() && found->second.bank.QueuedVoicesHaveVolume(found->second.sound,volume);}
    std::string GetLastError(uint64_t id) const {const auto found=errors_.find(id);return found==errors_.end()?std::string{}:found->second;}
private:
    static bool Active(const Scene::EntityWorld& world,const Scene::EntityRecord& entity) {
        const auto* current=&entity;size_t visited=0;
        while(current) {if(!current->active || ++visited>world.GetEntities().size()) return false;if(!current->parent) return true;current=world.Find(current->parent);}
        return false;
    }
    void RecordError(uint64_t id,const std::string& error,const Reporter& report) {
        if(!errors_.contains(id) && errors_.size()>=256) return;
        const auto text=error.substr(0,2048);auto& old=errors_[id];if(old==text) return;old=text;if(report) report(id,text);
    }
    struct Entry {OwnedSoundBank bank;OwnedSoundBank::Handle sound=0;std::filesystem::path source;std::string key;uintmax_t size=0;std::filesystem::file_time_type modified;bool loop=false,failed=false;float volume=1;};
    std::map<uint64_t,Entry> sources_;
    std::unordered_map<uint64_t,std::string> errors_;
    bool paused_=false;
};
}
