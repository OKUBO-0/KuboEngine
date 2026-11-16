#pragma once
#include <array>
#include <cstdint>
namespace Engine::Base {
class ImGuiDescriptorSlots {
public:
    static constexpr uint32_t kCapacity = 64; // Bounded Browser previews plus backend fonts.
    static constexpr uint32_t kFirstDynamic = 2; // Scene/Editor slots are reserved.
    bool Allocate(uint32_t& output) noexcept {
        if (!freeCount_ && next_ == kCapacity) return false;
        const auto index = freeCount_ ? free_[--freeCount_] : next_++;
        allocated_[index] = true; output = index; return true;
    }
    bool Release(uint32_t index) noexcept {
        if (index < kFirstDynamic || index >= kCapacity || !allocated_[index]) return false;
        allocated_[index] = false; free_[freeCount_++] = index; return true;
    }
    void Reset() noexcept { allocated_.fill(false); freeCount_ = 0; next_ = kFirstDynamic; }
private:
    std::array<uint32_t, kCapacity> free_{};
    std::array<bool, kCapacity> allocated_{};
    uint32_t freeCount_ = 0, next_ = kFirstDynamic;
};
}
