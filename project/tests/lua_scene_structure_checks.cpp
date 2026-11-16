#include "engine/script/ScriptRuntime.h"
#include "engine/script/LuaScript.h"
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <cstdlib>
#include <new>
namespace {
bool tracking=false;size_t allocations=0,failAt=0;
void Require(bool b,const char* why){if(!b)throw std::runtime_error(why);}
void Write(const std::filesystem::path& p,const std::string& text){std::ofstream out(p,std::ios::binary);out<<text;out.close();Require(!out.fail(),"Fixture write failed");}
}
void* operator new(size_t n){if(tracking && ++allocations==failAt)throw std::bad_alloc();if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,size_t) noexcept{std::free(p);}
void operator delete[](void* p,size_t) noexcept{std::free(p);}
int main(){using namespace Engine::Scene;using namespace Engine::Scripting;
 const auto root=std::filesystem::temp_directory_path()/(L"kubo-lua-structure-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
 try{
  std::filesystem::create_directories(root);std::string error;
  Write(root/"structure.lua",R"(
local made
function on_create(e)
 assert(scene.translate(1,10,0,0));e:translate(1,0,0)
 made=scene.spawn('生成',1);assert(made and scene.exists(made) and scene.find('生成')==made)
 assert(scene.entity_count()==4 and scene.name(made)=='生成' and scene.parent(made)==1)
 assert(scene.set_position(made,2,0,0));assert(scene.world_position(made)==12)
 assert(scene.translate(e:id(),1,0,0) and scene.position(e:id())==2)
 assert(scene.destroy(2) and not scene.exists(2) and scene.position(2)==nil)
 assert(not scene.destroy(2) and scene.spawn('Missing',999)==nil)
end
function on_update(e,dt)
 local p=scene.spawn('Temporary',0);local c=scene.spawn('Descendant',p)
 assert(scene.set_position(c,3,0,0) and scene.destroy_subtree(p))
 assert(not scene.exists(p) and not scene.exists(c) and scene.world_position(c)==nil)
 assert(scene.translate(made,1,0,0));e:translate(1,0,0)
end
function on_fixed_update(e,dt)
 local x=scene.spawn('Tick');assert(scene.set_position(x,4,0,0) and scene.destroy(x));e:translate(0,dt,0)
end
function on_destroy(e)
 assert(scene.spawn('Shutdown-created'));assert(scene.entity_count()==4)
end
)");
  EntityWorld world;world.CreateEntity("Parent");world.CreateEntity("Delete target");const auto owner=world.CreateEntity("Owner");world.SetScript(owner,"structure.lua");
  ScriptRuntime runtime;Require(runtime.LoadAsset("structure.lua",root,error),error.c_str());runtime.Update(world,0.1f);
  Require(runtime.GetLastError(owner).empty() && world.GetEntities().size()==3 && !world.Find(2) && world.Find(4)->parent==1 && world.Find(4)->transform.position.x==3 && world.Find(owner)->transform.position.x==3,"Lua Create/Update structure publication");
  runtime.FixedUpdate(world,0.25f);Require(runtime.GetLastError(owner).empty() && world.GetEntities().size()==3 && world.Find(owner)->transform.position.y==0.25f,"Lua fixed structure publication");
  runtime.Shutdown(world);Require(runtime.GetLastError(owner).empty() && world.GetEntities().size()==4 && runtime.GetRunningCount()==0,"Lua Shutdown structural hook");runtime.Clear();
  Write(root/"clone.lua",R"(
function on_create(e)
 assert(scene.clone(999)==nil and scene.clone(1,999)==nil)
 assert(scene.translate(1,2,0,0));local copy=scene.clone(1,4)
 assert(copy==5 and scene.parent(copy)==4 and scene.parent(6)==copy)
 assert(scene.position(copy)==2 and scene.world_position(copy)==102)
 assert(scene.entity_count()==6 and scene.name(6)=='Child')
end
function on_update(e,dt) end
)");
  EntityWorld cloned;cloned.CreateEntity("Template");cloned.CreateEntity("Child",1);const auto cloner=cloned.CreateEntity("Cloner");cloned.CreateEntity("Destination");cloned.Find(4)->transform.position.x=100;
  Require(cloned.SetModel(1,"Assets/model.obj","0123456789abcdef0123456789abcdef") && cloned.SetCollider(1,{}) && cloned.SetRigidBody(1,{{4,5,6},3,true,false,true}) && cloned.SetScript(1,"template") && cloned.SetScriptProperty(1,"label",std::string("日本語")) && cloned.SetScript(cloner,"clone.lua"),"Clone component fixture");
  Engine::AudioSystem::AudioComponent sound;sound.assetPath="Assets/sound.wav";sound.loop=true;Require(cloned.SetAudio(1,sound),"Clone Audio fixture");
  std::vector<EntityId> starts,updates;ScriptRuntime cr;
  Require(cr.Register("template",{[&](EntityRecord& e){starts.push_back(e.id);},[&](EntityRecord& e,float){updates.push_back(e.id);},{}}) && cr.LoadAsset("clone.lua",root,error),"Clone runtime assets");
  cr.Update(cloned,0.1f);
  Require(cr.GetLastError(cloner).empty() && cloned.Find(5) && cloned.Find(6) && starts==std::vector<EntityId>{1} && updates==std::vector<EntityId>{1} && !cloned.Find(5)->script.started && !cloned.Find(5)->rigidBody.grounded,"Clone must not start new Script in current dispatch");
  Require(cloned.Find(5)->model.assetGuid==cloned.Find(1)->model.assetGuid && cloned.Find(5)->script.properties==cloned.Find(1)->script.properties && cloned.Find(5)->audio.assetPath==sound.assetPath && cloned.Find(5)->hasCollider && cloned.Find(5)->rigidBody.velocity.x==4,"Lua clone lost typed component/asset data");
  cr.Update(cloned,0.1f);
  Require(cr.GetLastError(5).empty() && starts==std::vector<EntityId>({1,5}) && updates==std::vector<EntityId>({1,1,5}) && cloned.Find(5)->script.started,"Clone Script fresh lifecycle on subsequent dispatch");cr.Shutdown(cloned);cr.Clear();
  Write(root/"clone-template.lua","local n=0;function on_create(e) e:translate(0,1,0) end;function on_update(e,dt) n=n+1;local x,y,z=e:position();e:set_position(x,y,n) end");
  auto isolated=cloned;Require(isolated.DestroyEntity(5) && isolated.DestroyEntity(6) && isolated.SetScript(1,"clone-template.lua"),"Isolated clone fixture");
  // Use a fresh allocator/World: clone.lua deliberately asserts IDs 5 and 6.
  EntityWorld fresh;for(const auto& record:isolated.GetEntities())fresh.GetEntities().push_back(record);
  ScriptRuntime ir;Require(ir.LoadAsset("clone-template.lua",root,error) && ir.LoadAsset("clone.lua",root,error),"Isolated Lua clone assets");fresh.Find(1)->transform.position.x=0;
  ir.Update(fresh,0.1f);Require(ir.GetLastError(3).empty() && fresh.Find(5) && fresh.Find(5)->transform.position.y==1 && fresh.Find(5)->transform.position.z==1,"Lua template clone snapshot");
  ir.Update(fresh,0.1f);Require(ir.GetLastError(5).empty() && fresh.Find(1)->transform.position.z==2 && fresh.Find(5)->transform.position.y==2 && fresh.Find(5)->transform.position.z==1,"Clone shared Lua VM or skipped fresh Create");ir.Shutdown(fresh);ir.Clear();
  Write(root/"death.lua",R"(
function on_update(e,dt)
 e:set_position(7,8,9);assert(scene.destroy(e:id()))
 assert(not scene.exists(e:id()) and not scene.set_active(e:id(),true))
 assert(scene.spawn('After self deletion'))
end
function on_destroy(e)
 assert(not scene.exists(e:id()));local x,y,z=e:position();assert(x==7 and y==8 and z==9)
 assert(scene.spawn('From retained Destroy'))
end
)");
  EntityWorld death;const auto dying=death.CreateEntity("Dying");death.SetScript(dying,"death.lua");ScriptRuntime dr;
  Require(dr.LoadAsset("death.lua",root,error),error.c_str());dr.Update(death,0.1f);
  Require(dr.GetLastError(dying).empty() && !death.Find(dying) && death.GetEntities().size()==2 && dr.GetRunningCount()==0,"Self deletion/final retained Destroy/resurrection");dr.Shutdown(death);dr.Clear();
  Write(root/"parent.lua","function on_update(e,dt) assert(scene.parent(e:id())==1 and scene.world_position(e:id())==20);assert(scene.destroy(1));assert(scene.parent(e:id())==0 and scene.world_position(e:id())==0) end");
  EntityWorld parentWorld;parentWorld.CreateEntity("Parent");parentWorld.Find(1)->transform.position.x=20;const auto childOwner=parentWorld.CreateEntity("Child owner",1);parentWorld.SetScript(childOwner,"parent.lua");ScriptRuntime parentRuntime;
  Require(parentRuntime.LoadAsset("parent.lua",root,error),error.c_str());parentRuntime.Update(parentWorld,0.1f);
  Require(parentRuntime.GetLastError(childOwner).empty() && !parentWorld.Find(1) && parentWorld.Find(childOwner)->parent==0 && parentWorld.Find(childOwner)->transform.position.x==0,"Self parent query retained stale hook parent after single deletion");parentRuntime.Shutdown(parentWorld);parentRuntime.Clear();
  for(const auto& failure:std::vector<std::string>{"error('explicit')","while true do end","local s=string.rep('x',16*1024*1024)",
   "scene.spawn('')","scene.spawn(string.rep('x',257))","scene.spawn('a'..string.char(0)..'b')","scene.spawn(123)",
   "scene.spawn(string.char(255))","scene.spawn(string.char(0x80))","scene.spawn(string.char(0xc0,0xaf))",
   "scene.spawn(string.char(0xc2))","scene.spawn(string.char(0xe0,0x80,0x80))","scene.spawn(string.char(0xed,0xa0,0x80))",
   "scene.spawn(string.char(0xf4,0x90,0x80,0x80))","scene.spawn(string.char(0xf5,0x80,0x80,0x80))",
   "scene.spawn(string.char(0xe2,0x28,0xa1))","scene.spawn(string.rep('a',255)..string.char(0xc2))",
   "scene.spawn('x',-1)","scene.spawn('x',1.5)","scene.spawn('x','1')","scene.destroy(0)","scene.destroy_subtree(true)",
   "scene.clone(0)","scene.clone(1.5)","scene.clone('1')","scene.clone(true)","scene.clone(2,-1)","scene.clone(2,1.5)","scene.clone(2,'0')",
   "for i=1,64 do assert(scene.clone(2)) end",
   "for i=1,64 do scene.translate(e:id(),1,0,0) end"}){
   Write(root/"failure.lua","function on_update(e,dt) assert(scene.translate(1,5,0,0));e:translate(7,0,0);audio.play('Assets/sound.wav');assert(scene.spawn('discarded',1));assert(scene.clone(1));assert(scene.destroy(1));"+failure+" end");
   EntityWorld failedWorld;failedWorld.CreateEntity("Target");const auto bad=failedWorld.CreateEntity("Bad"),healthy=failedWorld.CreateEntity("Healthy");failedWorld.SetScript(bad,"failure.lua");failedWorld.SetScript(healthy,"healthy");
   const auto allocator=failedWorld.AllocationHighWatermark();ScriptRuntime failed;failed.SetAudioRequestsEnabled(true);
   Require(failed.LoadAsset("failure.lua",root,error),error.c_str());Require(failed.Register("healthy",{{},[](EntityRecord& e,float){e.transform.position.x+=1;},{}}),"Healthy registration");failed.Update(failedWorld,0.1f);
   Require(!failed.GetLastError(bad).empty() && failedWorld.Find(1) && failedWorld.Find(1)->transform.position.x==0 && failedWorld.Find(bad)->transform.position.x==0 && failedWorld.GetEntities().size()==3 && failedWorld.AllocationHighWatermark()==allocator && failedWorld.Find(healthy)->transform.position.x==1 && failed.TakeAudioRequests().empty(),"Fault leaked spawn/delete/ID/scalar/audio or blocked native peer");failed.Shutdown(failedWorld);failed.Clear();
  }
  Write(root/"queue.lua","function on_update(e,dt) assert(scene.spawn('Queue-created'));assert(scene.clone(1));assert(scene.translate(1,1,0,0));e:translate(1,0,0);for i=1,32 do audio.play('Assets/a.wav') end end");
  EntityWorld queued;queued.CreateEntity("Target");std::vector<EntityId> ids;
  for(int i=0;i<9;++i){auto id=queued.CreateEntity("Owner");queued.SetScript(id,"queue.lua");ids.push_back(id);}
  ScriptRuntime queue;queue.SetAudioRequestsEnabled(true);Require(queue.LoadAsset("queue.lua",root,error),error.c_str());queue.Update(queued,0.1f);
  Require(queued.GetEntities().size()==26 && queued.AllocationHighWatermark()==27 && queued.Find(1)->transform.position.x==8 && queued.Find(ids.back())->transform.position.x==0 && !queue.GetLastError(ids.back()).empty() && queue.TakeAudioRequests().size()==256,"Audio refusal published last structure or lost earlier hooks");queue.Shutdown(queued);queue.Clear();
  auto input=std::make_shared<Engine::InputSystem::InputFrame>();auto view=std::make_shared<ScriptSceneView>();auto audio=std::make_shared<ScriptAudioRequests>();audio->SetEnabled(true);ScriptProperties defaults;
  const auto source="function on_update(e,dt) e:translate(2,0,0);assert(scene.translate(1,3,0,0));assert(scene.clone(1,1));assert(scene.destroy(2));audio.stop_all() end";
  auto factory=CreateLuaScriptFactory(source,"direct structure",input,defaults,audio,{},view);
  // Fault every ordinary native allocation in a complete direct Lua hook,
  // including candidate copy, structural preparation and Audio admission.
  size_t positions=0;
  for(size_t position=0;;++position){
   EntityWorld live;live.CreateEntity("Target");live.CreateEntity("Delete");const auto id=live.CreateEntity(std::string(300,'n'));
   auto hooks=factory();audio->Clear();const auto snapshot=live.Capture().dump();const auto allocator=live.AllocationHighWatermark();
   ScriptSceneView::WriteScope scope(*view,live);allocations=0;failAt=position;tracking=true;bool faulted=false;
   try{hooks.onUpdate(*live.Find(id),0);}catch(...){faulted=true;}tracking=false;failAt=0;
   const auto pending=audio->Take();
   if(position==0){positions=allocations;Require(!faulted && positions>0 && live.Find(id)->transform.position.x==2 && live.Find(1)->transform.position.x==3 && !live.Find(2) && live.Find(4) && pending.size()==1,"Native allocation baseline/direct live reference");}
   else Require(faulted && live.Capture().dump()==snapshot && live.AllocationHighWatermark()==allocator && pending.empty(),"Native hook failure partially published structure/scalars/audio/allocator");
   if(position==positions)break;
  }
  EntityWorld direct;direct.CreateEntity("Target");direct.CreateEntity("Delete");direct.CreateEntity("Owner");auto hooks=factory();bool refused=false;
  {ScriptSceneView::Scope scope(*view,direct);try{hooks.onUpdate(*direct.Find(3),0);}catch(const LuaScriptError&){refused=true;}}
  Require(refused && direct.GetEntities().size()==3 && direct.Find(3)->transform.position.x==0,"Readonly direct hook mutated");
  refused=false;try{CreateLuaScriptFactory("scene.clone(1);function on_update(e,dt) end","module",input,defaults,audio,{},view);}catch(const LuaScriptError&){refused=true;}Require(refused,"Module-level structural mutation allowed");
  std::cout<<"PASS: Lua spawn/component clone/single/subtree deletion, candidate/parent/world/scalar reads, fresh clone Script lifecycle, Create/Update/Fixed/Shutdown/retained self-deletion, strict/error/instruction/memory/mixed-cap/Audio refusal rollback, readonly/module gates and "<<positions<<" native hook allocation failure positions\nRetained fixtures: "<<root.string()<<'\n';return 0;
 }catch(const std::exception& e){tracking=false;std::cerr<<"FAIL: "<<e.what()<<"; fixtures="<<root.string()<<'\n';return 1;}
}
