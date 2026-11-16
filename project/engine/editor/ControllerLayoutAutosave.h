#pragma once
#include <cmath>
namespace Engine::Editor {
// Pure debounce policy. No files, GUI time source or document ownership here.
// A failed save stops automatic retries until an explicit Save/Reload succeeds.
class ControllerLayoutAutosave final {
    bool enabled_=true,allowed_=false,pending_=false,blocked_=false;
    double changedAt_=0;
public:
    static constexpr double QuietSeconds=.75;
    bool Enabled() const noexcept{return enabled_;}
    bool Allowed() const noexcept{return allowed_;}
    bool Pending() const noexcept{return pending_;}
    bool Blocked() const noexcept{return blocked_;}
    void SetEnabled(bool value) noexcept{enabled_=value;}
    void Reset(bool allowed=false) noexcept{allowed_=allowed;pending_=blocked_=false;changedAt_=0;}
    void Saved() noexcept{allowed_=true;pending_=blocked_=false;}
    void Failed() noexcept{blocked_=true;}
    bool Observe(bool changed,double now,bool maySave,bool interacting) noexcept {
        if(!std::isfinite(now) || now<0)return false;
        if(changed){pending_=true;changedAt_=now;}
        // A reset/reversed clock must not prematurely publish preferences.
        if(now<changedAt_){changedAt_=now;return false;}
        return enabled_ && allowed_ && pending_ && !blocked_ && maySave && !interacting && now-changedAt_>=QuietSeconds;
    }
    bool MayFlush() const noexcept{return enabled_ && allowed_ && pending_ && !blocked_;}
};
}
