#pragma once
#include "OwnedSoundBank.h"
#include "../script/ScriptRuntime.h"
#include <map>
#include <functional>

namespace Engine::AudioSystem {
// A VM epoch, not Entity ID or path, owns every bank. Retired/faulted VM tokens
// cannot replay deferred commands or borrow a reloaded script's sound buffers.
class ScriptAudioHost final {
public:
    static constexpr size_t kMaxOwners=64, kMaxBytes=64*1024*1024, kMaxVoices=64;
    using Reporter=std::function<void(uint64_t,const std::string&)>;
    void Synchronize(Scripting::ScriptRuntime& runtime,bool enabled,bool paused=false,const Reporter& report={}) {
        runtime.SetAudioRequestsEnabled(enabled);
        if(!enabled) {Clear();return;}
        std::erase_if(owners_,[](const auto& item){return !item.first->alive;});
        for(const auto& request:runtime.TakeAudioRequests()) {
            if(!request.owner || !request.owner->alive || request.entity!=request.owner->entity) continue;
            try {
                auto found=owners_.find(request.owner);
                if(found==owners_.end()) {
                    if(request.operation==Scripting::ScriptAudioRequest::Operation::SetVolume) throw std::runtime_error("Audio sound has not been loaded by this VM");
                    if(owners_.size()>=kMaxOwners) throw std::runtime_error("Script audio owner budget exceeded");
                    found=owners_.try_emplace(request.owner).first;
                }
                auto& entry=found->second;
                if(request.operation==Scripting::ScriptAudioRequest::Operation::StopAll) entry.bank.StopAll();
                else if(request.operation==Scripting::ScriptAudioRequest::Operation::SetVolume) {
                    const auto sound=entry.sounds.find(request.path);std::string error;
                    if(sound==entry.sounds.end()) throw std::runtime_error("Audio sound has not been loaded by this VM");
                    if(!entry.bank.SetVolume(sound->second,request.volume,error)) throw std::runtime_error(error);
                }
                else {
                    std::string error;
                    auto sound=entry.sounds.find(request.path);
                    if(sound==entry.sounds.end()) {
                        OwnedSoundBank::Handle handle=0;
                        if(!entry.bank.Load(request.path,handle,error)) throw std::runtime_error(error);
                        if(BufferBytes()>kMaxBytes) {entry.bank.Release(handle,error);throw std::runtime_error("Script audio total buffer budget exceeded");}
                        try {sound=entry.sounds.emplace(request.path,handle).first;}
                        catch(...) {entry.bank.Release(handle,error);throw;}
                    }
                    if(VoiceCount()>=kMaxVoices) throw std::runtime_error("Script audio total voice budget exceeded");
                    if(!entry.bank.Play(sound->second,request.loop,request.volume,error)) throw std::runtime_error(error);
                }
                // Newly submitted voices also follow an already-paused host.
                if(paused) entry.bank.PauseAll();
            } catch(const std::exception& exception) {RecordError(request.entity,exception.what(),report);}
        }
        if(paused!=paused_) {
            for(auto& [owner,entry]:owners_) {
                try {if(paused) entry.bank.PauseAll();else entry.bank.ResumeAll();}
                catch(const std::exception& exception) {RecordError(owner->entity,exception.what(),report);}
            }
        }
        paused_=paused;
    }
    void Clear() {owners_.clear();errors_.clear();paused_=false;}
    size_t OwnerCount() const {return owners_.size();}
    bool IsPaused() const {return paused_;}
    bool QueuedVoicesHaveVolume(uint64_t entity,const std::string& path,float volume) const {
        for(const auto& [owner,entry]:owners_) if(owner->alive && owner->entity==entity) {
            const auto sound=entry.sounds.find(path);
            if(sound!=entry.sounds.end()) return entry.bank.QueuedVoicesHaveVolume(sound->second,volume);
        }
        return false;
    }
    uint64_t PlayedSamples() const {uint64_t total=0;for(const auto& [owner,entry]:owners_) {(void)owner;total+=entry.bank.PlayedSamples();}return total;}
    size_t BufferBytes() const {size_t total=0;for(const auto& [owner,entry]:owners_) {(void)owner;total+=entry.bank.BufferBytes();}return total;}
    size_t VoiceCount() const {size_t total=0;for(const auto& [owner,entry]:owners_) {(void)owner;total+=entry.bank.VoiceCount();}return total;}
    std::string GetLastError(uint64_t entity) const {const auto found=errors_.find(entity);return found==errors_.end()?std::string{}:found->second;}
private:
    struct Entry {OwnedSoundBank bank;std::unordered_map<std::string,OwnedSoundBank::Handle> sounds;};
    void RecordError(uint64_t entity,const std::string& text,const Reporter& report) {
        if(!errors_.contains(entity) && errors_.size()>=256) return;
        const auto bounded=text.substr(0,2048);
        auto& old=errors_[entity];if(old==bounded) return;old=bounded;
        if(report) report(entity,bounded);
    }
    std::map<std::shared_ptr<Scripting::ScriptAudioOwner>,Entry,std::owner_less<std::shared_ptr<Scripting::ScriptAudioOwner>>> owners_;
    std::unordered_map<uint64_t,std::string> errors_;
    bool paused_=false;
};
}
