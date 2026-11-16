#include "engine/script/ScriptRuntime.h"
#include "engine/physics/PhysicsWorld.h"
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cmath>
#include <stdexcept>

void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void Write(const std::filesystem::path& path,const std::string& text){std::ofstream out(path,std::ios::binary);out<<text;out.close();Require(!out.fail(),"Fixed script fixture write failed");}
int main(){
    using namespace Engine::Scene;using namespace Engine::Scripting;using namespace Engine::Physics;
    const auto fixture=std::filesystem::temp_directory_path()/(L"kubo-fixed-script-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try{
        std::filesystem::create_directories(fixture);std::string error;
        Write(fixture/"fixed.lua","function on_create(e) e:translate(0,7,0) end\nfunction on_fixed_update(e,dt) e:translate(dt,0,0) end\nfunction on_destroy(e) e:translate(0,1,0) end\n");
        EntityWorld seed;const auto id=seed.CreateEntity("Fixed only");seed.SetScript(id,"fixed");
        auto a=seed,b=seed,c=seed;ScriptRuntime ra,rb,rc;PhysicsWorld pa,pb,pc;
        for(auto* runtime:{&ra,&rb,&rc})Require(runtime->LoadResolvedAsset("fixed",fixture/"fixed.lua",error),error.c_str());
        const auto run=[](ScriptRuntime& runtime,PhysicsWorld& physics,EntityWorld& world,int count,float dt){for(int n=0;n<count;++n){auto frame=runtime.BeginFrame();runtime.Update(world,dt);physics.Advance(world,dt,[&](float tick){runtime.FixedUpdate(world,tick);});}};
        run(ra,pa,a,60,1.0f/60);run(rb,pb,b,240,1.0f/240);run(rc,pc,c,10,0.1f);
        Require(a.Capture().dump()==b.Capture().dump() && a.Capture().dump()==c.Capture().dump() && std::abs(a.Find(id)->transform.position.x-1)<1e-5 && a.Find(id)->transform.position.y==7,"Fixed-only Lua hooks vary with frame partition or repeated Create");
        ra.Shutdown(a);rb.Shutdown(b);rc.Shutdown(c);Require(a.Find(id)->transform.position.y==8 && ra.GetRunningCount()==0,"Fixed-only Destroy lifecycle failed");ra.Clear();rb.Clear();rc.Clear();
        Require(!ra.GetLuaResourceUsage().memoryBytes && !ra.GetLuaResourceUsage().states,"Fixed-only lifecycle leaked source/VM");
        int creates=0,destroys=0,ticks=0;EntityWorld nativeWorld;const auto nativeId=nativeWorld.CreateEntity("Native fixed");nativeWorld.SetScript(nativeId,"native");ScriptRuntime native;
        ScriptCallbacks callbacks;callbacks.onCreate=[&](auto&){++creates;};callbacks.onDestroy=[&](auto&){++destroys;};callbacks.onFixedUpdate=[&](auto& entity,float dt){++ticks;entity.transform.position.x+=dt;};
        Require(native.Register("native",callbacks),"Native fixed-only registration refused");native.Update(nativeWorld,0.2f);Require(creates==1 && ticks==0,"Variable dispatcher called native fixed hook");
        native.FixedUpdate(nativeWorld,0.02f);Require(ticks==1 && std::abs(nativeWorld.Find(nativeId)->transform.position.x-0.02f)<1e-6,"Native fixed dispatcher failed");native.FixedUpdate(nativeWorld,0);Require(ticks==1,"Zero fixed delta invoked hook");
        nativeWorld.Find(nativeId)->script.enabled=false;native.FixedUpdate(nativeWorld,0.02f);Require(destroys==1 && native.GetRunningCount()==0,"Fixed dispatcher failed disabled retirement");
        Write(fixture/"legacy.kubo","on_create translate 0 5 0\non_update translate 0 1 0\non_fixed_update translate 2 0 0\n");
        ScriptRuntime legacy;Require(legacy.LoadResolvedAsset("legacy",fixture/"legacy.kubo",error),error.c_str());EntityWorld legacyWorld;const auto legacyId=legacyWorld.CreateEntity("Legacy fixed");legacyWorld.SetScript(legacyId,"legacy");
        legacy.Update(legacyWorld,0.2f);legacy.FixedUpdate(legacyWorld,0.1f);Require(std::abs(legacyWorld.Find(legacyId)->transform.position.x-0.2f)<1e-6 && std::abs(legacyWorld.Find(legacyId)->transform.position.y-5.2f)<1e-6,"Legacy fixed operation mixed phases");legacy.Shutdown(legacyWorld);legacy.Clear();
        Write(fixture/"budget.lua","function on_update(e,dt) e:translate(1,0,0) end\nfunction on_fixed_update(e,dt) e:translate(999,0,0); audio.play('sound.wav',false,0); while true do end end\n");
        ScriptRuntime limited({1024*1024,8,100});limited.SetAudioRequestsEnabled(true);Require(limited.LoadResolvedAsset("budget",fixture/"budget.lua",error),error.c_str());
        EntityWorld limitedWorld;const auto fault=limitedWorld.CreateEntity("Fault"),peer=limitedWorld.CreateEntity("Native peer");limitedWorld.SetScript(fault,"budget");limitedWorld.SetScript(peer,"peer");int peerTicks=0;ScriptCallbacks peerCallbacks;peerCallbacks.onFixedUpdate=[&](auto&,float){++peerTicks;};limited.Register("peer",peerCallbacks);
        PhysicsWorld limitedPhysics;{auto frame=limited.BeginFrame();limited.Update(limitedWorld,0.2f);const auto spent=limited.GetLuaResourceUsage().instructions;Require(spent>0,"Variable Lua hook spent no shared budget");limitedPhysics.Advance(limitedWorld,0.2f,[&](float dt){limited.FixedUpdate(limitedWorld,dt);});}
        Require(limited.GetLuaResourceUsage().instructions==100 && limited.GetLuaResourceUsage().instructionExhausted && limited.GetLastError(fault).find("frame instruction")!=std::string::npos && limitedWorld.Find(fault)->transform.position.x==1 && limited.TakeAudioRequests().empty() && peerTicks==8,"Fixed hooks reset frame budget, leaked fault mutation/audio or stopped native peer");
        limited.Shutdown(limitedWorld);limited.Clear();Require(!limited.GetLuaResourceUsage().memoryBytes && !limited.GetLuaResourceUsage().states,"Fixed fault leaked resource budget");
        Write(fixture/"bad.lua","on_fixed_update=3");ScriptRuntime invalid;Require(!invalid.LoadResolvedAsset("bad",fixture/"bad.lua",error) && !invalid.GetLuaResourceUsage().states,"Invalid fixed hook accepted/leaked VM");
        Write(fixture/"sensor.lua",R"(
function on_trigger_enter(entity, other)
    assert(other == scene.find('Visitor'))
    assert(scene.translate(scene.find('Output'), 1, 0, 0))
end
function on_trigger_exit(entity, other)
    assert(other == scene.find('Visitor'))
    assert(scene.translate(scene.find('Output'), 0, 1, 0))
end
)");
        Write(fixture/"visitor.lua",R"(
function on_trigger_enter(entity, other)
    assert(other == scene.find('Sensor'))
    entity:translate(100, 0, 0)
    error('discard failed trigger hook')
end
)");
        EntityWorld sensorWorld;
        const auto sensor=sensorWorld.CreateEntity("Sensor"),visitor=sensorWorld.CreateEntity("Visitor"),
            output=sensorWorld.CreateEntity("Output");
        sensorWorld.SetCollider(sensor,{{1,1,1},true});
        sensorWorld.SetCollider(visitor,{{1,1,1},false});
        sensorWorld.SetScript(sensor,"sensor");
        sensorWorld.SetScript(visitor,"visitor");
        sensorWorld.Find(visitor)->transform.position={10,0,0};
        PhysicsWorld sensorPhysics;
        ScriptRuntime sensorScripts;
        Require(sensorScripts.LoadResolvedAsset("sensor",fixture/"sensor.lua",error) &&
            sensorScripts.LoadResolvedAsset("visitor",fixture/"visitor.lua",error),error.c_str());
        const auto sensorFrame=[&](float dt) {
            auto frame=sensorScripts.BeginFrame();
            if (dt>0) sensorScripts.Update(sensorWorld,dt);
            sensorPhysics.Advance(sensorWorld,dt,[&](float tick){sensorScripts.FixedUpdate(sensorWorld,tick);},
                [&](std::span<const TriggerEvent> events){sensorScripts.DispatchTriggers(sensorWorld,events);});
        };
        sensorFrame(0.1f);
        Require(sensorWorld.Find(output)->transform.position.x==0,"Separated colliders fired trigger enter");
        sensorWorld.Find(visitor)->transform.position={0,0,0};
        sensorFrame(0.1f);
        Require(sensorWorld.Find(output)->transform.position.x==1 &&
            sensorScripts.GetLastError(visitor).find("discard failed trigger hook")!=std::string::npos &&
            sensorWorld.Find(visitor)->transform.position.x==0,
            "Trigger enter repeated or failed Lua trigger mutation was published");
        sensorWorld.Find(visitor)->transform.position={10,0,0};
        sensorFrame(0);
        Require(sensorWorld.Find(output)->transform.position.y==0,"Paused trigger tracking fired an exit");
        sensorFrame(0.1f);sensorFrame(0.1f);
        Require(sensorWorld.Find(output)->transform.position.y==1,"Trigger exit lost or repeated during catch-up");
        sensorWorld.Find(visitor)->transform.position={0,0,0};
        sensorFrame(0.1f);
        sensorPhysics.ResetSimulation();
        sensorFrame(0.1f);
        Require(sensorWorld.Find(output)->transform.position.x==3,"A new simulation retained stale trigger pairs");
        sensorScripts.Shutdown(sensorWorld);
        std::error_code cleanup;std::filesystem::remove_all(fixture,cleanup);std::cout<<"PASS: fixed-only Lua/native/legacy phases, partition equivalence, lifecycle, shared variable/fixed budget, fault/audio rollback and native peer progress\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<"; retained fixture "<<fixture<<'\n';return 1;}
}
