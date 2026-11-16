#include "engine/script/ScriptRuntime.h"
#include "engine/physics/PhysicsWorld.h"
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

void Require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
int main(){
    using namespace Engine::InputSystem;using namespace Engine::Scripting;using namespace Engine::Scene;using namespace Engine::Physics;
    const auto fixture=std::filesystem::temp_directory_path()/(L"kubo-fixed-input-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try {
        InputFrame press;press.available=true;press.keys[17]={true,true,false};press.mouse[0]={true,true,false};press.actions["移動"]={{true,true,false},{1,0,0}};press.deltaX=2;press.deltaY=3;press.wheel=1;press.mouseX=11;
        auto release=press;release.keys[17]={false,false,true};release.mouse[0]={false,false,true};release.actions["移動"]={{false,false,true},{}};release.deltaX=4;release.deltaY=5;release.wheel=-1;release.mouseX=12;
        FixedInputBuffer buffer;buffer.Push(press);buffer.Push(release);Require(buffer.Usage().pendingSamples==2,"No-tick transitions lost");
        auto first=buffer.Consume(),second=buffer.Consume(),held=buffer.Consume();
        Require(first.keys[17].pressed && first.keys[17].down && first.mouse[0].pressed && first.actions.at("移動").button.pressed && first.deltaX==2 && first.mouseX==11,"Historical press sample changed");
        Require(second.keys[17].released && !second.keys[17].down && second.mouse[0].released && second.actions.at("移動").button.released && second.deltaX==4 && second.mouseX==12,"Press/release sample order lost");
        Require(!held.keys[17].pressed && !held.keys[17].released && held.deltaX==0 && held.deltaY==0 && held.wheel==0 && held.mouseX==12 && !held.actions.at("移動").button.released,"Transient repeated after consumption");
        buffer.Push(press);buffer.Push({});Require(!buffer.Usage().pendingSamples && !buffer.Consume().available,"Input gate retained stale queue");
        for(size_t n=0;n<FixedInputBuffer::MaximumPendingSamples+3;++n){auto frame=press;frame.mouseX=float(n);buffer.Push(frame);}
        Require(buffer.Usage().pendingSamples==64 && buffer.Usage().droppedSamples==3 && buffer.Consume().mouseX==3,"Bound/drop-oldest telemetry incorrect");buffer.Reset();Require(!buffer.Usage().droppedSamples && !buffer.Consume().available,"Reset retained queue/telemetry");
        std::filesystem::create_directories(fixture);
        {std::ofstream out(fixture/"input.lua",std::ios::binary);out<<R"(
function on_update(e,dt)
  if input.key_pressed(input.keys.W) then e:translate(0,10,0) end
end
function on_fixed_update(e,dt)
  local n=0
  if input.key_pressed(input.keys.W) then n=n+1 end
  if input.key_released(input.keys.W) then n=n+2 end
  if input.mouse_pressed(0) then n=n+4 end
  if input.mouse_released(0) then n=n+8 end
  if input.action_pressed("移動") then n=n+16 end
  if input.action_released("移動") then n=n+32 end
  local dx,dy=input.mouse_delta()
  e:translate(n,0,dx+dy+input.mouse_wheel())
end
)";out.close();Require(!out.fail(),"Input script fixture write failed");}
        ScriptRuntime runtime;std::string error;Require(runtime.LoadResolvedAsset("input",fixture/"input.lua",error),error.c_str());EntityWorld world;const auto a=world.CreateEntity("First"),b=world.CreateEntity("Second");world.SetScript(a,"input");world.SetScript(b,"input");PhysicsWorld physics;
        auto frame=[&](InputFrame input,float dt){runtime.SetInputFrame(std::move(input));auto budget=runtime.BeginFrame();runtime.Update(world,dt);physics.Advance(world,dt,[&](float tick){runtime.FixedUpdate(world,tick);});};
        frame(press,1.0f/240);frame(release,1.0f/240);Require(!physics.GetAdvanceUsage().steps && runtime.GetFixedInputUsage().pendingSamples==2 && world.Find(a)->transform.position.y==10,"Host no-tick buffering/variable input changed");
        InputFrame neutral=release;neutral.keys[17].released=neutral.mouse[0].released=neutral.actions.at("移動").button.released=false;neutral.deltaX=neutral.deltaY=neutral.wheel=0;
        frame(neutral,0.1f);Require(world.Find(a)->transform.position.x==63 && world.Find(a)->transform.position.y==10 && world.Find(a)->transform.position.z==14 && world.Find(b)->transform.position.x==63 && world.Find(b)->transform.position.z==14 && !runtime.GetFixedInputUsage().pendingSamples,"Lua peers differ or catch-up repeats/drops input");
        ScriptCallbacks throwing;throwing.onFixedUpdate=[](auto&,float){throw std::runtime_error("Native sentinel");};runtime.Register("throw",throwing);const auto native=world.CreateEntity("Throw");world.SetScript(native,"throw");
        runtime.SetInputFrame(press);runtime.FixedUpdate(world,0);Require(runtime.GetFixedInputUsage().pendingSamples==1,"Invalid fixed delta consumed input");
        try{runtime.FixedUpdate(world,1.0f/60);Require(false,"Native exception swallowed");}catch(const std::runtime_error& failure){Require(std::string(failure.what())=="Native sentinel","Unexpected fixed exception");}
        world.Find(native)->script.enabled=false;runtime.Update(world,0.01f);Require(world.Find(a)->transform.position.y==20,"Native exception failed to restore variable input");
        runtime.SetInputFrame(press);runtime.SetInputFrame({});runtime.FixedUpdate(world,1.0f/60);Require(world.Find(a)->transform.position.x==84 && !runtime.GetFixedInputUsage().pendingSamples,"Unavailable input replayed stale edges");
        runtime.SetInputFrame(press);runtime.Shutdown(world);Require(!runtime.GetFixedInputUsage().pendingSamples && !runtime.GetFixedInputUsage().droppedSamples,"Shutdown retained pending input");runtime.Clear();
        std::error_code cleanup;std::filesystem::remove_all(fixture,cleanup);std::cout<<"PASS: fixed input FIFO/order/bounds, no-tick buffering, Lua peer broadcast, single consumption, variable isolation, exception restoration and gate/reset cleanup\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<"; retained fixture "<<fixture<<'\n';return 1;}
}
