#pragma once
#include <cstddef>
#include <stdexcept>

namespace Engine::Scripting {
struct LuaResourceLimits {
    size_t memoryBytes=64*1024*1024;
    size_t states=256;
    size_t frameInstructions=2*1000*1000;
};
struct LuaResourceUsage { size_t memoryBytes=0,states=0,instructions=0,deferredEntities=0; bool instructionExhausted=false; };

// Authoring/runtime-thread owned. Counts Lua allocator bytes (factory seed and
// retained callback VMs) plus retained factory source payload bytes, once per
// shared source lease. Not a process-wide limit or total native heap accounting.
class LuaResourceBudget final {
public:
    explicit LuaResourceBudget(LuaResourceLimits limits={}) : limits_(limits) {
        if(!limits.memoryBytes || !limits.states || !limits.frameInstructions)throw std::invalid_argument("Lua resource limits must be positive");
    }
    LuaResourceBudget(const LuaResourceBudget&)=delete;
    LuaResourceBudget& operator=(const LuaResourceBudget&)=delete;
    LuaResourceLimits Limits() const noexcept { return limits_; }
    LuaResourceUsage Usage() const noexcept { return usage_; }
    bool FrameActive() const noexcept {return frameActive_;}
    bool HasFrameAllowance() const noexcept {return !frameActive_ || usage_.instructions<limits_.frameInstructions;}
    bool ChargeInstruction() noexcept {
        if(!frameActive_)return true;
        if(usage_.instructions>=limits_.frameInstructions){usage_.instructionExhausted=true;return false;}
        ++usage_.instructions;usage_.instructionExhausted=usage_.instructions==limits_.frameInstructions;return true;
    }
    void DeferEntity() noexcept {++usage_.deferredEntities;}
    class FrameScope final {
    public:
        explicit FrameScope(LuaResourceBudget& budget) noexcept : budget_(budget),owner_(!budget.frameActive_) {
            if(owner_){budget.frameActive_=true;budget.usage_.instructions=0;budget.usage_.deferredEntities=0;budget.usage_.instructionExhausted=false;}
        }
        ~FrameScope(){if(owner_)budget_.frameActive_=false;}
        FrameScope(const FrameScope&)=delete;
    private:
        LuaResourceBudget& budget_;bool owner_;
    };
    bool AcquireState() noexcept {
        if(usage_.states>=limits_.states)return false;
        ++usage_.states;return true;
    }
    void ReleaseState() noexcept { --usage_.states; }
    // Reserve before realloc. A failed realloc restores its old reservation.
    bool Resize(size_t oldBytes,size_t newBytes) noexcept {
        if(oldBytes>usage_.memoryBytes || newBytes>limits_.memoryBytes)return false;
        const auto retained=usage_.memoryBytes-oldBytes;
        if(retained>limits_.memoryBytes-newBytes)return false;
        usage_.memoryBytes=retained+newBytes;return true;
    }
private:
    LuaResourceLimits limits_;
    LuaResourceUsage usage_;
    bool frameActive_=false;
};
}
