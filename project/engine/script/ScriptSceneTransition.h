#pragma once

#include <cmath>
#include <optional>
#include <string>
#include <utility>

namespace Engine::Scripting {

struct ScriptSceneTransition {
    std::string destination;
    double delaySeconds = 0;
};

// Main-thread slot. Lua stages requests per hook and only publishes on success.
class ScriptSceneTransitions final {
public:
    void SetEnabled(bool enabled) {
        enabled_ = enabled;
        if (!enabled) Clear();
    }
    bool Available() const noexcept {return enabled_ && !pending_;}
    void Publish(ScriptSceneTransition request) noexcept {
        if (Available()) pending_.emplace(std::move(request));
    }
    std::optional<ScriptSceneTransition> TakeReady(float seconds) {
        if (!pending_ || !std::isfinite(seconds) || seconds<=0) return std::nullopt;
        pending_->delaySeconds -= double(seconds);
        if (pending_->delaySeconds>0) return std::nullopt;
        return std::exchange(pending_,std::nullopt);
    }
    void Clear() noexcept {pending_.reset();}
private:
    bool enabled_ = false;
    std::optional<ScriptSceneTransition> pending_;
};

} // namespace Engine::Scripting
