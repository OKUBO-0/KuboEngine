#pragma once
#include <cstddef>
namespace Engine::Scripting {
// Deterministic admission work, not a wall-clock/OS syscall/RSS sandbox.
// Charge before allocating/reading bytes or requesting a filesystem check.
// Failed work remains charged for the lifetime of this hook transaction.
class ScriptPrefabWorkBudget final {
public:
    static constexpr size_t MaxBytes=8*1024*1024,MaxReferences=512,MaxFileChecks=4096;
    struct Usage {size_t bytes=0,references=0,fileChecks=0;bool exceeded=false;};
    const Usage& GetUsage() const noexcept {return usage_;}
    bool Bytes(size_t count) noexcept {return Charge(usage_.bytes,count,MaxBytes);}
    bool Reference() noexcept {return Charge(usage_.references,1,MaxReferences);}
    bool FileChecks(size_t count=1) noexcept {return Charge(usage_.fileChecks,count,MaxFileChecks);}
private:
    bool Charge(size_t& used,size_t count,size_t maximum) noexcept {
        if(usage_.exceeded || count>maximum-used){usage_.exceeded=true;return false;}
        used+=count;return true;
    }
    Usage usage_{};
};
}
