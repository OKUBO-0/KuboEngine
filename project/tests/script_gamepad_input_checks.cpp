#include "engine/script/ScriptRuntime.h"
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void Require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void Write(const std::filesystem::path& path,const std::string& source){
    std::ofstream out(path,std::ios::binary);out<<source;out.close();Require(!out.fail(),"Fixture write failed");
}
}
int main(){
    using namespace Engine::InputSystem;using namespace Engine::Scripting;using namespace Engine::Scene;
    const auto fixture=std::filesystem::temp_directory_path()/(L"kubo-pad-input-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try {
        GamepadSample raw;raw.connected=true;raw.buttons=0x1000;raw.leftX=32767;raw.rightTrigger=255;
        ActionMap map;map.BindPadButton("jump",GamepadButton::A);map.BindPadAxis("move",ActionMap::PadAxis::LeftX,{2,0,0});
        const std::array<bool,256> keys{};const std::array<bool,8> mouse{};
        InputFrame press=MakeInputFrame(map,keys,keys,mouse,mouse,true,raw,{});
        auto off=raw;off.connected=false;
        auto release=MakeInputFrame(map,keys,keys,mouse,mouse,true,off,raw);
        auto reconnect=MakeInputFrame(map,keys,keys,mouse,mouse,true,raw,off);
        auto held=MakeInputFrame(map,keys,keys,mouse,mouse,true,raw,raw);
        auto swapped=release;swapped.Swap(press);
        Require(swapped.pad.connectedThisFrame && press.pad.disconnectedThisFrame,"InputFrame Swap lost pad state");press.Swap(swapped);
        auto cleared=press;cleared.Reset();Require(!cleared.available && !cleared.pad.connected && !cleared.pad.buttons[10].pressed && cleared.pad.leftX==0,"InputFrame Reset retained pad");
        FixedInputBuffer fifo;fifo.Push(press);fifo.Push(release);fifo.Push(reconnect);
        Require(fifo.Usage().pendingSamples==3,"Pad-only transitions not queued");
        const auto one=fifo.Consume(),two=fifo.Consume(),three=fifo.Consume(),four=fifo.Consume();
        Require(one.pad.buttons[10].pressed && two.pad.buttons[10].released && two.pad.disconnectedThisFrame && three.pad.connectedThisFrame && three.pad.buttons[10].pressed,"Pad FIFO reordered connection/button transitions");
        Require(four.pad.connected && four.pad.buttons[10].down && !four.pad.buttons[10].pressed && !four.pad.connectedThisFrame && four.pad.leftX==1,"Pad transient repeated or analog lost");
        InputFrame connectOnly;connectOnly.available=true;connectOnly.pad=MakeGamepadFrame(GamepadSample{true},{},true);
        fifo.Push(connectOnly);Require(fifo.Usage().pendingSamples==1,"Buttonless connection edge not queued");fifo.Push({});Require(fifo.Usage().pendingSamples==0 && !fifo.Consume().pad.connected,"Gate retained pad queue");
        for(size_t index=0;index<67;++index)fifo.Push(press);
        Require(fifo.Usage().pendingSamples==64 && fifo.Usage().droppedSamples==3,"Pad-only queue bound changed");fifo.Reset();
        std::filesystem::create_directories(fixture);
        Write(fixture/"pad.lua",R"(
local function edges()
  assert(input.action_down('jump')==input.pad_down(input.pad_buttons.A))
  assert(input.action_pressed('jump')==input.pad_pressed(input.pad_buttons.A))
  assert(input.action_released('jump')==input.pad_released(input.pad_buttons.A))
  local x,y,z=input.action_axis('move');local sx=input.pad_stick()
  assert(x==2*sx and y==0 and z==0)
  local n=0
  if input.pad_pressed(input.pad_buttons.A) then n=n+1 end
  if input.pad_released(input.pad_buttons.A) then n=n+2 end
  if input.pad_connected_this_frame() then n=n+4 end
  if input.pad_disconnected_this_frame() then n=n+8 end
  return n
end
function on_update(e,dt) e:translate(0,0,edges()) end
function on_fixed_update(e,dt)
  local x,y=input.pad_stick()
  local rx,ry=input.pad_stick(true)
  assert(y==0 and rx==0 and ry==0 and input.pad_trigger()==0)
  assert(input.pad_down(input.pad_buttons.A)==input.pad_connected())
  e:translate(edges(),x+input.pad_trigger(true),0)
end
)");
        ScriptRuntime runtime;std::string error;Require(runtime.LoadResolvedAsset("pad",fixture/"pad.lua",error),error.c_str());
        EntityWorld world;const auto a=world.CreateEntity("A"),b=world.CreateEntity("B");world.SetScript(a,"pad");world.SetScript(b,"pad");
        for(const auto& frame:{press,release,reconnect}){runtime.SetInputFrame(frame);runtime.Update(world,0.001f);}
        Require(runtime.GetFixedInputUsage().pendingSamples==3 && world.Find(a)->transform.position.z==20,"No-tick pad buffering/variable delivery failed");
        runtime.SetInputFrame(held);
        for(int tick=0;tick<4;++tick)runtime.FixedUpdate(world,1.0f/60);
        for(const auto id:{a,b})Require(world.Find(id)->transform.position.x==20 && world.Find(id)->transform.position.y==6 && world.Find(id)->transform.position.z==20 && runtime.GetLastError(id).empty(),"Lua fixed peers lost/repeated pad transitions or analog");
        runtime.Update(world,0.001f);Require(world.Find(a)->transform.position.z==20,"Fixed snapshot replaced variable snapshot");
        runtime.SetInputFrame(press);runtime.SetInputFrame({});runtime.FixedUpdate(world,1.0f/60);
        Require(world.Find(a)->transform.position.x==20 && world.Find(a)->transform.position.y==6,"Unavailable input leaked pad reads");
        runtime.Shutdown(world);runtime.Clear();
        const std::string invalid[]{"input.pad_down(-1)","input.pad_pressed(14)","input.pad_released(1.5)","input.pad_down('A')","input.pad_stick(1)","input.pad_trigger('right')"};
        for(size_t index=0;index<sizeof(invalid)/sizeof(invalid[0]);++index){
            Write(fixture/"bad.lua","function on_update(e,dt) "+invalid[index]+"; e:translate(100,0,0) end");
            ScriptRuntime bad;Require(bad.LoadResolvedAsset("bad",fixture/"bad.lua",error),error.c_str());EntityWorld faults;const auto id=faults.CreateEntity("Fault");faults.SetScript(id,"bad");bad.SetInputFrame(held);bad.Update(faults,0.01f);
            Require(!bad.GetLastError(id).empty() && faults.Find(id)->transform.position.x==0,"Invalid pad argument accepted");bad.Shutdown(faults);bad.Clear();
        }
        Write(fixture/"outside.lua","input.pad_connected(); function on_update(e,dt) end");
        ScriptRuntime outside;Require(!outside.LoadResolvedAsset("outside",fixture/"outside.lua",error),"Module-scope pad read accepted");
        std::error_code cleanup;std::filesystem::remove_all(fixture,cleanup);
        std::cout<<"PASS: pad InputFrame Reset/Swap, fixed FIFO/bounds/gates, Lua variable/fixed peer delivery, disconnect release, hook/argument guards\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<"; retained fixture "<<fixture<<'\n';return 1;}
}
