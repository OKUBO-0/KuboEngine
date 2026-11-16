#include "engine/input/InputFrame.h"
#include <iostream>
#include <limits>
#include <stdexcept>
namespace { void Require(bool value,const char* why) { if(!value) throw std::runtime_error(why); } }
int main() {
    using namespace Engine::InputSystem;
    try {
        ActionMap map;map.Bind("move",17,{0,0,1});map.Bind("move",31,{0,0,-1});map.Bind("移動",32,{1,0,0});map.Bind("invalid",999,{1,0,0});
        std::array<bool,256> keys{},previous{};std::array<bool,8> mouse{},previousMouse{};
        keys[17]=true;mouse[0]=true;
        auto frame=MakeInputFrame(map,keys,previous,mouse,previousMouse,true);
        Require(frame.available && frame.keys[17].down && frame.keys[17].pressed && !frame.keys[17].released,"Key press snapshot incorrect");
        Require(frame.mouse[0].pressed && frame.actions.at("move").button.pressed && frame.actions.at("move").axis.z==1,"Mouse/action press snapshot incorrect");
        Require(!frame.actions.at("invalid").button.down,"Invalid scan code wrapped");
        previous=keys;previousMouse=mouse;keys[31]=true;keys[32]=true;
        frame=MakeInputFrame(map,keys,previous,mouse,previousMouse,true);
        Require(frame.actions.at("move").button.down && frame.actions.at("move").button.pressed && frame.actions.at("move").axis.z==0,"Opposing bindings or additional-key trigger incorrect");
        Require(frame.actions.at("移動").axis.x==1 && !frame.keys[17].pressed,"Unicode action or held key incorrect");
        previous=keys;previousMouse=mouse;keys[17]=false;mouse[0]=false;
        frame=MakeInputFrame(map,keys,previous,mouse,previousMouse,true);
        Require(frame.keys[17].released && frame.mouse[0].released && frame.actions.at("move").button.released && frame.actions.at("move").button.down && frame.actions.at("move").axis.z==-1,"Release binding semantics incorrect");
        auto disabled=MakeInputFrame(map,keys,previous,mouse,previousMouse,false);
        Require(!disabled.available && disabled.actions.empty() && !disabled.keys[31].down && !disabled.keys[17].released,"Disabled input leaked physical state");
        map.Bind("nonfinite",31,{std::numeric_limits<float>::infinity(),0,0});
        frame=MakeInputFrame(map,keys,previous,mouse,previousMouse,true);
        Require(frame.actions.at("nonfinite").axis.x==0,"Nonfinite binding propagated into snapshot");
        keys[31]=false;Require(frame.keys[31].down,"Snapshot retained mutable physical buffer");
        std::cout<<"PASS: input snapshot edges, named/Unicode actions, opposing axes, valid scan codes, neutral gate, finite axes and owned data\n";return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
