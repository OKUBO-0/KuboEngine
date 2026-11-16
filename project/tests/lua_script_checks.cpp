#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "engine/script/ScriptRuntime.h"
#include "engine/script/ScriptReference.h"
#include <Windows.h>
#include <filesystem>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include "engine/editor/EntityHistory.h"
#include "engine/scene/entity/WorldPlayback.h"

namespace {
void Require(bool value,const char* why) { if(!value) throw std::runtime_error(why); }
void Write(const std::filesystem::path& path,const std::string& source) { std::ofstream file(path,std::ios::binary|std::ios::trunc);file<<source;file.close();Require(!file.fail(),"Cannot write Lua fixture"); }
}
int main() {
    using namespace Engine::Scene;using namespace Engine::Scripting;
    const auto fixture=std::filesystem::temp_directory_path()/(L"kubo-lua-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try {
        std::filesystem::create_directories(fixture/"Assets");
        const auto path=fixture/L"Assets/動作.LUA";std::string error;
        const std::string source=R"(
assert(io==nil and os==nil and package==nil and debug==nil and coroutine==nil)
assert(load==nil and loadfile==nil and dofile==nil and pcall==nil and xpcall==nil and setmetatable==nil and string.dump==nil)
assert(utf8.len("日本語")==3 and math.abs(-2)==2)
local ticks=0
function on_create(entity)
  assert(entity:id()>0 and entity:active())
  entity:translate(1,0,0)
  entity:set_rotation(0.1,0.2,0.3)
  entity:set_scale(-2,3,4)
  entity:set_velocity(1,2,3)
end
function on_update(entity,dt)
  ticks=ticks+1
  local x,y,z=entity:position()
  entity:set_position(x+ticks*dt,y,z)
end
function on_destroy(entity) entity:translate(-1,0,0) end
)";
        Write(path,"\xEF\xBB\xBF"+source);
        ScriptRuntime runtime;Require(runtime.LoadResolvedAsset("mover",path,error),error.c_str());
        EntityWorld world;const auto first=world.CreateEntity("First"),second=world.CreateEntity("Second");
        for(const auto id:{first,second}) { world.SetRigidBody(id,{});world.SetScript(id,"mover"); }
        runtime.Update(world,0.5f);
        Require(runtime.GetRunningCount()==2 && world.Find(first)->transform.position.x==1.5f && world.Find(second)->transform.position.x==1.5f,"Lua VM/local state was shared across entities");
        Require(world.Find(first)->transform.scale.x==-2 && world.Find(first)->rigidBody.velocity.y==2 && world.Find(first)->transform.rotation.z==0.3f,"Lua authored component bindings failed");
        runtime.Update(world,0.5f);Require(world.Find(first)->transform.position.x==2.5f && world.Find(second)->transform.position.x==2.5f,"Lua local state did not persist per instance");
        world.Find(first)->script.enabled=false;runtime.Update(world,0.5f);
        Require(world.Find(first)->transform.position.x==1.5f && !world.Find(first)->script.started && runtime.GetRunningCount()==1,"Lua disable did not run destruction once");
        world.Find(first)->script.enabled=true;runtime.Update(world,0.5f);
        Require(world.Find(first)->transform.position.x==3 && runtime.GetRunningCount()==2,"Lua reenable reused stale VM state");
        const auto count=runtime.GetRegisteredCount();
        Write(path,"function on_update(");Require(!runtime.LoadResolvedAsset("mover",path,error) && runtime.GetRegisteredCount()==count,"Syntax failure replaced valid Lua registration");
        Write(path,"on_update=1");Require(!runtime.LoadResolvedAsset("mover",path,error),"Invalid hook type accepted");
        Write(path,"function on_create(e) end");Require(!runtime.LoadResolvedAsset("mover",path,error),"Missing update hook accepted");
        Write(path,"while true do end");Require(!runtime.LoadResolvedAsset("mover",path,error) && error.find("budget")!=std::string::npos,"Infinite module was not bounded");
        Write(path,std::string("\x1bLua",4));Require(!runtime.LoadResolvedAsset("mover",path,error),"Lua bytecode was accepted");
        runtime.Shutdown(world);const auto stopped=world.Capture().dump();runtime.Shutdown(world);
        Require(runtime.GetRunningCount()==0 && world.Capture().dump()==stopped,"Lua shutdown duplicated destruction");

        EntityWorld faults;ScriptRuntime faultRuntime;const auto broken=faults.CreateEntity("Broken"),healthy=faults.CreateEntity("Healthy");
        faults.SetScript(broken,"broken");faults.SetScript(healthy,"healthy");
        Write(path,"function on_update(e,dt) e:translate(7,0,0); while true do end end");Require(faultRuntime.LoadResolvedAsset("broken",path,error),error.c_str());
        Write(path,"function on_update(e,dt) e:translate(dt,0,0) end");Require(faultRuntime.LoadResolvedAsset("healthy",path,error),error.c_str());
        faultRuntime.Update(faults,0.25f);
        Require(faults.Find(broken)->transform.position.x==0 && faults.Find(healthy)->transform.position.x==0.25f && faultRuntime.GetRunningCount()==1,"Lua hook failure mutated entity or stopped healthy peer");
        const auto diagnostic=faultRuntime.GetLastError(broken);Require(diagnostic.find("budget")!=std::string::npos,"Lua instruction failure not diagnosed");
        faultRuntime.Update(faults,0.25f);Require(faults.Find(broken)->transform.position.x==0 && faults.Find(healthy)->transform.position.x==0.5f && faultRuntime.GetLastError(broken)==diagnostic,"Faulted instance retried or blocked peer");
        Write(path,"function on_update(e,dt) e:translate(2*dt,0,0) end");Require(faultRuntime.LoadResolvedAsset("broken",path,error),error.c_str());
        faultRuntime.Update(faults,0.25f);Require(faults.Find(broken)->transform.position.x==0.5f && faultRuntime.GetLastError(broken).empty() && faultRuntime.GetRunningCount()==2,"Valid reload did not recover faulted Lua instance");
        Write(path,"function on_update(e,dt) e:translate(7,0,0); local huge=string.rep('x',16*1024*1024) end");Require(faultRuntime.LoadResolvedAsset("broken",path,error),error.c_str());
        faultRuntime.Update(faults,0.25f);Require(faults.Find(broken)->transform.position.x==0.5f && faultRuntime.GetLastError(broken).find("memory")!=std::string::npos,"Lua memory budget was not atomic/recoverable");
        Write(path,"function on_update(e,dt) e:translate(7,0,0); e:set_position(math.huge,0,0) end");Require(faultRuntime.LoadResolvedAsset("broken",path,error),error.c_str());
        faultRuntime.Update(faults,0.25f);Require(faults.Find(broken)->transform.position.x==0.5f && faultRuntime.GetLastError(broken).find("finite")!=std::string::npos,"Nonfinite Lua value crossed host boundary");
        Write(path,"function on_update(e,dt) end; function on_destroy(e) e:translate(9,0,0);error('destroy failed') end");Require(faultRuntime.LoadResolvedAsset("broken",path,error),error.c_str());
        faultRuntime.Update(faults,0.25f);faultRuntime.Shutdown(faults);
        Require(faults.Find(broken)->transform.position.x==0.5f && !faults.Find(broken)->script.started && faultRuntime.GetLastError(broken).find("destroy failed")!=std::string::npos,"Destruction failure mutated host/escaped shutdown");

        Engine::Assets::AssetRegistry registry;const auto guidPath=fixture/L"Assets/参照.lua";
        Write(guidPath,"function on_update(e,dt) e:translate(3*dt,0,0) end");Require(registry.Scan(fixture/"Assets",error),error.c_str());
        const auto* registered=registry.FindByPath(guidPath);Require(registered!=nullptr,"Lua GUID fixture was not registered");
        const auto guid=registered->guid;
        EntityWorld referenced;const auto id=referenced.CreateEntity("GUID Lua");referenced.SetScript(id,"Assets/old.lua",true,guid);ScriptRuntime guidRuntime;
        Require(LoadScriptWorldAssets(guidRuntime,referenced,fixture,registry,error),error.c_str());guidRuntime.Update(referenced,0.5f);
        Require(referenced.Find(id)->transform.position.x==1.5f,"GUID resolved Lua did not execute");
        guidRuntime.Shutdown(referenced);
        // An automatic load must not execute an unchanged failed module each frame.
        // Preserve size/time deliberately to prove the cached path; explicit retry wins.
        const auto retryPath=fixture/"Assets/retry.lua";
        const std::string invalidModule="error('first load')";
        std::string fixedModule="function on_update(e,dt) end";
        std::string paddedInvalid=invalidModule+std::string(fixedModule.size()-invalidModule.size(),' ');
        Write(retryPath,paddedInvalid);
        EntityWorld retryWorld;const auto retryId=retryWorld.CreateEntity("Retry");retryWorld.SetScript(retryId,"Assets/retry.lua");
        ScriptRuntime retries;
        Require(!LoadScriptWorldAssets(retries,retryWorld,fixture,registry,error),"Failed startup module accepted");
        const auto firstLoadError=error;const auto savedTime=std::filesystem::last_write_time(retryPath);
        Write(retryPath,fixedModule);std::filesystem::last_write_time(retryPath,savedTime);
        Require(!LoadScriptWorldAssets(retries,retryWorld,fixture,registry,error) && error==firstLoadError && !retries.Contains("Assets/retry.lua"),"Unchanged failure metadata reparsed module");
        Require(LoadScriptReference(retries,retryWorld.Find(retryId)->script,fixture,registry,error,true),"Explicit retry was suppressed");
        retries.Unregister("Assets/retry.lua");Write(retryPath,paddedInvalid);
        Require(!LoadScriptWorldAssets(retries,retryWorld,fixture,registry,error),"Second startup failure accepted");
        Write(retryPath,fixedModule+" ");
        Require(LoadScriptWorldAssets(retries,retryWorld,fixture,registry,error),"Changed file size did not recover failed load");
        retries.Unregister("Assets/retry.lua");Write(retryPath,paddedInvalid);
        Require(!LoadScriptWorldAssets(retries,retryWorld,fixture,registry,error),"Third startup failure accepted");
        const auto oldTime=std::filesystem::last_write_time(retryPath);
        Write(retryPath,fixedModule);std::filesystem::last_write_time(retryPath,oldTime+std::chrono::seconds(2));
        Require(LoadScriptWorldAssets(retries,retryWorld,fixture,registry,error),"Changed modification time did not recover failed load");
        retries.Unregister("Assets/retry.lua");Write(retryPath,paddedInvalid);
        Require(!LoadScriptWorldAssets(retries,retryWorld,fixture,registry,error),"Fourth startup failure accepted");
        const auto clearTime=std::filesystem::last_write_time(retryPath);Write(retryPath,fixedModule);std::filesystem::last_write_time(retryPath,clearTime);
        retries.Clear();Require(LoadScriptWorldAssets(retries,retryWorld,fixture,registry,error),"Clear retained failure cache");
        const auto inputPath=fixture/"Assets/input.lua";
        Write(inputPath,R"(
local pressed=input.key_pressed
function on_update(e,dt)
  local x,y,z=input.action_axis("移動")
  assert(not input.action_down("unknown") and not input.action_pressed("unknown") and not input.action_released("unknown"))
  local mx,my=input.mouse_position();local dx,dy=input.mouse_delta()
  local score=0
  if input.available() then score=score+1 end
  if input.key_down(input.keys.W) then score=score+2 end
  if pressed(input.keys.W) then score=score+4 end
  if input.key_released(input.keys.W) then score=score+8 end
  if input.mouse_down(0) then score=score+16 end
  if input.mouse_pressed(0) then score=score+32 end
  if input.mouse_released(0) then score=score+64 end
  if input.action_down("移動") then score=score+128 end
  if input.action_pressed("移動") then score=score+256 end
  if input.action_released("移動") then score=score+512 end
  e:translate(x*dt,y*dt,z*dt)
  e:set_rotation(score,mx+my,dx+dy+input.mouse_wheel())
end
)");
        ScriptRuntime inputRuntime;Require(inputRuntime.LoadResolvedAsset("input",inputPath,error),error.c_str());
        EntityWorld inputWorld;const auto inputFirst=inputWorld.CreateEntity("Input first"),inputSecond=inputWorld.CreateEntity("Input second");
        inputWorld.SetScript(inputFirst,"input");inputWorld.SetScript(inputSecond,"input");
        Engine::InputSystem::InputFrame frame;frame.available=true;
        frame.keys[17]={true,true,false};frame.mouse[0]={true,true,false};frame.actions["移動"]={{true,true,false},{2,0,0}};
        frame.mouseX=10;frame.mouseY=20;frame.deltaX=2;frame.deltaY=-1;frame.wheel=0.5f;
        inputRuntime.SetInputFrame(frame);frame.actions.clear();inputRuntime.Update(inputWorld,0.5f);
        for(const auto inputId:{inputFirst,inputSecond}) Require(inputWorld.Find(inputId)->transform.position.x==1 && inputWorld.Find(inputId)->transform.rotation.x==439 && inputWorld.Find(inputId)->transform.rotation.y==30 && inputWorld.Find(inputId)->transform.rotation.z==1.5f,"Lua input snapshot/edge values differ between entities");
        frame.available=true;frame.keys[17]={false,false,true};frame.mouse[0]={false,false,true};frame.actions["移動"]={{false,false,true},{0,0,0}};
        inputRuntime.SetInputFrame(frame);inputRuntime.Update(inputWorld,0.5f);
        Require(inputWorld.Find(inputFirst)->transform.rotation.x==585,"Lua released input values incorrect");
        frame.available=false;frame.keys[17]={true,true,true};frame.mouse[0]={true,true,true};frame.actions["移動"]={{true,true,true},{100,0,0}};
        inputRuntime.SetInputFrame(frame);inputRuntime.Update(inputWorld,0.5f);
        Require(inputWorld.Find(inputFirst)->transform.rotation.x==0 && inputWorld.Find(inputFirst)->transform.rotation.y==0 && inputWorld.Find(inputFirst)->transform.position.x==1,"Unavailable frame leaked input");
        Write(inputPath,"local value=input.available(); function on_update(e,dt) end");
        Require(!inputRuntime.LoadResolvedAsset("input",inputPath,error) && error.find("hook")!=std::string::npos,"Module initialization accessed live input");
        for(const auto& invalid: {"input.key_down(-1)","input.key_down(256)","input.key_down(1.5)","input.mouse_down(8)","input.action_axis({})","input.action_down('')"}) {
            Write(inputPath,std::string("function on_update(e,dt) e:translate(9,0,0); ")+invalid+" end");
            Require(inputRuntime.LoadResolvedAsset("input",inputPath,error),error.c_str());inputRuntime.Update(inputWorld,0.5f);
            Require(inputRuntime.GetRunningCount()==0 && !inputRuntime.GetLastError(inputFirst).empty() && inputWorld.Find(inputFirst)->transform.position.x==1,"Invalid Lua input escaped validation or committed host mutation");
        }
        inputRuntime.Shutdown(inputWorld);
        const auto propertyPath=fixture/"Assets/properties.lua";
        const std::string propertySource=R"(
properties={speed=2,enabled=true,label="日本語"}
function on_create(e) e:translate(e:property("speed"),0,0) end
function on_update(e,dt)
  assert(e:property("unknown")==nil)
  local label=e:property("label");assert(label=="日本語" or label=="別名")
  if e:property("enabled") then e:translate(e:property("speed")*dt,0,0) end
end
)";
        Write(propertyPath,propertySource);ScriptRuntime propertyRuntime;
        Require(propertyRuntime.LoadResolvedAsset("properties",propertyPath,error),error.c_str());
        auto defaults=propertyRuntime.GetPropertyDefaults("properties");
        Require(defaults.size()==3 && std::get<double>(defaults.at("speed"))==2 && std::get<bool>(defaults.at("enabled")) && std::get<std::string>(defaults.at("label"))=="日本語","Lua default schema extraction failed");
        defaults["speed"]=999.0;Require(std::get<double>(propertyRuntime.GetPropertyDefaults("properties").at("speed"))==2,"Inspector schema copy mutated runtime defaults");
        EntityWorld propertyWorld;const auto propertyFirst=propertyWorld.CreateEntity("Property first"),propertySecond=propertyWorld.CreateEntity("Property second");
        propertyWorld.SetScript(propertyFirst,"properties");propertyWorld.SetScript(propertySecond,"properties");
        Require(propertyWorld.SetScriptProperty(propertySecond,"speed",4.0) && propertyWorld.SetScriptProperty(propertySecond,"enabled",false) && propertyWorld.SetScriptProperty(propertySecond,"label",std::string("別名")),"Cannot author per-entity properties");
        const auto authored=propertyWorld.Capture();EntityWorld propertyLoaded;
        EntityId propertyClone=0;Require(propertyWorld.DuplicateSubtree(propertySecond,0,propertyClone,error) && propertyWorld.Find(propertyClone)->script.properties==propertyWorld.Find(propertySecond)->script.properties,"Duplication lost script overrides");
        Require(propertyWorld.Restore(authored,error),error.c_str());
        Require(propertyWorld.Save(fixture/"properties.scene",error) && propertyLoaded.Load(fixture/"properties.scene",error) && propertyLoaded.Capture().dump()==authored.dump(),"Properties failed Scene save/load");
        Engine::Editor::EntityHistory propertyHistory;Require(propertyWorld.SetScriptProperty(propertyFirst,"speed",3.0),"Cannot edit property");
        Require(propertyHistory.Record(authored,propertyWorld,propertyFirst,propertyFirst,error),error.c_str());propertyHistory.Commit();EntityId propertySelection=propertyFirst;
        Require(propertyHistory.Undo(propertyWorld,propertySelection,error) && propertyWorld.Find(propertyFirst)->script.properties.empty(),"Undo retained override");
        Require(propertyHistory.Redo(propertyWorld,propertySelection,error) && std::get<double>(propertyWorld.Find(propertyFirst)->script.properties.at("speed"))==3,"Redo lost override");
        Require(propertyWorld.RemoveScriptProperty(propertyFirst,"speed"),"Reset failed");
        Engine::Scene::WorldPlayback propertyPlayback;Engine::Physics::PhysicsWorld propertyPhysics;
        Require(propertyPlayback.Play(propertyWorld),"Property Play failed");propertyRuntime.Update(propertyWorld,0.5f);
        Require(propertyWorld.Find(propertyFirst)->transform.position.x==3 && propertyWorld.Find(propertySecond)->transform.position.x==4,"Default/override values leaked between entities");
        Require(propertyPlayback.Stop(propertyWorld,propertyRuntime,propertyPhysics,error) && propertyWorld.Capture().dump()==authored.dump(),"Stop lost authored property snapshot");
        for(const auto& declaration:{"properties=1","properties={speed=math.huge}","properties={speed={}}","properties={[1]=2}","properties={label=string.rep('x',1025)}","properties={label=string.char(255)}"}) {
            Write(propertyPath,std::string(declaration)+"; function on_update(e,dt) end");
            Require(!propertyRuntime.LoadResolvedAsset("properties",propertyPath,error) && std::get<double>(propertyRuntime.GetPropertyDefaults("properties").at("speed"))==2,"Invalid declaration replaced valid schema");
        }
        Write(propertyPath,"properties={}; for i=1,65 do properties['p'..i]=i end; function on_update(e,dt) end");
        Require(!propertyRuntime.LoadResolvedAsset("properties",propertyPath,error),"Too many Lua property declarations accepted");
        auto badProperties=authored;badProperties["entities"][0]["script"]["properties"]=crude_json::array{1.0};
        Require(!propertyLoaded.Restore(badProperties,error) && propertyLoaded.Capture().dump()==authored.dump(),"Malformed properties restore was not atomic");
        badProperties=authored;badProperties["entities"][0]["script"]["properties"]["speed"]=1e99;
        Require(!propertyLoaded.Restore(badProperties,error) && propertyLoaded.Capture().dump()==authored.dump(),"Oversized property accepted");
        Require(!propertyWorld.SetScriptProperty(propertyFirst,"bad",std::string(1025,'x')) && !propertyWorld.SetScriptProperty(propertyFirst,"",1.0),"Property setter crossed validation bounds");
        EntityWorld propertyBounds;const auto boundedId=propertyBounds.CreateEntity("Bounded");propertyBounds.SetScript(boundedId,"properties");
        for(int index=0;index<64;++index) Require(propertyBounds.SetScriptProperty(boundedId,"p"+std::to_string(index),double(index)),"Valid property count rejected");
        Require(!propertyBounds.SetScriptProperty(boundedId,"extra",1.0) && propertyBounds.SetScriptProperty(boundedId,"p0",2.0),"Property count bound or replacement incorrect");
        Require(propertyWorld.SetScriptProperty(propertyFirst,"speed",true),"Schema-independent authored value rejected");
        const auto beforeFault=propertyWorld.Find(propertyFirst)->transform.position.x;propertyRuntime.Update(propertyWorld,0.5f);
        Require(!propertyRuntime.GetLastError(propertyFirst).empty() && propertyWorld.Find(propertyFirst)->transform.position.x==beforeFault && propertyRuntime.GetRunningCount()==1,"Override type mismatch escaped isolation");
        propertyRuntime.Shutdown(propertyWorld);
        std::error_code cleanup;std::filesystem::remove_all(fixture,cleanup);
        std::cout<<"PASS: real Lua VM, per-entity closures, bindings/UTF-8/BOM, lifecycle/reload, syntax/bytecode/quota/fault isolation, transactional host values and GUID loading\n";return 0;
    } catch(const std::exception& exception) { std::error_code cleanup;std::filesystem::remove_all(fixture,cleanup);std::cerr<<"FAIL: "<<exception.what()<<'\n';return 1; }
}
