#pragma once
#include "Audio.h"
#include "../base/AssetPath.h"
#include <atomic>
#include <memory>
#include <limits>
#include <cmath>

namespace Engine::AudioSystem {
// Main-thread host ownership for Script/Entity audio. Buffers never move while
// XAudio2 uses them. Destroy banks before Audio::Finalize; no global StopAudio.
class OwnedSoundBank final {
public:
    using Handle=uint64_t;
    static constexpr size_t kMaxSounds=64, kMaxBufferBytes=64*1024*1024;
    static constexpr size_t kMaxVoices=64, kMaxVoicesPerSound=8;
    OwnedSoundBank()=default;
    ~OwnedSoundBank() { Clear(); }
    OwnedSoundBank(const OwnedSoundBank&)=delete;
    OwnedSoundBank& operator=(const OwnedSoundBank&)=delete;
    OwnedSoundBank(OwnedSoundBank&&)=delete;
    OwnedSoundBank& operator=(OwnedSoundBank&&)=delete;

    bool Load(const std::string& relativeUtf8,Handle& output,std::string& error) {
        try {
            if(sounds_.size()>=kMaxSounds) throw std::runtime_error("Audio sound count budget exceeded");
            if(relativeUtf8.empty() || relativeUtf8.size()>4096 || relativeUtf8.find('\0')!=std::string::npos || relativeUtf8.find(':')!=std::string::npos)
                throw std::invalid_argument("Audio asset must be a bounded project-relative path");
            const auto path=Engine::Base::PathFromUtf8(relativeUtf8);
            if(path.has_root_name() || path.has_root_directory()) throw std::invalid_argument("Absolute audio asset is not allowed");
            for(const auto& part:path) if(part=="..") throw std::invalid_argument("Audio asset escapes project");
            auto candidate=std::make_unique<SoundData>();
            if(!Audio::GetInstance()->TrySoundLoadWave(path,*candidate,error)) return false;
            if(candidate->buffer.size()>kMaxBufferBytes-bytes_) throw std::runtime_error("Audio buffer budget exceeded");
            const auto id=NextHandle();
            const auto size=candidate->buffer.size();
            sounds_.emplace(id,std::move(candidate));bytes_+=size;
            output=id;error.clear();return true;
        } catch(const std::exception& exception) {error=exception.what();return false;}
    }
    bool Play(Handle id,bool loop,float volume,std::string& error) {
        try {
            auto& sound=Find(id);
            if(!std::isfinite(volume) || volume<0 || volume>1) throw std::invalid_argument("Audio volume must be between zero and one");
            auto* audio=Audio::GetInstance();
            if(audio->GetQueuedVoiceCount(&sound)>=kMaxVoicesPerSound || VoiceCount()>=kMaxVoices)
                throw std::runtime_error("Audio voice budget exceeded");
            audio->SoundPlayWave(sound,loop,volume);error.clear();return true;
        } catch(const std::exception& exception) {error=exception.what();return false;}
    }
    bool Stop(Handle id,std::string& error) {
        try {Audio::GetInstance()->StopSpecificAudio(&Find(id));error.clear();return true;}
        catch(const std::exception& exception) {error=exception.what();return false;}
    }
    bool SetVolume(Handle id,float volume,std::string& error) {
        try {
            auto& sound=Find(id);
            if(!std::isfinite(volume) || volume<0 || volume>1) throw std::invalid_argument("Audio volume must be between zero and one");
            Audio::GetInstance()->SetVolume(&sound,volume);error.clear();return true;
        } catch(const std::exception& exception) {error=exception.what();return false;}
    }
    bool QueuedVoicesHaveVolume(Handle id,float volume) const {
        const auto found=sounds_.find(id);return found!=sounds_.end() && Audio::GetInstance()->QueuedVoicesHaveVolume(found->second.get(),volume);
    }
    bool Release(Handle id,std::string& error) {
        const auto found=sounds_.find(id);
        if(found==sounds_.end()) {error="Unknown or released audio handle";return false;}
        const auto size=found->second->buffer.size();
        Audio::GetInstance()->SoundUnload(found->second.get());sounds_.erase(found);bytes_-=size;
        error.clear();return true;
    }
    bool IsPlaying(Handle id) const {
        const auto found=sounds_.find(id);
        return found!=sounds_.end() && Audio::GetInstance()->IsSoundPlaying(found->second.get());
    }
    void Clear() {
        for(auto& [id,sound]:sounds_) {(void)id;Audio::GetInstance()->SoundUnload(sound.get());}
        sounds_.clear();bytes_=0;
    }
    void StopAll() { for(auto& [id,sound]:sounds_) {(void)id;Audio::GetInstance()->StopSpecificAudio(sound.get());} }
    void PauseAll() { for(auto& [id,sound]:sounds_) {(void)id;Audio::GetInstance()->PauseSpecificAudio(sound.get());} }
    void ResumeAll() { for(auto& [id,sound]:sounds_) {(void)id;Audio::GetInstance()->ResumeSpecificAudio(sound.get());} }
    size_t SoundCount() const { return sounds_.size(); }
    uint64_t PlayedSamples() const {uint64_t total=0;for(const auto& [id,sound]:sounds_) {(void)id;total+=Audio::GetInstance()->GetPlayedSampleCount(sound.get());}return total;}
    size_t BufferBytes() const { return bytes_; }
    size_t VoiceCount() const {
        size_t count=0;
        for(const auto& [id,sound]:sounds_) {(void)id;count+=Audio::GetInstance()->GetQueuedVoiceCount(sound.get());}
        return count;
    }
private:
    SoundData& Find(Handle id) {
        const auto found=sounds_.find(id);
        if(found==sounds_.end()) throw std::invalid_argument("Unknown or released audio handle");
        return *found->second;
    }
    static Handle NextHandle() {
        // Never reuse a released handle, including between independent owners.
        static std::atomic<Handle> next{1};
        auto value=next.load();
        for(;;) {
            if(value==(std::numeric_limits<Handle>::max)()) throw std::runtime_error("Audio handle space exhausted");
            if(next.compare_exchange_weak(value,value+1)) return value;
        }
    }
    std::unordered_map<Handle,std::unique_ptr<SoundData>> sounds_;
    size_t bytes_=0;
};
}
