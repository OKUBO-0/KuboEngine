#include "engine/script/ScriptRuntime.h"
#include <array>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>

namespace {
bool refuseAllocations=false;
unsigned allocationAttempts=0;
void Require(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
struct Fault { Engine::Scene::EntityId id; };
struct Fixture {
    Engine::Scene::EntityWorld world;
    Engine::Scripting::ScriptRuntime runtime;
    std::array<unsigned,32> stopped{};
    unsigned updates=0, first=32;
    void Start(std::function<void(Engine::Scene::EntityRecord&)> destroy,bool guid=false) {
        Engine::Scripting::ScriptCallbacks hooks;
        hooks.onUpdate=[this](auto&,float){++updates;};
        hooks.onDestroy=std::move(destroy);
        const char* key=guid ? "guid:00112233445566778899aabbccddeeff" : "owned-shutdown-with-long-registration-key";
        Require(runtime.Register(key,std::move(hooks)),"Register shutdown fixture");
        for(unsigned index=0;index!=stopped.size();++index){
            const auto id=world.CreateEntity("Owned script");
            Require(id==index+1,"Unexpected fixture ID");
            Require(world.SetScript(id,"owned-shutdown-with-long-registration-key",true,guid ? "00112233445566778899aabbccddeeff" : ""),"Attach fixture script");
        }
        runtime.Update(world,0.1f);
        Require(runtime.GetRunningCount()==stopped.size(),"Fixture instances did not start");
    }
    void Stop(Engine::Scene::EntityRecord& entity) {
        const auto index=static_cast<unsigned>(entity.id-1);
        if(first==32)first=index;
        ++stopped.at(index);
    }
    void CheckRetired() {
        Require(runtime.GetRunningCount()==0,"Shutdown retained live instances");
        for(unsigned index=0;index!=stopped.size();++index){
            Require(stopped[index]==1,"Destroy was skipped or repeated");
            Require(!world.Find(index+1)->script.started,"Stopped Entity still marked started");
        }
        runtime.Shutdown(world);
        for(auto count:stopped)Require(count==1,"Repeated shutdown invoked Destroy");
    }
};
}
void* operator new(std::size_t bytes) {
    if(refuseAllocations){++allocationAttempts;throw std::bad_alloc();}
    if(auto* result=std::malloc(bytes ? bytes : 1))return result;
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes){return ::operator new(bytes);}
void operator delete(void* value) noexcept {std::free(value);}
void operator delete[](void* value) noexcept {std::free(value);}
void operator delete(void* value,std::size_t) noexcept {std::free(value);}
void operator delete[](void* value,std::size_t) noexcept {std::free(value);}

int main(){try {
    for(bool guid:{false,true}) {
        Fixture fixture;fixture.Start([&](auto& entity){fixture.Stop(entity);},guid);
        allocationAttempts=0;refuseAllocations=true;bool failed=false;
        try{fixture.runtime.Shutdown(fixture.world);}catch(...){failed=true;}
        refuseAllocations=false;
        const auto remaining=fixture.runtime.GetRunningCount();
        std::cout<<"SCRIPT_SHUTDOWN allocation_attempts="<<allocationAttempts<<" failed="<<failed<<" remaining="<<remaining<<'\n';
        Require(!failed && !allocationAttempts,"Shutdown allocates before retiring instances");
        fixture.CheckRetired();
    }
    for(bool allocationFault:{false,true}) {
        Fixture fixture;fixture.Start([&](auto& entity){fixture.Stop(entity);if(allocationFault)throw std::bad_alloc();throw Fault{entity.id};});
        bool original=false;
        try{fixture.runtime.Shutdown(fixture.world);}
        catch(const Fault& fault){original=!allocationFault && fault.id==fixture.first+1;}
        catch(const std::bad_alloc&){original=allocationFault;}
        Require(original,"Shutdown replaced the first callback exception");
        fixture.CheckRetired();
    }
    {
        const Engine::Scripting::LuaScriptError fault("Owned long Lua teardown fault whose diagnostic requires storage");
        Fixture fixture;fixture.Start([&](auto& entity){fixture.Stop(entity);throw fault;});
        allocationAttempts=0;refuseAllocations=true;bool diagnosticFailed=false;
        try{fixture.runtime.Shutdown(fixture.world);}catch(const std::bad_alloc&){diagnosticFailed=true;}
        refuseAllocations=false;
        Require(diagnosticFailed && allocationAttempts==32,"Diagnostic allocation fault did not reach each retired hook");
        fixture.CheckRetired();
    }
    {
        Fixture fixture;fixture.Start([&](auto& entity){
            fixture.Stop(entity);fixture.runtime.Shutdown(fixture.world);
            fixture.runtime.Update(fixture.world,0.1f);fixture.runtime.FixedUpdate(fixture.world,0.1f);
        });
        const auto updates=fixture.updates;
        fixture.runtime.Shutdown(fixture.world);
        fixture.CheckRetired();Require(updates==fixture.updates,"Destroy callback restarted scripts during shutdown");
        fixture.runtime.Update(fixture.world,0.1f);
        Require(fixture.runtime.GetRunningCount()==32 && fixture.updates==updates+32,"Shutdown guard was not released for next session");
        fixture.runtime.Shutdown(fixture.world);
        for(auto count:fixture.stopped)Require(count==2,"Second session did not retire exactly once");
    }
    {
        Fixture fixture;bool refused=false;
        Engine::Scripting::ScriptCallbacks hooks;
        hooks.onUpdate=[&](auto&,float){Require(fixture.runtime.IsDispatchActive(),"Dispatch admission not published");try{fixture.runtime.Shutdown(fixture.world);}catch(const std::logic_error&){refused=true;}};
        hooks.onDestroy=[&](auto& entity){fixture.Stop(entity);};
        Require(fixture.runtime.Register("owned-shutdown",std::move(hooks)),"Register dispatch control");
        auto id=fixture.world.CreateEntity("Dispatch");fixture.world.SetScript(id,"owned-shutdown");
        fixture.runtime.Update(fixture.world,0.1f);
        Require(refused && !fixture.runtime.IsDispatchActive() && fixture.runtime.GetRunningCount()==1 && fixture.world.Find(id)->script.started,"Shutdown retired an executing dispatch instance or retained admission");
        fixture.runtime.Shutdown(fixture.world);Require(fixture.stopped[0]==1,"Dispatch refusal prevented later shutdown");
    }
    std::cout<<"PASS: Script shutdown allocation-free core, first failure/all retirement, reentry, reuse and dispatch refusal\n";
    return 0;
}catch(const std::exception& error){refuseAllocations=false;std::cerr<<error.what()<<'\n';return 1;}}
