#pragma once
#include <utility>

namespace Engine::Base {
// WM_CLOSE is deferred only when an application explicitly enables confirmation.
class CloseRequestGate final {
public:
    void Enable(bool enabled) { enabled_ = enabled; if (!enabled) requested_ = false; }
    bool OnClose()
    {
        if (!enabled_) return false;
        requested_ = true;
        return true;
    }
    bool Consume() { return std::exchange(requested_, false); }
private:
    bool enabled_ = false, requested_ = false;
};
}
