#pragma once
#include "ActionMap.h"
#include "GamepadFrame.h"
#include <array>
#include <cmath>
#include <utility>

namespace Engine::InputSystem {
struct ButtonFrame { bool down=false, pressed=false, released=false; };
struct ActionFrame { ButtonFrame button; Vector3 axis{}; };
// A host-owned read-only snapshot, never a device pointer exposed to scripts.
struct InputFrame {
    bool available=false;
    std::array<ButtonFrame,256> keys{};
    std::array<ButtonFrame,8> mouse{};
    GamepadFrame pad{};
    float mouseX=0,mouseY=0,deltaX=0,deltaY=0,wheel=0;
    std::unordered_map<std::string,ActionFrame> actions;
    void Reset() noexcept {
        available=false;keys.fill({});mouse.fill({});pad={};
        mouseX=mouseY=deltaX=deltaY=wheel=0;actions.clear();
    }
    void Swap(InputFrame& other) noexcept {
        static_assert(noexcept(actions.swap(other.actions)));
        std::swap(available,other.available);keys.swap(other.keys);mouse.swap(other.mouse);
        std::swap(pad,other.pad);
        std::swap(mouseX,other.mouseX);std::swap(mouseY,other.mouseY);
        std::swap(deltaX,other.deltaX);std::swap(deltaY,other.deltaY);std::swap(wheel,other.wheel);
        actions.swap(other.actions);
    }
};
inline ButtonFrame MakeButtonFrame(bool down,bool previous) { return {down,down&&!previous,!down&&previous}; }
inline InputFrame MakeInputFrame(const ActionMap& map,const std::array<bool,256>& keys,
    const std::array<bool,256>& previous,const std::array<bool,8>& mouse,
    const std::array<bool,8>& previousMouse,bool available,
    const GamepadSample& currentPad={},const GamepadSample& previousPad={})
{
    InputFrame result;
    if(!available) return result;
    result.available=true;
    result.pad=MakeGamepadFrame(currentPad,previousPad,true);
    const auto oldPad=MakeGamepadFrame(previousPad,{},true);
    for(size_t index=0;index<keys.size();++index) result.keys[index]=MakeButtonFrame(keys[index],previous[index]);
    for(size_t index=0;index<mouse.size();++index) result.mouse[index]=MakeButtonFrame(mouse[index],previousMouse[index]);
    for(const auto& [name,bindings]:map.Bindings()) {
        auto query=[&](int code) { return code>=0 && code<256 && keys[static_cast<size_t>(code)]; };
        auto oldQuery=[&](int code) { return code>=0 && code<256 && previous[static_cast<size_t>(code)]; };
        ButtonFrame button;
        for(const auto& binding:bindings){
            const bool down=ActionMap::IsActive(binding,ActionMap::Value(binding,query,result.pad));
            const bool wasDown=ActionMap::IsActive(binding,ActionMap::Value(binding,oldQuery,oldPad));
            button.down|=down;button.pressed|=down&&!wasDown;button.released|=!down&&wasDown;
        }
        auto axis=map.Evaluate(name,query,result.pad);
        if(!std::isfinite(axis.x)||!std::isfinite(axis.y)||!std::isfinite(axis.z)) axis={};
        result.actions[name]={button,axis};
    }
    return result;
}
}
