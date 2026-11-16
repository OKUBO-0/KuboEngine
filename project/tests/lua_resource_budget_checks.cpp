#include "engine/script/ScriptRuntime.h"
#include "engine/script/LuaScript.h"
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void Write(const std::filesystem::path& path,const char* source){std::ofstream file(path,std::ios::binary);file<<source;file.close();Require(!file.fail(),"Fixture write failed");}
}
int main(){
    using namespace Engine::Scripting;using namespace Engine::Scene;
    const auto fixture=std::filesystem::temp_directory_path()/(L"kubo-lua-budget-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try{
        LuaResourceBudget arithmetic({10,1});Require(arithmetic.AcquireState() && !arithmetic.AcquireState(),"State cap failed");arithmetic.ReleaseState();
        Require(arithmetic.Resize(0,10) && !arithmetic.Resize(0,1) && arithmetic.Usage().memoryBytes==10,"Memory cap changed on refusal");
        Require(!arithmetic.Resize(11,0) && !arithmetic.Resize(0,(std::numeric_limits<size_t>::max)()) && arithmetic.Resize(10,0),"Overflow/underflow accounting failed");
        bool invalid=false;try{ScriptRuntime rejected(LuaResourceLimits{0,1});}catch(const std::invalid_argument&){invalid=true;}Require(invalid,"Zero limit accepted");
        std::filesystem::create_directories(fixture);const auto path=fixture/L"動作.lua";std::string error;
        ScriptRuntime runtime(LuaResourceLimits{1024*1024,2});
        Require(!runtime.ConfigureLuaResourceLimits({1,0,1},error) && runtime.GetLuaResourceLimits().memoryBytes==1024*1024 && runtime.GetLuaResourceLimits().states==2,"Invalid configuration replaced empty runtime limits");
        Require(runtime.ConfigureLuaResourceLimits({1024*1024,2,2000000},error),error.c_str());
        Write(path,"function on_update(e,dt) e:translate(dt,0,0) end");Require(runtime.LoadResolvedAsset("mover",path,error),error.c_str());
        Require(runtime.GetLuaResourceUsage().states==1 && runtime.GetLuaResourceUsage().memoryBytes>0,"Factory seed not counted");
        Require(!runtime.ConfigureLuaResourceLimits({1,1,1},error) && runtime.GetLuaResourceLimits().states==2 && runtime.GetLuaResourceUsage().states==1,"Configuration revoked registered seed ownership");
        const auto seed=runtime.GetLuaResourceUsage();Write(path,"function on_update(");Require(!runtime.LoadResolvedAsset("invalid",path,error) && runtime.GetLuaResourceUsage().states==seed.states && runtime.GetLuaResourceUsage().memoryBytes==seed.memoryBytes,"Failed validation leaked quota");
        EntityWorld world;const auto first=world.CreateEntity("First"),second=world.CreateEntity("Second"),third=world.CreateEntity("Refused"),native=world.CreateEntity("Native");
        for(const auto id:{first,second,third})world.SetScript(id,"mover");world.SetScript(native,"native");
        ScriptCallbacks callbacks;callbacks.onUpdate=[](EntityRecord& e,float dt){e.transform.position.x+=dt;};Require(runtime.Register("native",callbacks),"Native registration failed");
        runtime.Update(world,0.5f);
        Require(runtime.GetLuaResourceUsage().states==2 && runtime.GetRunningCount()==3 && world.Find(first)->transform.position.x==0.5f && world.Find(second)->transform.position.x==0.5f && world.Find(third)->transform.position.x==0 && world.Find(native)->transform.position.x==0.5f && runtime.GetLastError(third).find("VM budget")!=std::string::npos,"VM cap/fault isolation failed");
        Write(path,"function on_update(e,dt) e:translate(10,0,0) end");const auto full=runtime.GetLuaResourceUsage();
        Require(!runtime.LoadResolvedAsset("mover",path,error) && error.find("VM budget")!=std::string::npos && runtime.GetLuaResourceUsage().states==full.states && runtime.GetLuaResourceUsage().memoryBytes==full.memoryBytes,"Rejected reload replaced old factory/leaked quota");
        runtime.Update(world,0.5f);Require(world.Find(first)->transform.position.x==1 && world.Find(third)->transform.position.x==0,"Refused VM retried or rejected reload lost valid registration");
        world.Find(first)->script.enabled=false;world.Find(third)->script.started=false;runtime.Update(world,0.5f);
        Require(world.Find(third)->transform.position.x==0.5f && runtime.GetLastError(third).empty() && runtime.GetLuaResourceUsage().states==2,"Released state slot not reusable");
        runtime.Clear();Require(runtime.GetLuaResourceUsage().states==2,"Clear forgot retained callback states");runtime.Shutdown(world);
        Require(runtime.GetLuaResourceUsage().states==0 && runtime.GetLuaResourceUsage().memoryBytes==0,"Shutdown leaked Lua quota");
        Require(runtime.ConfigureLuaResourceLimits({2*1024*1024,16,1000},error) && runtime.GetLuaResourceLimits().states==16 && runtime.GetLuaResourceLimits().frameInstructions==1000,"Empty runtime could not adopt next-launch limits");

        ScriptRuntime memory(LuaResourceLimits{700*1024,8});EntityWorld crowded;
        const auto a=crowded.CreateEntity("Memory owner"),b=crowded.CreateEntity("Over budget"),c=crowded.CreateEntity("Healthy Lua"),d=crowded.CreateEntity("Healthy native");
        crowded.SetScript(a,"large");crowded.SetScript(b,"large");crowded.SetScript(c,"healthy");crowded.SetScript(d,"native");memory.Register("native",callbacks);
        Write(path,"function on_update(e,dt) e:translate(7,0,0); retained=string.rep('x',256*1024) end");Require(memory.LoadResolvedAsset("large",path,error),error.c_str());
        Write(path,"function on_update(e,dt) e:translate(dt,0,0) end");Require(memory.LoadResolvedAsset("healthy",path,error),error.c_str());memory.Update(crowded,0.5f);
        Require(crowded.Find(a)->transform.position.x==7 && crowded.Find(b)->transform.position.x==0 && crowded.Find(c)->transform.position.x==0.5f && crowded.Find(d)->transform.position.x==0.5f && memory.GetLastError(b).find("memory")!=std::string::npos && memory.GetLuaResourceUsage().memoryBytes<=700*1024,"Aggregate memory refusal leaked hook mutation or stopped healthy peer");
        memory.Shutdown(crowded);memory.Clear();Require(memory.GetLuaResourceUsage().memoryBytes==0 && memory.GetLuaResourceUsage().states==0,"Memory fault cleanup leaked quota");
        Write(path,"function on_update(e,dt) end");
        for(const auto bytes:{size_t{1},size_t{4096},size_t{8192}}){ScriptRuntime tiny(LuaResourceLimits{bytes,1});Require(!tiny.LoadResolvedAsset("tiny",path,error) && tiny.GetLuaResourceUsage().states==0 && tiny.GetLuaResourceUsage().memoryBytes==0,"Failed/partial Lua initialization leaked quota");}
        ScriptRuntime independent;Require(independent.LoadResolvedAsset("independent",path,error) && independent.GetLuaResourceUsage().states==1 && runtime.GetLuaResourceUsage().states==0,"Different runtimes share quota");independent.Clear();Require(independent.GetLuaResourceUsage().states==0 && independent.GetLuaResourceUsage().memoryBytes==0,"Unconsumed seed leaked quota");
        // Long comments need virtually no Lua VM memory, but the native factory
        // retains every source payload. Reserve it before validation/registration.
        const std::string padded="--"+std::string(512*1024,'x')+"\nfunction on_update(e,dt) e:translate(dt,0,0) end";
        {std::ofstream out(path,std::ios::binary);out<<padded;Require(bool(out),"Padded Lua write failed");}
        ScriptRuntime sources({800*1024,8});Require(sources.LoadResolvedAsset("kept",path,error),error.c_str());
        const auto retained=sources.GetLuaResourceUsage();Require(retained.memoryBytes>=padded.size() && retained.memoryBytes<=800*1024,"Retained source omitted from shared memory usage");
        Require(!sources.LoadResolvedAsset("extra",path,error) && error.find("retained source memory budget")!=std::string::npos && !sources.Contains("extra") && sources.GetLuaResourceUsage().memoryBytes==retained.memoryBytes && sources.GetLuaResourceUsage().states==retained.states,"Aggregate source refusal leaked reservation/registered script");
        Require(!sources.LoadResolvedAsset("kept",path,error) && sources.Contains("kept") && sources.GetLuaResourceUsage().memoryBytes==retained.memoryBytes,"Source-limited reload lost previous registration");
        EntityWorld sourceWorld;const auto mover=sourceWorld.CreateEntity("Retained source mover");sourceWorld.SetScript(mover,"kept");sources.Update(sourceWorld,0.5f);Require(sourceWorld.Find(mover)->transform.position.x==0.5f,"Refused source reload stopped old script");
        sources.Unregister("kept");Require(sources.GetLuaResourceUsage().memoryBytes<padded.size() && sources.GetLuaResourceUsage().states==1,"Factory removal retained unused source or revoked live callbacks");
        sources.Shutdown(sourceWorld);sources.Clear();Require(sources.GetLuaResourceUsage().memoryBytes==0 && sources.GetLuaResourceUsage().states==0,"Source reservation cleanup leaked");
        auto sourceBudget=std::make_shared<LuaResourceBudget>(LuaResourceLimits{800*1024,8});ScriptProperties defaults;
        auto factory=CreateLuaScriptFactory(padded,"shared",{},defaults,{},sourceBudget);const auto charged=sourceBudget->Usage();
        auto factoryCopy=factory;Require(sourceBudget->Usage().memoryBytes==charged.memoryBytes,"Factory copy charged source twice");factory={};Require(sourceBudget->Usage().memoryBytes==charged.memoryBytes,"First factory release revoked shared source");factoryCopy={};Require(sourceBudget->Usage().memoryBytes==0 && sourceBudget->Usage().states==0,"Last factory release leaked source/seed");
        bool oversized=false;try{CreateLuaScriptFactory(std::string(1024*1024+1,' '),"oversized",{},defaults,{},sourceBudget);}catch(const LuaScriptError&){oversized=true;}Require(oversized && sourceBudget->Usage().memoryBytes==0 && sourceBudget->Usage().states==0,"Direct factory bypassed source cap/leaked quota");
        std::error_code cleanup;std::filesystem::remove_all(fixture,cleanup);
        std::cout<<"PASS: runtime aggregate Lua memory/VM limits, seed/retained accounting, overflow/refusal/reload isolation, slot reuse, native peer progress and constructor/fault/shutdown cleanup\n";return 0;
    }catch(const std::exception& exception){std::error_code cleanup;std::filesystem::remove_all(fixture,cleanup);std::cerr<<"FAIL: "<<exception.what()<<'\n';return 1;}
}
