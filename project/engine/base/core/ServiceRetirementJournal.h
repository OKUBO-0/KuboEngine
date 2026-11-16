#pragma once
#include <array>
#include <cstddef>
#include <stdexcept>

namespace Engine::Base {

// Host-thread-only, fixed-capacity ownership bookkeeping. A callback exception
// keeps its entry and every not-yet-retired entry available for explicit retry.
template <std::size_t Capacity>
class ServiceRetirementJournal {
public:
    using Finalizer = void (*)(void*);
    void Record(std::size_t slot, void* instance, Finalizer finalizer) {
        if (sealed_ || retiring_) throw std::logic_error("Service acquisition is closed");
        if (slot >= Capacity || !instance || !finalizer)
            throw std::invalid_argument("Invalid service acquisition record");
        if (entries_[slot].instance) throw std::logic_error("Service already acquired");
        entries_[slot] = {instance, finalizer};
    }
    void Seal() noexcept { sealed_ = true; }
    bool IsSealed() const noexcept { return sealed_; }
    bool Contains(std::size_t slot) const noexcept {
        return slot < Capacity && entries_[slot].instance != nullptr;
    }
    std::size_t PendingCount() const noexcept {
        std::size_t count = 0;
        for (const auto& entry : entries_) if (entry.instance) ++count;
        return count;
    }
    void Retire(std::size_t slot) {
        if (slot >= Capacity) throw std::out_of_range("Invalid service retirement slot");
        Seal();
        if (retiring_ || !entries_[slot].instance) return;
        retiring_ = true;
        struct Guard { bool& value; ~Guard() { value = false; } } guard{retiring_};
        const auto entry = entries_[slot];
        entry.finalizer(entry.instance);
        entries_[slot] = {};
    }
private:
    struct Entry { void* instance = nullptr; Finalizer finalizer = nullptr; };
    std::array<Entry, Capacity> entries_{};
    bool sealed_ = false;
    bool retiring_ = false;
};

}
