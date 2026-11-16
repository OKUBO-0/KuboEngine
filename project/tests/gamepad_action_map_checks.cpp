#include "engine/input/InputFrame.h"
#include "engine/input/FixedInputBuffer.h"
#include <Windows.h>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void Require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
std::string Read(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
}
int main(){
    using namespace Engine::InputSystem;
    const auto fixture=std::filesystem::temp_directory_path()/(L"kubo-pad-actions-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try {
        ActionMap map;map.Bind("jump",10,{1,0,0});map.BindPadButton("jump",GamepadButton::A,{2,0,0});
        map.BindPadAxis("移動",ActionMap::PadAxis::LeftX,{2,0,0},0.5f);map.BindPadAxis("移動",ActionMap::PadAxis::LeftY,{0,0,3},0.5f);
        map.BindPadAxis("brake",ActionMap::PadAxis::RightTrigger,{0,1,0},0.75f);
        map.BindPadButton("jump",GamepadButton::A,{3,0,0});Require(map.BindingCount("jump")==2,"Source/code identity collided or duplicated");
        std::array<bool,256> keys{},previous{};std::array<bool,8> mouse{};
        GamepadSample pad;pad.connected=true;pad.buttons=0x1000;pad.leftX=-32768;pad.leftY=32767;pad.rightTrigger=128;
        auto first=MakeInputFrame(map,keys,previous,mouse,mouse,true,pad,{});
        Require(first.actions.at("jump").button.pressed && first.actions.at("jump").axis.x==3 && first.actions.at("移動").axis.x==-2 && first.actions.at("移動").axis.z==3 && first.actions.at("移動").button.pressed,"Pad mapping value/press wrong");
        Require(!first.actions.at("brake").button.down && first.actions.at("brake").axis.y>0.5f,"Analog value incorrectly gated by activation threshold");
        keys[10]=true;auto combined=MakeInputFrame(map,keys,previous,mouse,mouse,true,pad,pad);
        Require(combined.actions.at("jump").button.pressed && combined.actions.at("jump").axis.x==4,"Additional keyboard binding did not preserve edge semantics");
        previous=keys;auto disconnected=pad;disconnected.connected=false;
        auto released=MakeInputFrame(map,keys,previous,mouse,mouse,true,disconnected,pad);
        Require(released.actions.at("jump").button.down && released.actions.at("jump").button.released && released.actions.at("jump").axis.x==1 && released.actions.at("移動").button.released && released.actions.at("移動").axis.x==0,"Disconnect release/remaining key wrong");
        auto disabled=MakeInputFrame(map,keys,previous,mouse,mouse,false,pad,pad);Require(disabled.actions.empty() && !disabled.pad.connected,"Disabled actions leaked");
        GamepadSample low=pad;low.leftX=8192;low.leftY=0;low.rightTrigger=255;
        auto crossed=MakeInputFrame(map,{}, {},mouse,mouse,true,low,pad);
        Require(crossed.actions.at("移動").button.released && !crossed.actions.at("移動").button.down && crossed.actions.at("移動").axis.x>0 && crossed.actions.at("brake").button.pressed,"Axis threshold crossing wrong");
        low.leftX=-32768;auto sign=MakeInputFrame(map,{}, {},mouse,mouse,true,low,pad);
        Require(sign.actions.at("移動").button.down && !sign.actions.at("移動").button.pressed,"Signed held axis repeated press");
        Require(!map.Active("jump",[](int){return false;}) && map.Evaluate("jump",[](int code){return code==10;}).x==1,"Legacy keyboard-only query interpreted pad code as key");
        FixedInputBuffer fifo;fifo.Push(first);fifo.Push(released);Require(fifo.Usage().pendingSamples==2 && fifo.Consume().actions.at("jump").button.pressed && fifo.Consume().actions.at("jump").button.released && !fifo.Consume().actions.at("jump").button.released,"Mapped action FIFO repeated/reordered edges");
        std::filesystem::create_directories(fixture);const auto path=fixture/L"設定.json";std::string error;
        Require(map.Save(path,error),error.c_str());const auto valid=Read(path);Require(valid.find("pad_axis")!=std::string::npos && valid.find("pad_button")!=std::string::npos,"Extended sources not persisted");
        ActionMap loaded;Require(loaded.Load(path,error),error.c_str());Require(loaded.BindingCount("jump")==2 && loaded.Evaluate("移動",[](int){return false;},first.pad).x==-2 && loaded.Bindings().at("brake")[0].activationThreshold==0.75f,"Extended roundtrip lost threshold/domain");
        for(const auto& bad:{
            R"({"version":2,"actions":{"x":[{"source":"pad_button","code":14,"contribution":[1,0,0]}]}})",
            R"({"version":2,"actions":{"x":[{"source":"pad_axis","code":6,"threshold":0.5,"contribution":[1,0,0]}]}})",
            R"({"version":2,"actions":{"x":[{"source":"pad_axis","code":0,"threshold":0,"contribution":[1,0,0]}]}})",
            R"({"version":2,"actions":{"x":[{"source":"pad_axis","code":0,"threshold":1.1,"contribution":[1,0,0]}]}})",
            R"({"version":2,"actions":{"x":[{"source":"pad_axis","code":0,"contribution":[1,0,0]}]}})",
            R"({"version":2,"actions":{"x":[{"source":"other","code":0,"contribution":[1,0,0]}]}})",
            R"({"version":2,"actions":{"x":[{"source":"keyboard","key":1,"code":0,"contribution":[1,0,0]}]}})",
            R"({"version":1,"actions":{"x":[{"source":"pad_button","key":10,"contribution":[1,0,0]}]}})",
            R"({"version":2,"actions":{"x":[{"source":"pad_button","code":0.5,"contribution":[1,0,0]}]}})"}){
            {std::ofstream out(path);out<<bad;}Require(!loaded.Load(path,error) && loaded.BindingCount("jump")==2 && loaded.Bindings().at("brake")[0].activationThreshold==0.75f,"Malformed extended load replaced live map");
        }
        Require(map.Save(path,error),error.c_str());
        ActionMap invalid;invalid.BindPadAxis("x",ActionMap::PadAxis::LeftX,{1,0,0},std::numeric_limits<float>::quiet_NaN());Require(!invalid.Save(path,error) && Read(path)==valid,"Invalid threshold save replaced existing file");
        invalid.Clear();invalid.BindPadButton("x",static_cast<GamepadButton>(255));Require(!invalid.Save(path,error) && Read(path)==valid,"Invalid button save replaced existing file");
        ActionMap keyboard;keyboard.Bind("old",17);Require(keyboard.Save(path,error) && Read(path).find("pad_")==std::string::npos && loaded.Load(path,error) && loaded.BindingCount("old")==1,"Legacy keyboard roundtrip changed");
        std::error_code cleanup;std::filesystem::remove_all(fixture,cleanup);
        std::cout<<"PASS: mixed-domain action values/edges, axis thresholds, disconnect/gates/FIFO, version1/2 roundtrip and invalid load/save isolation\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<"; retained fixture "<<fixture<<'\n';return 1;}
}
