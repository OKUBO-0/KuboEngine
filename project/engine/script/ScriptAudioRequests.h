#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <memory>

namespace Engine::Scripting {
struct ScriptAudioOwner { uint64_t entity=0; bool alive=true; };
struct ScriptAudioRequest {
    enum class Operation { Play, StopAll, SetVolume };
    Operation operation=Operation::Play;
    uint64_t entity=0;
    std::shared_ptr<ScriptAudioOwner> owner;
    std::string path;
    bool loop=false;
    float volume=1;
};
// Host-agnostic, main-thread command staging. A host explicitly opts in and
// drains commands only after successful Lua hooks. This is not a sound device.
class ScriptAudioRequests final {
public:
    static constexpr size_t kMaxPerHook=32, kMaxPending=256;
    bool Enabled() const {return enabled_;}
    void SetEnabled(bool enabled) {enabled_=enabled;if(!enabled) pending_.clear();}
    bool Publish(const std::vector<ScriptAudioRequest>& batch) {
        if(batch.empty()) return true;
        if(!enabled_ || batch.size()>kMaxPerHook || batch.size()>kMaxPending-pending_.size()) return false;
        // Copy-and-swap gives all-or-nothing admission even on allocation failure.
        auto candidate=pending_;candidate.insert(candidate.end(),batch.begin(),batch.end());
        pending_.swap(candidate);return true;
    }
    std::vector<ScriptAudioRequest> Take() {std::vector<ScriptAudioRequest> result;result.swap(pending_);return result;}
    void Clear() {pending_.clear();}
private:
    bool enabled_=false;
    std::vector<ScriptAudioRequest> pending_;
};
}
