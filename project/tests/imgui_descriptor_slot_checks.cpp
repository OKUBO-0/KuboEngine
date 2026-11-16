#include "engine/base/render/ImGuiDescriptorSlots.h"
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
namespace { bool failNext = false; unsigned consumed = 0; }
void* operator new(size_t size) {
    if (failNext) { failNext = false; ++consumed; throw std::bad_alloc(); }
    if (auto* memory = std::malloc(size ? size : 1)) return memory;
    throw std::bad_alloc();
}
void* operator new[](size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, size_t) noexcept { std::free(memory); }
int main() {
    try {
        Engine::Base::ImGuiDescriptorSlots slots;
        bool valid = true;
        failNext = true;
        for (uint32_t index = 2; index < Engine::Base::ImGuiDescriptorSlots::kCapacity; ++index) {
            uint32_t allocated = UINT32_MAX;
            valid = slots.Allocate(allocated) && allocated == index && valid;
        }
        uint32_t unchanged = 123;
        valid = !slots.Allocate(unchanged) && unchanged == 123 && valid;
        valid = !slots.Release(0) && !slots.Release(1) && !slots.Release(Engine::Base::ImGuiDescriptorSlots::kCapacity) && !slots.Release(UINT32_MAX) && valid;
        for (uint32_t index = 2; index < Engine::Base::ImGuiDescriptorSlots::kCapacity; ++index)
            valid = slots.Release(index) && !slots.Release(index) && valid;
        for (uint32_t expected = Engine::Base::ImGuiDescriptorSlots::kCapacity-1; expected >= 2; --expected) {
            uint32_t allocated = 0;
            valid = slots.Allocate(allocated) && allocated == expected && valid;
        }
        slots.Reset();
        valid = !slots.Release(2) && valid;
        for (unsigned cycle = 0; cycle < 1024; ++cycle) {
            uint32_t allocated = 0;
            valid = slots.Allocate(allocated) && allocated == 2 && slots.Release(allocated) && valid;
        }
        const bool allocationFree = failNext && consumed == 0;
        failNext = false;
        if (!valid || !allocationFree) throw std::runtime_error("UI descriptor inventory/ownership/allocation contract failed");
        std::cout << "PASS: reserved slots, bounded exhaustion, unchanged refusal output, invalid/double release, LIFO reuse, reset and 1024 no-allocation cycles\n"
            << "IMGUI_DESCRIPTOR_FAULT consumed=" << consumed << " (armed fault unconsumed; allocation-free observed paths, not injected OOM)\n";
        return 0;
    } catch (const std::exception& error) { failNext = false; std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
