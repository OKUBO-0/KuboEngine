#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Engine::InputSystem {
// Stable identifiers, not platform button masks. One host-selected controller.
enum class GamepadButton : std::uint8_t {
    DpadUp, DpadDown, DpadLeft, DpadRight, Start, Back,
    LeftStick, RightStick, LeftShoulder, RightShoulder, A, B, X, Y, Count
};
struct GamepadButtonFrame { bool down=false, pressed=false, released=false; };
struct GamepadFrame {
    bool connected=false, connectedThisFrame=false, disconnectedThisFrame=false;
    std::array<GamepadButtonFrame,static_cast<std::size_t>(GamepadButton::Count)> buttons{};
    float leftX=0,leftY=0,rightX=0,rightY=0,leftTrigger=0,rightTrigger=0;
};
// Host-owned sample using the XInput wire layout; no Windows/device dependency.
// Disconnected samples may contain stale bytes, which must not be exposed.
struct GamepadSample {
    bool connected=false;
    std::uint16_t buttons=0;
    std::int16_t leftX=0,leftY=0,rightX=0,rightY=0;
    std::uint8_t leftTrigger=0,rightTrigger=0;
};
inline float NormalizeGamepadAxis(std::int16_t raw) noexcept {
    // Signed negative extreme is -32768, positive extreme is 32767.
    const float value=static_cast<float>(raw)/(raw<0 ? 32768.0f : 32767.0f);
    return std::fabs(value)<0.2f ? 0.0f : value;
}
inline GamepadFrame MakeGamepadFrame(const GamepadSample& current,
    const GamepadSample& previous,bool available) noexcept
{
    GamepadFrame frame;
    if(!available)return frame;
    frame.connected=current.connected;
    frame.connectedThisFrame=current.connected && !previous.connected;
    frame.disconnectedThisFrame=!current.connected && previous.connected;
    constexpr std::array<std::uint16_t,static_cast<std::size_t>(GamepadButton::Count)> masks{
        0x0001,0x0002,0x0004,0x0008,0x0010,0x0020,0x0040,0x0080,
        0x0100,0x0200,0x1000,0x2000,0x4000,0x8000};
    for(std::size_t index=0;index<masks.size();++index) {
        const bool down=current.connected && (current.buttons&masks[index])!=0;
        const bool wasDown=previous.connected && (previous.buttons&masks[index])!=0;
        frame.buttons[index]={down,down&&!wasDown,!down&&wasDown};
    }
    if(current.connected) {
        frame.leftX=NormalizeGamepadAxis(current.leftX);
        frame.leftY=NormalizeGamepadAxis(current.leftY);
        frame.rightX=NormalizeGamepadAxis(current.rightX);
        frame.rightY=NormalizeGamepadAxis(current.rightY);
        frame.leftTrigger=static_cast<float>(current.leftTrigger)/255.0f;
        frame.rightTrigger=static_cast<float>(current.rightTrigger)/255.0f;
    }
    return frame;
}
}
