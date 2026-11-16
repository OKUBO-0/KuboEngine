#pragma once
#include "ViewExtent.h"
#include <optional>

namespace Engine::Base {
// Owner-thread transient policy: settle for 150 ms, or refresh at most every
// 500 ms during continuous dragging. Observation and application are separate
// so the image can always use the extent actually rendered this frame.
class ViewResizeSchedule {
public:
    void Cancel() { pending_.reset(); quiet_ = total_ = 0; changed_ = false; }
    void Observe(float width, float height, ViewExtent current) {
        ViewExtent extent;
        if (!PanelViewExtent(width,height,extent) || extent == current) { Cancel(); return; }
        if (!pending_ || *pending_ != extent) {
            if (!pending_) total_ = 0;
            quiet_ = 0; changed_ = true;
        }
        pending_ = extent;
    }
    std::optional<ViewExtent> Advance(float delta, ViewExtent current) {
        if (!pending_) return {};
        if (*pending_ == current) { Cancel(); return {}; }
        if (!std::isfinite(delta) || delta <= 0) return {};
        const double step = (std::min)(double(delta),0.25);
        // A hitch on the very frame a size changed is not evidence that this
        // latest size has already been stable for that entire interval.
        if (!changed_) quiet_ += step;
        changed_ = false; total_ += step;
        if (quiet_ + 1e-8 >= 0.15 || total_ + 1e-8 >= 0.5) return pending_;
        return {};
    }
    // Called only after successful target replacement. On an exception the
    // caller can cancel/retry without claiming that the new extent is live.
    void Applied() { Cancel(); }
private:
    std::optional<ViewExtent> pending_;
    double quiet_ = 0, total_ = 0;
    bool changed_ = false;
};
}
