#include "engine/script/ScriptRuntime.h"
#include "engine/script/LuaScript.h"
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <limits>

namespace {
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void Write(const std::filesystem::path& path,const char* source){std::ofstream file(path,std::ios::binary);file<<source;file.close();Require(!file.fail(),"Fixture write failed");}
}
int main(){
    using namespace Engine::Scripting;using namespace Engine::Scene;
    const auto fixture=std::filesystem::temp_directory_path()/(L"kubo-lua-frame-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try{
        LuaResourceBudget arithmetic({1024,1,2});
        {LuaResourceBudget::FrameScope outer(arithmetic);Require(arithmetic.ChargeInstruction(),"Initial charge failed");
            {LuaResourceBudget::FrameScope nested(arithmetic);Require(arithmetic.Usage().instructions==1 && arithmetic.ChargeInstruction() && !arithmetic.ChargeInstruction(),"Nested scope reset/overflowed budget");}
            Require(arithmetic.FrameActive() && arithmetic.Usage().instructions==2,"Nested scope closed outer frame");}
        Require(!arithmetic.FrameActive(),"Frame scope leaked");
        {LuaResourceBudget::FrameScope reset(arithmetic);Require(arithmetic.Usage().instructions==0 && !arithmetic.Usage().instructionExhausted,"Frame counter did not reset");}
        bool invalid=false;try{ScriptRuntime rejected(LuaResourceLimits{1024,1,0});}catch(const std::invalid_argument&){invalid=true;}Require(invalid,"Zero frame limit accepted");
        std::filesystem::create_directories(fixture);const auto path=fixture/L"動作.lua";std::string error;
        ScriptRuntime runtime(LuaResourceLimits{4*1024*1024,32,5000});runtime.SetAudioRequestsEnabled(true);
        Write(path,"function on_update(e,dt) e:translate(7,0,0); audio.play('Assets/test.wav'); local n=0; for i=1,10000 do n=n+1 end end");Require(runtime.LoadResolvedAsset("heavy",path,error),error.c_str());
        Write(path,"function on_update(e,dt) e:translate(dt,0,0) end");Require(runtime.LoadResolvedAsset("healthy",path,error),error.c_str());
        EntityWorld world;const auto heavy=world.CreateEntity("Heavy"),first=world.CreateEntity("Deferred first"),native1=world.CreateEntity("Native first"),second=world.CreateEntity("Deferred second"),native2=world.CreateEntity("Native second");
        world.SetScript(heavy,"heavy");world.SetScript(first,"healthy");world.SetScript(second,"healthy");world.SetScript(native1,"native1");world.SetScript(native2,"native2");
        std::vector<int> nativeOrder;ScriptCallbacks callbacks;callbacks.onUpdate=[&](EntityRecord& e,float dt){nativeOrder.push_back(e.id==native1?1:2);e.transform.position.x+=dt;};runtime.Register("native1",callbacks);runtime.Register("native2",callbacks);
        runtime.Update(world,0.5f);
        Require(runtime.GetLuaResourceUsage().instructions==5000 && runtime.GetLuaResourceUsage().instructionExhausted && runtime.GetLuaResourceUsage().deferredEntities==2,"Frame cap/deferred count incorrect");
        Require(world.Find(heavy)->transform.position.x==0 && runtime.GetLastError(heavy).find("frame instruction")!=std::string::npos && runtime.TakeAudioRequests().empty(),"Interrupted hook published Entity/Audio changes");
        Require(!world.Find(first)->script.started && !world.Find(second)->script.started && runtime.GetLastError(first).empty() && runtime.GetLastError(second).empty(),"Unattempted Lua Entity was faulted/started");
        Require(nativeOrder==std::vector<int>{1,2} && world.Find(native1)->transform.position.x==0.5f && world.Find(native2)->transform.position.x==0.5f,"Lua quota blocked/reordered native callbacks");
        runtime.Update(world,0.5f);
        Require(world.Find(first)->transform.position.x==0.5f && world.Find(second)->transform.position.x==0.5f && runtime.GetLuaResourceUsage().instructions<5000 && runtime.GetLuaResourceUsage().deferredEntities==0 && nativeOrder==std::vector<int>{1,2,1,2},"Deferred Lua did not progress next frame or native order changed");
        const auto used=runtime.GetLuaResourceUsage().instructions;runtime.Update(world,(std::numeric_limits<float>::quiet_NaN)());Require(runtime.GetLuaResourceUsage().instructions==used,"Invalid dt reset frame statistics");
        runtime.Shutdown(world);runtime.Clear();Require(runtime.GetLuaResourceUsage().states==0 && runtime.GetLuaResourceUsage().memoryBytes==0,"Quota fault teardown leaked VM");

        ScriptRuntime create(LuaResourceLimits{4*1024*1024,8,150});create.SetAudioRequestsEnabled(true);EntityWorld created;
        Write(path,"function on_create(e) e:translate(7,0,0);audio.play('Assets/test.wav');local n=0;for i=1,10000 do n=n+1 end end;function on_update(e,dt) end");Require(create.LoadResolvedAsset("create",path,error),error.c_str());
        const auto creating=created.CreateEntity("Create quota");created.SetScript(creating,"create");create.Update(created,0.5f);
        Require(created.Find(creating)->transform.position.x==0 && create.TakeAudioRequests().empty() && create.GetLastError(creating).find("frame instruction")!=std::string::npos && create.GetLuaResourceUsage().instructions==150,"Create hook escaped aggregate transaction cap");create.Shutdown(created);create.Clear();
        Require(create.GetLuaResourceUsage().states==0 && create.GetLuaResourceUsage().memoryBytes==0,"Create fault leaked quota");

        ScriptRuntime cold(LuaResourceLimits{4*1024*1024,8,5000});EntityWorld clones;
        Write(path,"local n=0;for i=1,10000 do n=n+1 end;function on_update(e,dt) e:translate(dt,0,0) end");Require(cold.LoadResolvedAsset("cold",path,error),error.c_str());
        const auto seeded=clones.CreateEntity("Seed"),unseeded=clones.CreateEntity("Cold module");clones.SetScript(seeded,"cold");clones.SetScript(unseeded,"cold");cold.Update(clones,0.5f);
        Require(clones.Find(seeded)->transform.position.x==0.5f && clones.Find(unseeded)->transform.position.x==0 && cold.GetLastError(unseeded).find("frame instruction")!=std::string::npos && cold.GetLuaResourceUsage().instructions==5000 && cold.GetLuaResourceUsage().states==1,"Cold VM module execution escaped frame cap/leaked state");cold.Shutdown(clones);cold.Clear();
        Require(cold.GetLuaResourceUsage().states==0 && cold.GetLuaResourceUsage().memoryBytes==0,"Cold module failure leaked quota");

        // A successful Create can spend the last instruction and defer Update.
        // Destruction after Entity removal must see the successful Create record.
        const std::string retainedSource="function on_create(e) local n=0;for i=1,20 do n=n+1 end;e:translate(7,0,0) end;function on_update(e,dt) e:translate(2,0,0) end;function on_destroy(e) assert(e:position()==7,'lost create snapshot') end";
        auto probeBudget=std::make_shared<LuaResourceBudget>();ScriptProperties defaults;auto factory=CreateLuaScriptFactory(retainedSource,"probe",std::make_shared<Engine::InputSystem::InputFrame>(),defaults,{},probeBudget);auto probe=factory();EntityRecord probeRecord;probeRecord.id=1;
        {LuaResourceBudget::FrameScope count(*probeBudget);probe.onCreate(probeRecord);}
        const auto createInstructions=probeBudget->Usage().instructions;
        ScriptRuntime retained(LuaResourceLimits{4*1024*1024,8,createInstructions});EntityWorld removed;
        Write(path,retainedSource.c_str());Require(retained.LoadResolvedAsset("retained",path,error),error.c_str());const auto retainedId=removed.CreateEntity("Retained Create");removed.SetScript(retainedId,"retained");retained.Update(removed,0.5f);
        Require(removed.Find(retainedId)->transform.position.x==7 && retained.GetLuaResourceUsage().deferredEntities==1 && retained.GetLastError(retainedId).empty(),"Successful Create was lost or Update did not defer");
        Require(removed.DestroyEntity(retainedId),"Retained fixture deletion failed");retained.Shutdown(removed);
        Require(retained.GetLastError(retainedId).empty(),"Deferred Update left stale retained Create record for destroy");retained.Clear();Require(retained.GetLuaResourceUsage().states==0 && retained.GetLuaResourceUsage().memoryBytes==0,"Retained Create teardown leaked quota");

        // Hundreds of short calls cannot escape accounting by returning before
        // the old 500-opcode hook interval. Deferred slots rotate forward.
        ScriptRuntime many(LuaResourceLimits{4*1024*1024,64,200});EntityWorld crowded;
        Write(path,"function on_update(e,dt) e:translate(1,0,0) end");Require(many.LoadResolvedAsset("short",path,error),error.c_str());
        std::vector<EntityId> ids;for(int i=0;i<40;++i){ids.push_back(crowded.CreateEntity("Short"));crowded.SetScript(ids.back(),"short");}
        many.Update(crowded,0.5f);Require(many.GetLuaResourceUsage().instructions<=200 && many.GetLuaResourceUsage().instructionExhausted && many.GetLuaResourceUsage().deferredEntities>0 && !crowded.Find(ids.back())->script.started,"Short hooks escaped frame cap");
        for(int frame=0;frame<12;++frame){many.Update(crowded,0.5f);Require(many.GetLuaResourceUsage().instructions<=200,"Later frame exceeded cap");}
        for(const auto id:ids)Require(crowded.Find(id)->script.started,"Deferred cold VM starved across bounded frames");
        Require(crowded.Find(ids.back())->transform.position.x>0,"Last deferred Entity never updated");many.Shutdown(crowded);many.Clear();
        Require(many.GetLuaResourceUsage().states==0 && many.GetLuaResourceUsage().memoryBytes==0,"Short-hook teardown leaked quota");

        ScriptRuntime destroy(LuaResourceLimits{4*1024*1024,8,100});EntityWorld retired;
        Write(path,"function on_update(e,dt) end; function on_destroy(e) e:translate(9,0,0); local n=0;for i=1,10000 do n=n+1 end end");Require(destroy.LoadResolvedAsset("destroy",path,error),error.c_str());
        for(int i=0;i<3;++i){const auto id=retired.CreateEntity("Destroy");retired.SetScript(id,"destroy");}destroy.Update(retired,0.5f);destroy.Shutdown(retired);
        Require(destroy.GetLuaResourceUsage().instructions<=100 && destroy.GetLuaResourceUsage().instructionExhausted,"Shutdown destroy hooks escaped cap");
        for(const auto& e:retired.GetEntities())Require(e.transform.position.x==0 && !e.script.started,"Interrupted destroy published changes or remained started");destroy.Clear();Require(destroy.GetLuaResourceUsage().states==0 && destroy.GetLuaResourceUsage().memoryBytes==0,"Destroy refusal leaked VM");
        std::error_code cleanup;std::filesystem::remove_all(fixture,cleanup);std::cout<<"PASS: aggregate Lua opcode frame cap, short-hook accounting, transactional Entity/Audio refusal, deferred cold-VM fairness/native order, nested/reset/invalid-dt statistics and bounded shutdown cleanup\n";return 0;
    }catch(const std::exception& exception){std::error_code cleanup;std::filesystem::remove_all(fixture,cleanup);std::cerr<<"FAIL: "<<exception.what()<<'\n';return 1;}
}
