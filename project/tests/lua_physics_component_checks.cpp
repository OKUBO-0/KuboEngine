#include "engine/script/ScriptRuntime.h"
#include "engine/script/LuaScript.h"
#include "engine/physics/PhysicsWorld.h"
#include <Windows.h>
#include <fstream>
#include <iostream>
namespace {
void Need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void Write(const std::filesystem::path& path,const std::string& text){std::ofstream out(path,std::ios::binary);out<<text;out.close();Need(!out.fail(),"Physics Lua fixture write failed");}
void Load(Engine::Scripting::ScriptRuntime& runtime,const std::filesystem::path& root,const char* name){std::string error;if(!runtime.LoadAsset(name,root,error))throw std::runtime_error(error);}
}
int main(){try{
 using namespace Engine;using namespace Scene;using namespace Scripting;
 const auto root=std::filesystem::temp_directory_path()/(L"kubo-lua-physics-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));Need(!std::filesystem::exists(root),"Fresh fixture required");std::filesystem::create_directories(root);
 Write(root/"success.lua",R"(
 function on_create(e)
  local id=e:id();assert(scene.collider(id)==nil and scene.rigid_body(id)==nil)
  assert(scene.set_collider(id,2,3,4,true,0.25,0.75))
  local x,y,z,t,f,r=scene.collider(id);assert(x==2 and y==3 and z==4 and t and f==0.25 and r==0.75)
  assert(scene.set_rigid_body(id,4,5,6,2,true,false));assert(e:velocity()==4)
  e:set_velocity(7,0,0);assert(scene.velocity(id)==7 and scene.rigid_body(id)==7)
  local copy=scene.clone(id);assert(copy==3 and scene.rigid_body(copy)==7 and scene.collider(copy)==2)
  assert(scene.set_round_collider(copy,"capsule",0.25,1.5,true,0,0))
  local kind,radius,half=scene.collider_shape(copy);assert(kind=="capsule" and radius==0.25 and half==1.5)
  assert(scene.collider_shape(id)=="box")
  assert(scene.set_rigid_body(2,9,0,0,3,false,false));assert(scene.set_velocity(2,8,0,0))
  assert(scene.set_rigid_body(2,6,0,0,4,true,false));assert(scene.velocity(2)==6)
  assert(scene.remove_rigid_body(2) and scene.rigid_body(2)==nil and scene.velocity(2)==nil)
  assert(not scene.set_velocity(2,1,0,0) and not scene.remove_rigid_body(2))
  assert(scene.set_rigid_body(2,5,0,0,2,true,false));assert(scene.set_collider(2,1,1,1,false,0,0))
  assert(scene.remove_collider(2) and scene.collider(2)==nil and not scene.remove_collider(2))
  assert(not scene.set_collider(999,1,1,1,false,0,0) and not scene.set_rigid_body(999,0,0,0,1,true,true))
 end
 function on_update(e,dt)
  assert(scene.rigid_body(e:id())==7);e:translate(1,0,0)
 end
 function on_fixed_update(e,dt)
  assert(scene.set_rigid_body(e:id(),2,0,0,2,true,false));e:set_velocity(3,0,0)
  local x,y,z,m,d,g,ground=scene.rigid_body(e:id());assert(x==3 and m==2 and d and not g and not ground)
 end
 function on_destroy(e)
  assert(scene.remove_collider(2)==false);assert(scene.set_collider(2,0,0,0,true,1,1))
 end
 )");
 EntityWorld world;world.CreateEntity("Self");world.CreateEntity("Peer");world.SetScript(1,"success.lua");ScriptRuntime runtime;Load(runtime,root,"success.lua");runtime.Update(world,0.1f);
 Need(runtime.GetLastError(1).empty() && world.Find(1)->hasRigidBody && world.Find(1)->rigidBody.velocity.x==7 && world.Find(1)->collider.halfExtents.y==3 && world.Find(2)->rigidBody.velocity.x==5 && !world.Find(2)->hasCollider && world.Find(3)->rigidBody.velocity.x==7,"Lua Physics Create/Update/self addition/clone/order publication failed");
 Need(world.Find(3)->collider.shape==Physics::ColliderShape::Capsule &&
  world.Find(3)->collider.radius==0.25f && world.Find(3)->collider.halfSegment==1.5f,"Lua capsule did not publish");
 world.RemoveScript(3);Physics::PhysicsWorld physics;physics.Advance(world,1.0f/60.0f,[&](float dt){runtime.FixedUpdate(world,dt);});
 Need(runtime.GetLastError(1).empty() && world.Find(1)->rigidBody.velocity.x==3 && world.Find(1)->transform.position.x>1,"Fixed Physics edit not consumed by solver");runtime.Shutdown(world);Need(world.Find(2)->hasCollider && world.Find(2)->collider.isTrigger,"Live Destroy Physics edit failed");
 // Full settings and all scalar/Audio prefixes are private until hook success.
 const std::vector<std::string> failures={"error('rollback')","while true do end","local s=string.rep('x',16*1024*1024)",
  "scene.set_collider(1,-1e-100,1,1,false,0,0)","scene.set_collider(1,1,1,1,1,0,0)","scene.set_collider(1,1,1,1,false,1.1,0)","scene.set_collider(1,1,1,1,false,0,0/0)","scene.set_collider(1,1e100,1,1,false,0,0)","scene.set_collider(1,'1',1,1,false,0,0)",
  "scene.set_rigid_body(1,0,0,0,0,true,true)","scene.set_rigid_body(1,0,0,0,-1,true,true)","scene.set_rigid_body(1,0,0,0,1e-100,true,true)","scene.set_rigid_body(1,math.huge,0,0,1,true,true)","scene.set_rigid_body(1,0,0,0,1,1,true)","scene.set_rigid_body(1,0,0,0,1,true,'true')",
  "for i=1,64 do scene.set_collider(1,1,1,1,false,0,0) end"};
 for(const auto& suffix:failures){
  Write(root/"bad.lua","function on_update(e,dt) e:translate(7,0,0);assert(scene.translate(1,4,0,0));assert(scene.set_collider(1,2,2,2,false,0.5,0.5));assert(scene.set_rigid_body(e:id(),8,0,0,2,true,false));audio.play('Assets/sound.wav');"+suffix+" end");
  EntityWorld entities;entities.CreateEntity("Target");entities.CreateEntity("Bad");entities.CreateEntity("Good");entities.SetScript(2,"bad.lua");entities.SetScript(3,"native");const auto high=entities.AllocationHighWatermark();ScriptRuntime failed;failed.SetAudioRequestsEnabled(true);Load(failed,root,"bad.lua");ScriptCallbacks healthy;healthy.onUpdate=[](EntityRecord& entity,float){entity.transform.position.x+=1;};Need(failed.Register("native",healthy),"Healthy peer registration failed");failed.Update(entities,0.1f);
  Need(!failed.GetLastError(2).empty() && entities.Find(1)->transform.position.x==0 && !entities.Find(1)->hasCollider && !entities.Find(2)->hasRigidBody && entities.Find(2)->transform.position.x==0 && entities.Find(3)->transform.position.x==1 && failed.TakeAudioRequests().empty() && entities.AllocationHighWatermark()==high,"Failed Physics hook leaked World/self/Audio/IDs or blocked peer");failed.Shutdown(entities);
 }
 Write(root/"queue.lua","function on_update(e,dt) assert(scene.set_rigid_body(e:id(),8,0,0,2,true,false));assert(scene.set_collider(1,e:id(),1,1,false,0,0));for i=1,32 do audio.play('Assets/sound.wav') end end");
 EntityWorld queueWorld;queueWorld.CreateEntity("Target");for(int i=0;i<9;++i){const auto id=queueWorld.CreateEntity("Owner");queueWorld.SetScript(id,"queue.lua");}ScriptRuntime queued;queued.SetAudioRequestsEnabled(true);Load(queued,root,"queue.lua");queued.Update(queueWorld,0.1f);
 Need(queued.TakeAudioRequests().size()==256 && queueWorld.Find(1)->collider.halfExtents.x==9 && !queueWorld.Find(10)->hasRigidBody && !queued.GetLastError(10).empty(),"Full Audio queue published rejected Physics or lost earlier prefix");queued.Shutdown(queueWorld);
 auto view=std::make_shared<ScriptSceneView>();ScriptProperties defaults;
 auto factory=CreateLuaScriptFactory("function on_update(e,dt) assert(scene.set_rigid_body(1,6,0,0,2,true,false));assert(scene.set_collider(1,1,1,1,false,0,0)) end","physics scope",{},defaults,{},{},view);
 EntityWorld scoped;scoped.CreateEntity("Self");auto hook=factory();bool refused=false;
 {ScriptSceneView::Scope scope(*view,scoped);try{hook.onUpdate(*scoped.Find(1),0);}catch(const LuaScriptError&){refused=true;}}
 Need(refused && !scoped.Find(1)->hasRigidBody,"Readonly Lua Physics scope published");hook=factory();
 {ScriptSceneView::WriteScope scope(*view,scoped);hook.onUpdate(*scoped.Find(1),0);}Need(scoped.Find(1)->hasRigidBody && scoped.Find(1)->rigidBody.velocity.x==6 && scoped.Find(1)->hasCollider,"Borrowed live Physics hook lost new velocity/component");
 refused=false;try{hook.onUpdate(*scoped.Find(1),0);}catch(const LuaScriptError&){refused=true;}Need(refused && scoped.Find(1)->rigidBody.velocity.x==6,"Expired Physics scope admitted writes");
 refused=false;try{CreateLuaScriptFactory("scene.set_collider(1,1,1,1,false,0,0)","module Physics",{},defaults,{},{},view);}catch(const LuaScriptError&){refused=true;}Need(refused,"Module initializer admitted Physics writes");
 Write(root/"retired.lua","function on_update(e,dt) end function on_destroy(e) assert(not scene.set_rigid_body(e:id(),1,0,0,1,true,false));assert(scene.set_rigid_body(1,2,0,0,1,true,false)) end");
 EntityWorld removed;removed.CreateEntity("Peer");removed.CreateEntity("Retired");removed.SetScript(2,"retired.lua");ScriptRuntime retired;Load(retired,root,"retired.lua");retired.Update(removed,0);removed.DestroyEntity(2);retired.Update(removed,0);Need(!removed.Find(2) && retired.GetLastError(2).empty() && removed.Find(1)->rigidBody.velocity.x==2,"Retained Destroy resurrected self or lost peer Physics edit");retired.Shutdown(removed);
 std::cout<<"PASS: Lua Physics self/peer settings+queries/removal+readd, velocity order/clone, Create/Update/Fixed solver/live-retired Destroy, "<<failures.size()<<" error/type/range/quota/budget rollback cases, full Audio queue, readonly/write/expired/module scopes (no rendered host/pixels) retained="<<root<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
