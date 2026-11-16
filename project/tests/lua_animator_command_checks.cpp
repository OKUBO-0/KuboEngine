#include "engine/script/LuaScript.h"
#include <iostream>
static void Require(bool b,const char* why){if(!b)throw std::runtime_error(why);}
// Keep each large fixed Pending return in one short frame; dozens of debug
// temporaries in main would exhaust the default stack before the test starts.
__declspec(noinline) static size_t DrainCount(Engine::Scripting::ScriptAnimatorRequests& requests){return requests.Take().count;}
int main(){try{using namespace Engine::Scene;using namespace Engine::Scripting;using Engine::AnimationSystem::AnimatorCommand;
 EntityWorld world;const auto owner=world.CreateEntity("Owner");world.SetModel(owner,"owned.gltf");world.SetAnimator(owner,{});auto view=std::make_shared<ScriptSceneView>();auto requests=std::make_shared<ScriptAnimatorRequests>();auto audio=std::make_shared<ScriptAudioRequests>();requests->SetEnabled(true);audio->SetEnabled(true);ScriptProperties defaults;
 auto factory=CreateLuaScriptFactory("function on_update(e,dt) assert(scene.animator_play(e:id()) and scene.animator_pause(e:id()) and scene.animator_stop(e:id()) and scene.animator_restart(e:id()));assert(not scene.animator_play(999));e:translate(1,0,0);audio.play('owned.wav') end","Animator queue",{},defaults,audio,{},view,requests);auto hooks=factory();
 {ScriptSceneView::WriteScope scope(*view,world);hooks.onUpdate(*world.Find(owner),0);}auto pending=requests->Take();Require(pending.count==4 && pending.values[0].command==AnimatorCommand::Play && pending.values[3].command==AnimatorCommand::Restart && world.Find(owner)->transform.position.x==1 && audio->Take().size()==1,"Lua commands/FIFO/Audio/scalar publication failed");
 const auto before=world.Capture().dump();auto failure=CreateLuaScriptFactory("function on_update(e,dt) assert(scene.animator_restart(e:id()));e:translate(9,0,0);audio.play('owned.wav');error('discard') end","Animator failure",{},defaults,audio,{},view,requests);hooks=failure();bool refused=false;{ScriptSceneView::WriteScope scope(*view,world);try{hooks.onUpdate(*world.Find(owner),0);}catch(const LuaScriptError&){refused=true;}}
 Require(refused && world.Capture().dump()==before && DrainCount(*requests)==0 && audio->Take().empty(),"Failed hook leaked commands/Audio/World");
 ScriptAnimatorBatch batch;for(int i=0;i<64;++i)Require(batch.Append(owner,AnimatorCommand::Play,world.Find(owner)->animator),"Full fixture failed");for(int i=0;i<4;++i)Require(requests->PublishTogether(batch,nullptr,{}),"Queue fill failed");hooks=factory();refused=false;{ScriptSceneView::WriteScope scope(*view,world);try{hooks.onUpdate(*world.Find(owner),0);}catch(const LuaScriptError&){refused=true;}}
 Require(refused && world.Capture().dump()==before && DrainCount(*requests)==256 && audio->Take().empty(),"Full Animator queue leaked Audio/scalars or lost earlier requests");
 std::vector<ScriptAudioRequest> sounds(32);for(auto& sound:sounds)sound.path="earlier.wav";
 for(int i=0;i<8;++i)Require(audio->Publish(sounds),"Audio queue fill failed");
 hooks=factory();refused=false;{ScriptSceneView::WriteScope scope(*view,world);try{hooks.onUpdate(*world.Find(owner),0);}catch(const LuaScriptError&){refused=true;}}
 Require(refused && world.Capture().dump()==before && DrainCount(*requests)==0,"Full Audio queue leaked Animator/World");
 const auto retainedSounds=audio->Take();Require(retainedSounds.size()==256,"Audio refusal lost previous hooks");for(const auto& sound:retainedSounds)Require(sound.path=="earlier.wav","Audio refusal replaced earlier sounds");
 // Each invalid ownership/scope fails before a request or scalar is published.
 for(const char* source:{"function on_update(e,dt) scene.animator_play(e:id()) end","function on_update(e,dt) scene.animator_pause(e:id()) end","function on_update(e,dt) scene.animator_stop(e:id()) end","function on_update(e,dt) scene.animator_restart(e:id()) end"}){
  auto scopedFactory=CreateLuaScriptFactory(source,"Animator scope",{},defaults,audio,{},view,requests);
  hooks=scopedFactory();refused=false;{ScriptSceneView::Scope scope(*view,world);try{hooks.onUpdate(*world.Find(owner),0);}catch(const LuaScriptError&){refused=true;}}
  Require(refused && world.Capture().dump()==before && DrainCount(*requests)==0,"Read-only scope allowed Animator command");
  {ScriptSceneView::WriteScope scope(*view,world);}refused=false;try{hooks.onUpdate(*world.Find(owner),0);}catch(const LuaScriptError&){refused=true;}
  Require(refused && world.Capture().dump()==before && DrainCount(*requests)==0,"Expired scope allowed Animator command");
 }
 refused=false;{ScriptSceneView::WriteScope scope(*view,world);try{auto moduleFactory=CreateLuaScriptFactory("scene.animator_play(1)","Animator module",{},defaults,audio,{},view,requests);auto moduleHooks=moduleFactory();(void)moduleHooks;}catch(const LuaScriptError&){refused=true;}}
 Require(refused && world.Capture().dump()==before && DrainCount(*requests)==0,"Module scope allowed Animator command");
 auto missing=CreateLuaScriptFactory("function on_update(e,dt) assert(scene.set_active(e:id(),false));assert(not scene.animator_play(e:id())) end","Animator inactive",{},defaults,audio,{},view,requests);hooks=missing();{ScriptSceneView::WriteScope scope(*view,world);hooks.onUpdate(*world.Find(owner),0);}Require(!world.Find(owner)->active && DrainCount(*requests)==0,"Inactive candidate accepted command");world.Find(owner)->active=true;
 auto quota=CreateLuaScriptFactory("function on_update(e,dt) assert(scene.translate(e:id(),1,0,0));for i=1,64 do assert(scene.animator_play(e:id())) end end","Animator shared cap",{},defaults,audio,{},view,requests);hooks=quota();refused=false;{ScriptSceneView::WriteScope scope(*view,world);try{hooks.onUpdate(*world.Find(owner),0);}catch(const LuaScriptError&){refused=true;}}
 Require(refused && world.Capture().dump()==before && DrainCount(*requests)==0,"Animator did not share64 Scene operation budget");
 requests->SetEnabled(false);hooks=factory();refused=false;{ScriptSceneView::WriteScope scope(*view,world);try{hooks.onUpdate(*world.Find(owner),0);}catch(const LuaScriptError&){refused=true;}}Require(refused && world.Capture().dump()==before,"Disabled host allowed commands");
 requests->SetEnabled(true);auto controllerCfg=world.Find(owner)->animator;controllerCfg.controllerGuid=std::string(32,'a');Require(world.SetAnimator(owner,controllerCfg),"Controller queue fixture failed");
 const auto controllerBefore=world.Capture().dump();
 const auto controllerRun=[&](const char* source,bool writable=true){auto f=CreateLuaScriptFactory(source,"Controller writes",{},defaults,audio,{},view,requests);auto h=f();if(writable){ScriptSceneView::WriteScope scope(*view,world);h.onUpdate(*world.Find(owner),0);}else{ScriptSceneView::Scope scope(*view,world);h.onUpdate(*world.Find(owner),0);}};
 controllerRun("function on_update(e,dt) assert(scene.controller_set_float(e:id(),'speed',2));assert(scene.controller_set_bool(e:id(),'enabled',true));assert(scene.controller_trigger(e:id(),'go'));assert(scene.animator_pause(e:id()));e:translate(1,0,0);audio.play('owned.wav') end");
 auto controllerPending=requests->Take();Require(controllerPending.count==4 && controllerPending.values[0].kind==ScriptAnimatorRequestKind::Float && controllerPending.values[0].number==2 && controllerPending.values[1].flag && controllerPending.values[2].kind==ScriptAnimatorRequestKind::Trigger && controllerPending.values[3].kind==ScriptAnimatorRequestKind::Playback && audio->Take().size()==1,"Typed Controller/Playback FIFO publication failed");
 world.Find(owner)->transform.position.x-=1;
 for(const char* source:{
  "function on_update(e,dt) assert(scene.controller_trigger(e:id(),'go'));e:translate(9,0,0);audio.play('owned.wav');error('discard') end",
  "function on_update(e,dt) e:translate(9,0,0);scene.controller_set_float(e:id(),'speed',0/0) end",
  "function on_update(e,dt) e:translate(9,0,0);scene.controller_set_bool(e:id(),'enabled',1) end",
  "function on_update(e,dt) e:translate(9,0,0);scene.controller_trigger(e:id(),string.rep('a',129)) end",
  "function on_update(e,dt) assert(scene.translate(e:id(),1,0,0));for i=1,64 do assert(scene.controller_trigger(e:id(),'go')) end end"
 }) {bool denied=false;try{controllerRun(source);}catch(const LuaScriptError&){denied=true;}Require(denied && world.Capture().dump()==controllerBefore && DrainCount(*requests)==0 && audio->Take().empty(),"Controller invalid/failed/quota hook leaked effects");}
 refused=false;try{controllerRun("function on_update(e,dt) scene.controller_trigger(e:id(),'go') end",false);}catch(const LuaScriptError&){refused=true;}Require(refused && DrainCount(*requests)==0,"Readonly Controller write admitted");
 ScriptAnimatorBatch controllerFull;for(int i=0;i<64;++i)Require(controllerFull.AppendController(owner,ScriptAnimatorRequestKind::Trigger,"go",0,false,controllerCfg),"Controller full fixture failed");for(int i=0;i<4;++i)Require(requests->PublishTogether(controllerFull,nullptr,{}),"Controller queue fill failed");
 refused=false;try{controllerRun("function on_update(e,dt) assert(scene.controller_trigger(e:id(),'go'));e:translate(9,0,0);audio.play('owned.wav') end");}catch(const LuaScriptError&){refused=true;}
 Require(refused && DrainCount(*requests)==256 && audio->Take().empty() && world.Capture().dump()==controllerBefore,"Full Controller queue leaked Audio/World or lost earlier hooks");
 requests->SetEnabled(false);refused=false;try{controllerRun("function on_update(e,dt) scene.controller_trigger(e:id(),'go') end");}catch(const LuaScriptError&){refused=true;}Require(refused && world.Capture().dump()==controllerBefore,"Disabled Controller host admitted write");
 std::cout<<"PASS: Lua Controller typed fixed requests/mixed FIFO/paired Audio World publication/failed+invalid+quota+readonly+full+disabled isolation (parameter admission is queued, not actual owner validation)\n";
 std::cout<<"PASS: Lua Animator queued Play/Pause/Stop/Restart FIFO, paired Audio/World success and failed-hook/full Animator/full Audio/shared-budget/readonly/expired/module/inactive/disabled-host isolation (not real Scene/GPU/query)\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
