#pragma once
#include <cstdint>
#include <limits>
#include <stdexcept>
namespace Engine::Editor {
struct ControllerDocumentEpoch final {
    static uint64_t Next(uint64_t current) {
        if(current==(std::numeric_limits<uint64_t>::max)())throw std::overflow_error("Controller GUI document epoch exhausted");
        return current+1;
    }
};
}
