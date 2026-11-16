#include "engine/script/LuaScript.h"
#include "engine/script/ScriptRuntime.h"
#include <fstream>
#include <iostream>
#include <limits>
#include <Windows.h>
static void Require(bool b,const char* message){if(!b)throw std::runtime_error(message);}
class Reader final : public Engine::AnimationSystem::AnimatorStateReader {
public:
    explicit Reader(const Engine::Scene::EntityWorld& world):world_(world){}
    const Engine::Scene::EntityWorld* OwnerWorld() const noexcept override{return &world_;}
    Engine::AnimationSystem::AnimatorPlaybackState ReadAnimatorState(const Engine::Scene::EntityRecord& entity,const Engine::AnimationSystem::AnimatorComponent& cfg) const noexcept override{
        ++reads;if(entity.id!=1 || cfg.clip!="default" || cfg.speed!=1 || !cfg.loop || !cfg.playOnAwake)return {};return sample;
    }
    Engine::AnimationSystem::AnimatorPlaybackState sample{true,true,1.25f};mutable int reads=0;
    std::string_view controllerName="Run";
    std::string_view ReadControllerState(const Engine::Scene::EntityRecord&,const Engine::AnimationSystem::AnimatorComponent& cfg) const noexcept override {
        return cfg.controllerGuid=="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" && cfg.speed==1 ? controllerName : std::string_view{};
    }
private:
    const Engine::Scene::EntityWorld& world_;
};
int main(){try{
 using namespace Engine::Scene;using namespace Engine::Scripting;
 EntityWorld world;const auto id=world.CreateEntity("Owner");Require(world.SetModel(id,"owned.gltf") && world.SetAnimator(id,{}),"State fixture failed");Reader reader(world);auto view=std::make_shared<ScriptSceneView>();auto requests=std::make_shared<ScriptAnimatorRequests>();requests->SetEnabled(true);ScriptProperties defaults;
 const auto run=[&](const char* source,bool writable=true,const Engine::AnimationSystem::AnimatorStateReader* selected=nullptr){auto factory=CreateLuaScriptFactory(source,"Animator state",{},defaults,{},{},view,requests);auto callbacks=factory();if(writable){ScriptSceneView::WriteScope scope(*view,world,nullptr,selected?selected:&reader);callbacks.onUpdate(*world.Find(id),0);}else{ScriptSceneView::Scope scope(*view,world,selected?selected:&reader);callbacks.onUpdate(*world.Find(id),0);}};
 run("function on_update(e,dt) local r,p,t=scene.animator_state(e:id());assert(r and p and t==1.25);assert(scene.animator_state(999)==nil) end",false);
 run("function on_update(e,dt) assert(scene.controller_state(e:id())==nil and scene.controller_state(999)==nil) end",false);
 auto controllerSettings=world.Find(id)->animator;controllerSettings.controllerGuid=std::string(32,'a');Require(world.SetAnimator(id,controllerSettings),"Controller query fixture failed");
 run("function on_update(e,dt) assert(scene.controller_state(e:id())=='Run');assert(scene.animator_pause(e:id()));assert(scene.controller_state(e:id())=='Run') end");Require(requests->Take().count==1,"Controller query lost pending command");
 run("function on_update(e,dt) assert(scene.controller_state(e:id())=='Run') end",false);
 run("function on_update(e,dt) assert(scene.set_animator(e:id(),'default',2,true,true));assert(scene.controller_state(e:id())==nil) end");
 Require(world.SetAnimator(id,controllerSettings),"Controller query settings restore failed");
 const std::string invalidName(1,char(0xff));reader.controllerName=invalidName;
 run("function on_update(e,dt) assert(scene.controller_state(e:id())==nil) end",false);
 reader.controllerName="Run";Require(world.SetAnimator(id,{}),"Legacy query fixture restore failed");
 run("function on_update(e,dt) local r,p,t=scene.animator_state(e:id());assert(scene.animator_pause(e:id()));local a,b,c=scene.animator_state(e:id());assert(a==r and b==p and c==t) end");Require(requests->Take().count==1,"Query synthesized/lost queued state");
 run("function on_update(e,dt) assert(scene.set_animator(e:id(),'default',2,true,true));local r,p,t=scene.animator_state(e:id());assert(not r and not p and t==nil);assert(scene.set_animator(e:id(),'default',1,true,true));local a,b,c=scene.animator_state(e:id());assert(a and b and c==1.25) end");
 run("function on_update(e,dt) assert(scene.set_active(e:id(),false));local r,p,t=scene.animator_state(e:id());assert(not r and not p and t==nil) end");world.Find(id)->active=true;
 run("function on_update(e,dt) assert(scene.remove_animator(e:id()));assert(scene.animator_state(e:id())==nil) end");Require(world.SetAnimator(id,{}),"State restore failed");
 for(const float invalid:{-1.f,(std::numeric_limits<float>::infinity)(),(std::numeric_limits<float>::quiet_NaN)()}){reader.sample.time=invalid;run("function on_update(e,dt) local r,p,t=scene.animator_state(e:id());assert(not r and not p and t==nil) end",false);}reader.sample.time=1.25f;
 const auto before=world.Capture().dump();
 for(const char* source:{"function on_update(e,dt) e:translate(9,0,0);scene.animator_state(-1) end","function on_update(e,dt) e:translate(9,0,0);scene.animator_state(1.5) end","function on_update(e,dt) e:translate(9,0,0);scene.animator_state('1') end"}){bool refused=false;try{run(source);}catch(const LuaScriptError&){refused=true;}Require(refused && world.Capture().dump()==before,"Invalid state query published scalar");}
 const auto scopedFactory=CreateLuaScriptFactory("function on_update(e,dt) scene.animator_state(e:id()) end","State ownership",{},defaults,{},{},view,requests);auto callbacks=scopedFactory();
 bool refused=false;{ScriptSceneView::WriteScope scope(*view,world);try{callbacks.onUpdate(*world.Find(id),0);}catch(const LuaScriptError&){refused=true;}}Require(refused,"No reader allowed query");
 EntityWorld other;Reader wrong(other);const int readsBefore=wrong.reads;refused=false;try{run("function on_update(e,dt) scene.animator_state(e:id()) end",true,&wrong);}catch(const LuaScriptError&){refused=true;}Require(refused && wrong.reads==readsBefore,"Wrong World reader was invoked");
 {ScriptSceneView::Scope outer(*view,world,&reader);{ScriptSceneView::Scope inner(*view,world,&wrong);Require(view->AnimatorReader()==&wrong,"Nested reader missing");}Require(view->AnimatorReader()==&reader,"Nested reader not restored");}Require(!view->World() && !view->AnimatorReader(),"Reader escaped Scene scope");
 refused=false;try{callbacks.onUpdate(*world.Find(id),0);}catch(const LuaScriptError&){refused=true;}Require(refused,"Expired reader query allowed");
 refused=false;{ScriptSceneView::WriteScope scope(*view,world,nullptr,&reader);try{auto module=CreateLuaScriptFactory("scene.animator_state(1)","State module",{},defaults,{},{},view,requests);auto hooks=module();(void)hooks;}catch(const LuaScriptError&){refused=true;}}Require(refused,"Module-scope state read allowed");
 const auto controllerFactory=CreateLuaScriptFactory("function on_update(e,dt) scene.controller_state(e:id()) end","Controller ownership",{},defaults,{},{},view,requests);auto controllerCallbacks=controllerFactory();
 refused=false;try{run("function on_update(e,dt) scene.controller_state(e:id()) end",false,&wrong);}catch(const LuaScriptError&){refused=true;}Require(refused,"Wrong World Controller query allowed");
 refused=false;try{controllerCallbacks.onUpdate(*world.Find(id),0);}catch(const LuaScriptError&){refused=true;}Require(refused,"Expired Controller query allowed");
 refused=false;{ScriptSceneView::WriteScope scope(*view,world);try{controllerCallbacks.onUpdate(*world.Find(id),0);}catch(const LuaScriptError&){refused=true;}}Require(refused,"Controller query without reader allowed");
 refused=false;{ScriptSceneView::WriteScope scope(*view,world,nullptr,&reader);try{auto module=CreateLuaScriptFactory("scene.controller_state(1)","Controller module",{},defaults,{},{},view,requests);auto hooks=module();(void)hooks;}catch(const LuaScriptError&){refused=true;}}Require(refused,"Module-scope Controller query allowed");
 for(const char* source:{"function on_update(e,dt) e:translate(9,0,0);scene.controller_state(-1) end","function on_update(e,dt) e:translate(9,0,0);scene.controller_state(1.5) end","function on_update(e,dt) e:translate(9,0,0);scene.controller_state('1') end"}){bool denied=false;try{run(source);}catch(const LuaScriptError&){denied=true;}Require(denied && world.Capture().dump()==before,"Invalid Controller query published scalar");}
 // Actual ScriptRuntime propagates the borrowed source to all lifecycle hooks.
 const auto root=std::filesystem::temp_directory_path()/(L"kubo-animator-state-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));std::filesystem::create_directories(root);
 const auto source=root/"state.lua";{std::ofstream out(source);out<<"local function state(e) local r,p,t=scene.animator_state(e:id());assert(r and p and t==1.25) end\nfunction on_create(e) state(e);e:translate(1,0,0) end\nfunction on_update(e,dt) state(e);e:translate(0,1,0) end\nfunction on_fixed_update(e,dt) state(e);e:translate(0,0,1) end\nfunction on_destroy(e) state(e);e:set_rotation(0,1,0) end\n";Require(bool(out),"State lifecycle fixture write failed");}
 ScriptRuntime runtime;std::string error;Require(runtime.LoadResolvedAsset("state.lua",source,error),error.c_str());Require(world.SetScript(id,"state.lua"),"State Script assignment failed");
 {ScriptRuntime::AnimatorReadScope scope(runtime,&reader);{ScriptRuntime::AnimatorReadScope nested(runtime,&wrong);}runtime.Update(world,0.1f);runtime.FixedUpdate(world,0.1f);runtime.Shutdown(world);}
 Require(runtime.GetLastError(id).empty() && world.Find(id)->transform.position.x==1 && world.Find(id)->transform.position.y==1 && world.Find(id)->transform.position.z==1 && world.Find(id)->transform.rotation.y==1,"Runtime Create/Update/Fixed/Destroy lost state reader");
 ScriptCallbacks native;native.onUpdate=[&](EntityRecord&,float){bool denied=false;try{ScriptRuntime::AnimatorReadScope scope(runtime,&reader);}catch(const std::logic_error&){denied=true;}Require(denied,"Callback replaced state reader");};Require(runtime.Register("native",std::move(native)) && world.SetScript(id,"native"),"Reader replacement guard fixture failed");runtime.Update(world,0.1f);runtime.Shutdown(world);
 std::cout<<"PASS: Lua actual Animator state scoped reader, readonly/nested/expired/module/wrong-World/strict-ID admission, pending commands and candidate settings, invalid sample isolation, Runtime Create/Update/Fixed/Destroy and callback replacement refusal (no real Scene/GPU)\n";std::cout<<"Retained state lifecycle fixture: "<<root<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
