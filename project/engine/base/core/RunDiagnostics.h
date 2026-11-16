#pragma once
#include "RunFailure.h"
#include <Windows.h>
#include <cstdio>

namespace Engine::Base {
// No Logger/string construction: diagnostics must also work during allocation
// failure. Unknown native exception types are reported, not silently discarded.
inline void ReportRunException(const char* label, const std::exception_ptr& error) noexcept {
    if (!error) return;
    const auto report = [label](const char* message) noexcept {
        std::fprintf(stderr, "KuboEngine %s: %s\n", label, message);
        OutputDebugStringA("KuboEngine "); OutputDebugStringA(label);
        OutputDebugStringA(": "); OutputDebugStringA(message); OutputDebugStringA("\n");
    };
    try { std::rethrow_exception(error); }
    catch (const std::exception& failure) { report(failure.what()); }
    catch (...) { report("non-standard C++ exception"); }
    std::fflush(stderr);
}
inline void ReportRunFailure(const RunFailure& failure) noexcept {
    ReportRunException("operation error", failure.Operation());
    ReportRunException("first shutdown error", failure.Cleanup());
}
}
