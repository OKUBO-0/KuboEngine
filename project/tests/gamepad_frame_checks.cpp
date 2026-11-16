#include "engine/input/GamepadFrame.h"
#include <iostream>
#include <stdexcept>

namespace {
void Require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void Neutral(const Engine::InputSystem::GamepadFrame& frame){
    Require(!frame.connected && !frame.connectedThisFrame && !frame.disconnectedThisFrame,"Gate leaked connection");
    for(const auto& button:frame.buttons)Require(!button.down && !button.pressed && !button.released,"Gate leaked button");
    Require(frame.leftX==0 && frame.leftY==0 && frame.rightX==0 && frame.rightY==0 && frame.leftTrigger==0 && frame.rightTrigger==0,"Gate leaked analog state");
}
}
int main(){
    using namespace Engine::InputSystem;
    try {
        GamepadSample sample;sample.connected=true;sample.buttons=0xffff;
        sample.leftX=-32768;sample.leftY=32767;sample.rightX=-6553;sample.rightY=6553;
        sample.leftTrigger=255;sample.rightTrigger=128;
        const auto pressed=MakeGamepadFrame(sample,{},true);
        Require(pressed.connected && pressed.connectedThisFrame && !pressed.disconnectedThisFrame,"Connect edge missing");
        Require(pressed.leftX==-1 && pressed.leftY==1 && pressed.rightX==0 && pressed.rightY==0 && pressed.leftTrigger==1 && pressed.rightTrigger>0.5f && pressed.rightTrigger<0.51f,"Normalization or dead zone incorrect");
        for(const auto& button:pressed.buttons)Require(button.down && button.pressed && !button.released,"Connect with held button lost press");
        const auto held=MakeGamepadFrame(sample,sample,true);
        Require(!held.connectedThisFrame && !held.disconnectedThisFrame,"Held connection repeated edge");
        for(const auto& button:held.buttons)Require(button.down && !button.pressed && !button.released,"Held repeated edge");
        auto disconnected=sample;disconnected.connected=false;
        const auto release=MakeGamepadFrame(disconnected,sample,true);
        Require(!release.connected && release.disconnectedThisFrame && !release.connectedThisFrame && release.leftX==0 && release.rightTrigger==0,"Disconnect leaked analog sample");
        for(const auto& button:release.buttons)Require(!button.down && !button.pressed && button.released,"Disconnect failed to release held button");
        Neutral(MakeGamepadFrame(disconnected,disconnected,true));
        Neutral(MakeGamepadFrame(sample,disconnected,false));
        Neutral(MakeGamepadFrame(disconnected,sample,false));
        const auto reconnect=MakeGamepadFrame(sample,disconnected,true);
        Require(reconnect.connectedThisFrame && reconnect.buttons[static_cast<size_t>(GamepadButton::A)].pressed,"Reconnect inherited disconnected stale bytes");
        constexpr std::uint16_t masks[]{1,2,4,8,16,32,64,128,256,512,4096,8192,16384,32768};
        for(size_t index=0;index<14;++index){
            auto one=sample;one.buttons=masks[index];
            const auto frame=MakeGamepadFrame(one,{},true);
            for(size_t check=0;check<14;++check)Require(frame.buttons[check].down==(check==index) && frame.buttons[check].pressed==(check==index),"Button mask mapping incorrect");
            one.buttons=0;const auto up=MakeGamepadFrame(one,GamepadSample{true,masks[index]},true);
            Require(up.buttons[index].released && !up.buttons[index].down,"Connected button release missing");
        }
        auto reserved=sample;reserved.buttons=0x0c00;
        for(const auto& button:MakeGamepadFrame(reserved,{},true).buttons)Require(!button.down && !button.pressed,"Reserved mask exposed as button");
        Require(NormalizeGamepadAxis(-6554)<0 && NormalizeGamepadAxis(6554)>0,"Dead zone boundary swallowed");
        for(int raw=-32768;raw<=32767;++raw){
            const auto axis=NormalizeGamepadAxis(static_cast<std::int16_t>(raw));
            Require(std::isfinite(axis) && axis>=-1 && axis<=1,"Signed axis range escaped");
        }
        for(unsigned value=0;value<=255;++value){sample.leftTrigger=static_cast<std::uint8_t>(value);const auto frame=MakeGamepadFrame(sample,{},true);Require(frame.leftTrigger>=0 && frame.leftTrigger<=1,"Trigger range escaped");}
        std::cout<<"PASS: pad connect/disconnect/reconnect, 14 mask mappings, reserved bits, gate neutrality, all signed axis values and trigger values\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
