#pragma once
#include "InputFrame.h"
#include <deque>
#include <limits>
#include <utility>

namespace Engine::InputSystem {
struct FixedInputUsage { size_t pendingSamples=0, droppedSamples=0; };

// Runtime-thread owned FIFO of observed host samples with transient input.
// One sample per simulation tick; all script peers see the same tick snapshot.
// This preserves observed press/release order, not sub-device-poll timestamps.
class FixedInputBuffer final {
public:
    static constexpr size_t MaximumPendingSamples=64;
    void Push(const InputFrame& frame) {
        if(!frame.available){latest_.Reset();pending_.clear();return;}
        // Prepare copies before changing the buffer (allocation can fail).
        auto latest=frame;ClearTransients(latest);
        if(HasTransients(frame)) {
            pending_.push_back(frame);
            if(pending_.size()>MaximumPendingSamples) {
                pending_.pop_front();
                if(dropped_!=(std::numeric_limits<size_t>::max)())++dropped_;
            }
        }
        latest_.Swap(latest);
    }
    InputFrame Consume() {
        if(pending_.empty())return latest_;
        auto sample=std::move(pending_.front());pending_.pop_front();return sample;
    }
    void Reset() noexcept {latest_.Reset();pending_.clear();dropped_=0;}
    FixedInputUsage Usage() const noexcept {return {pending_.size(),dropped_};}
private:
    static bool HasTransients(const InputFrame& frame) noexcept {
        if(frame.pad.connectedThisFrame || frame.pad.disconnectedThisFrame)return true;
        for(const auto& button:frame.pad.buttons)if(button.pressed || button.released)return true;
        for(const auto& key:frame.keys)if(key.pressed || key.released)return true;
        for(const auto& button:frame.mouse)if(button.pressed || button.released)return true;
        for(const auto& action:frame.actions)if(action.second.button.pressed || action.second.button.released)return true;
        return frame.deltaX!=0 || frame.deltaY!=0 || frame.wheel!=0;
    }
    static void ClearTransients(InputFrame& frame) noexcept {
        frame.pad.connectedThisFrame=frame.pad.disconnectedThisFrame=false;
        for(auto& button:frame.pad.buttons)button.pressed=button.released=false;
        for(auto& key:frame.keys)key.pressed=key.released=false;
        for(auto& button:frame.mouse)button.pressed=button.released=false;
        for(auto& action:frame.actions)action.second.button.pressed=action.second.button.released=false;
        frame.deltaX=frame.deltaY=frame.wheel=0;
    }
    InputFrame latest_;
    std::deque<InputFrame> pending_;
    size_t dropped_=0;
};
}
