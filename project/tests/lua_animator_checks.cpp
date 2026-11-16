#include "engine/script/ScriptRuntime.h"
#include "engine/script/LuaScript.h"
#include <Windows.h>
#include <fstream>
#include <iostream>
static void Require(bool v,const char* why){if(!v)throw std::runtime_error(why);}
static void Write(const std::filesystem::path& file,const std::string& source){std::ofstream out(file,std::ios::binary);out<<source;out.close();Require(!out.fail(),"Lua fixture write failed");}
int main(){using namespace Engine::Scene;using namespace Engine::Scripting;try{
 const auto root=std::filesystem::temp_directory_path()/(L"kubo-lua-animator-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));Require(!std::filesystem::exists(root),"Fresh Lua fixture required");std::filesystem::create_directories(root);std::string error;
 Write(root/"success.lua",R"(
 function on_create(e)
  assert(scene.animator(e:id())==nil and scene.animator(2)==nil)
  assert(scene.set_animator(e:id(),'日本語',0.5,false,false));local c,s,l,p=scene.animator(e:id());assert(c=='日本語' and s==0.5 and not l and not p)
  e:translate(3,0,0);assert(scene.translate(2,2,0,0));assert(scene.set_animator(2,'peer',2,true,true))
  local clone=scene.clone(e:id());assert(clone==3 and scene.animator(clone)=='日本語' and scene.position(clone)==3)
 end
 function on_update(e,dt)
  assert(scene.set_animator(e:id(),'updated',3,true,false));local c,s,l,p=scene.animator(e:id());assert(c=='updated' and s==3 and l and not p)
  assert(not scene.set_animator(999,'default',1,true,true));assert(scene.remove_animator(2) and scene.animator(2)==nil and not scene.remove_animator(2))
 end
 function on_fixed_update(e,dt) assert(scene.set_animator(2,'fixed',0,false,true));assert(scene.animator(2)=='fixed') end
 function on_destroy(e) assert(scene.set_animator(2,'destroy',1,true,false)) end
 )");
 EntityWorld world;const auto owner=world.CreateEntity("Self");world.CreateEntity("Peer");world.SetScript(owner,"success.lua");ScriptRuntime runtime;Require(runtime.LoadAsset("success.lua",root,error),error.c_str());runtime.Update(world,0.1f);
 Require(runtime.GetLastError(owner).empty() && world.Find(owner)->animator.clip=="updated" && world.Find(owner)->animator.speed==3 && world.Find(owner)->transform.position.x==3 && !world.Find(2)->hasAnimator && world.Find(2)->transform.position.x==2 && world.Find(3)->hasAnimator,"Lua self/peer/clone/update publication failed");
 // Runtime clone copies Script too; disable it before the next dispatcher sample.
 world.RemoveScript(3);runtime.FixedUpdate(world,0.1f);Require(runtime.GetLastError(owner).empty() && world.Find(2)->animator.clip=="fixed","Fixed Animator failed");runtime.Shutdown(world);Require(world.Find(2)->animator.clip=="destroy","Destroy Animator failed");
 for(const auto& failure:std::vector<std::string>{"error('rollback')","while true do end","local blob=string.rep('x',16*1024*1024)","scene.set_animator(1,'',1,true,true)","scene.set_animator(1,'x',-1,true,true)","scene.set_animator(1,'x',0/0,true,true)","scene.set_animator(1,'x',math.huge,true,true)","scene.set_animator(1,'x','1',true,true)","scene.set_animator(1,'x',1,1,true)","scene.set_animator(1,string.rep('x',257),1,true,true)","scene.set_animator(1,'a'..string.char(0)..'b',1,true,true)","for i=1,64 do scene.set_animator(1,'x',1,true,true) end"}){
  Write(root/"failure.lua","function on_update(e,dt) e:translate(7,0,0);assert(scene.translate(1,4,0,0));assert(scene.set_animator(1,'discard',1,true,true));audio.play('Assets/sound.wav');"+failure+" end");
  EntityWorld entities;entities.CreateEntity("Peer");const auto bad=entities.CreateEntity("Failed"),healthy=entities.CreateEntity("Healthy");entities.SetScript(bad,"failure.lua");entities.SetScript(healthy,"native");ScriptRuntime failed;failed.SetAudioRequestsEnabled(true);Require(failed.LoadAsset("failure.lua",root,error),error.c_str());ScriptCallbacks callbacks;callbacks.onUpdate=[](EntityRecord& e,float){e.transform.position.x+=1;};Require(failed.Register("native",callbacks),"Healthy registration failed");failed.Update(entities,0.1f);
  Require(!failed.GetLastError(bad).empty() && !entities.Find(1)->hasAnimator && entities.Find(1)->transform.position.x==0 && entities.Find(bad)->transform.position.x==0 && entities.Find(healthy)->transform.position.x==1 && failed.TakeAudioRequests().empty(),"Failed Animator hook partially published World/Audio or blocked healthy peer");failed.Shutdown(entities);
 }
 Write(root/"queue.lua","function on_update(e,dt) assert(scene.translate(1,1,0,0));e:translate(1,0,0);assert(scene.set_animator(1,'queued',e:id(),true,true));for i=1,32 do audio.play('Assets/sound.wav') end end");
 EntityWorld many;many.CreateEntity("Target");std::vector<EntityId> owners;for(int i=0;i<9;++i){const auto id=many.CreateEntity("Owner");many.SetScript(id,"queue.lua");owners.push_back(id);}
 ScriptRuntime queued;queued.SetAudioRequestsEnabled(true);Require(queued.LoadAsset("queue.lua",root,error),error.c_str());queued.Update(many,0.1f);const auto requests=queued.TakeAudioRequests();
 Require(requests.size()==256 && many.Find(1)->hasAnimator && many.Find(1)->animator.speed==9 && many.Find(1)->transform.position.x==8 && many.Find(owners.back())->transform.position.x==0 && !queued.GetLastError(owners.back()).empty(),"Full Audio queue published rejected Animator/scalar hook or lost earlier successful hooks");queued.Shutdown(many);
 auto view=std::make_shared<ScriptSceneView>();ScriptProperties defaults;auto factory=CreateLuaScriptFactory("function on_update(e,dt) assert(scene.set_animator(1,'scoped',0.5,false,true));assert(scene.animator(1)=='scoped') end","Animator scope",{},defaults,{},{},view);
 EntityWorld direct;direct.CreateEntity("Target");const auto reader=direct.CreateEntity("Reader");auto hooks=factory();bool refused=false;
 {ScriptSceneView::Scope scope(*view,direct);try{hooks.onUpdate(*direct.Find(reader),0);}catch(const LuaScriptError&){refused=true;}}
 Require(refused && !direct.Find(1)->hasAnimator,"Read-only Lua Animator scope published settings");
 hooks=factory();{ScriptSceneView::WriteScope scope(*view,direct);hooks.onUpdate(*direct.Find(reader),0);}Require(direct.Find(1)->hasAnimator && direct.Find(1)->animator.clip=="scoped","Explicit Lua Animator write scope failed");
 refused=false;try{hooks.onUpdate(*direct.Find(reader),0);}catch(const LuaScriptError&){refused=true;}Require(refused && direct.Find(1)->animator.clip=="scoped","Expired Lua Animator scope mutated World");
 refused=false;try{CreateLuaScriptFactory("scene.set_animator(1,'module',1,true,true)","Animator module",{},defaults,{},{},view);}catch(const LuaScriptError&){refused=true;}Require(refused && direct.Find(1)->animator.clip=="scoped","Module initializer granted Animator write scope");
 Write(root/"removed.lua","function on_update(e,dt) end\nfunction on_destroy(e) assert(not scene.exists(e:id()) and scene.animator(e:id())==nil and not scene.set_animator(e:id(),'resurrect',1,true,true));assert(scene.set_animator(1,'retired',0,false,false)) end");
 EntityWorld deleted;deleted.CreateEntity("Peer");const auto removed=deleted.CreateEntity("Removed");deleted.SetScript(removed,"removed.lua");ScriptRuntime retired;Require(retired.LoadAsset("removed.lua",root,error),error.c_str());retired.Update(deleted,0);Require(deleted.DestroyEntity(removed),"Retained Animator self deletion failed");retired.Update(deleted,0);
 Require(!deleted.Find(removed) && retired.GetLastError(removed).empty() && deleted.Find(1)->animator.clip=="retired","Retained removed self resurrected or lost peer Animator change");retired.Shutdown(deleted);
 std::cout<<"PASS: Lua Animator authored self/peer queries/settings/removal, scalar/clone composition, Create/Update/Fixed/live-removed Destroy,12 unhandled argument/error/quota/budget rollback cases, full Audio admission refusal/earlier hook retention and readonly/write/expired/module scope gates (no runtime commands/GPU) retained="<<root<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
