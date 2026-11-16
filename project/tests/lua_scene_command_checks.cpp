#include "engine/script/LuaScript.h"
#include <Windows.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace {
void Require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
void Write(const std::filesystem::path& file,const std::string& source){std::ofstream stream(file,std::ios::binary);stream<<source;stream.close();Require(!stream.fail(),"Command Lua fixture write failed");}
}
int main(){
 using namespace Engine::Scene;using namespace Engine::Scripting;
 const auto root=std::filesystem::temp_directory_path()/(L"kubo-lua-command-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
 try{
  std::filesystem::create_directories(root);std::string error;
  Write(root/"commands.lua",R"(
function on_create(e)
 e:translate(1,0,0);assert(scene.translate(e:id(),1,0,0));assert(scene.position(e:id())==2)
 assert(scene.translate(1,1,0,0) and scene.set_position(2,2,2,3) and scene.translate(2,1,0,0))
 assert(scene.set_rotation(2,0,0.2,0) and scene.set_scale(2,0,2,-1))
 assert(scene.set_active(2,true) and scene.active(2))
 assert(scene.set_velocity(2,7,8,9));local x,y,z=scene.velocity(2);assert(x==7 and y==8 and z==9)
 local x,y,z=scene.world_position(2);assert(math.abs(x-5)<0.001 and math.abs(y-14)<0.001 and z==33)
 assert(#scene.world_matrix(2)==16 and scene.scale(2)==0)
 assert(scene.set_position(e:id(),5,0,0));e:translate(1,0,0);assert(scene.position(e:id())==6)
 assert(scene.set_velocity(e:id(),4,0,0) and scene.velocity(e:id())==4)
end
function on_update(e,dt)
 assert(scene.translate(2,dt,0,0) and scene.set_active(2,false) and not scene.active(2))
 assert(scene.velocity(1)==nil and not scene.set_velocity(1,1,2,3))
 assert(not scene.set_position(9007199254740991,1,2,3))
 assert(scene.name(2)=='Child' and scene.parent(2)==1);e:translate(1,0,0)
end
function on_fixed_update(e,dt) assert(scene.translate(2,0,dt,0));e:translate(0,dt,0) end
function on_destroy(e)
 assert(scene.translate(1,2,0,0))
 if scene.exists(e:id()) then assert(scene.translate(e:id(),1,0,0))
 else assert(not scene.set_position(e:id(),99,0,0)) end
end
)");
  EntityWorld world;world.CreateEntity("Parent");world.CreateEntity("Child",1);const auto owner=world.CreateEntity("Reader");
  world.Find(1)->transform.position={10,20,30};world.Find(1)->transform.rotation={0,0,1.57079632679f};world.Find(1)->transform.scale={-2,3,1};world.Find(2)->active=false;
  world.SetRigidBody(2,{});world.SetRigidBody(owner,{});world.SetScript(owner,"commands.lua");ScriptRuntime runtime;
  Require(runtime.LoadAsset("commands.lua",root,error),error.c_str());runtime.Update(world,0.5f);
  Require(runtime.GetLastError(owner).empty() && world.Find(1)->transform.position.x==11 && world.Find(2)->transform.position.x==3.5f && world.Find(2)->transform.scale.z==-1 && world.Find(2)->transform.rotation.y==0.2f && !world.Find(2)->active && world.Find(2)->rigidBody.velocity.z==9 && world.Find(owner)->transform.position.x==7 && world.Find(owner)->rigidBody.velocity.x==4,"Create/update Scene command publication failed");
  runtime.FixedUpdate(world,0.25f);Require(runtime.GetLastError(owner).empty() && world.Find(2)->transform.position.y==2.25f && world.Find(owner)->transform.position.y==0.25f,"Fixed command publication failed");
  runtime.Shutdown(world);Require(world.Find(1)->transform.position.x==13 && world.Find(owner)->transform.position.x==8,"Live Destroy command failed");
  Write(root/"removed.lua","function on_update(e,dt) end\nfunction on_destroy(e) assert(not scene.exists(e:id()) and not scene.set_active(e:id(),true));assert(scene.translate(1,2,0,0)) end");
  world.SetScript(owner,"removed.lua");ScriptRuntime removed;Require(removed.LoadAsset("removed.lua",root,error),error.c_str());removed.Update(world,0);Require(world.DestroyEntity(owner),"Removed command fixture failed");removed.Update(world,0);
  Require(removed.GetLastError(owner).empty() && !world.Find(owner) && world.Find(1)->transform.position.x==15,"Removed Destroy resurrected self or lost peer command");removed.Shutdown(world);
  // Unhandled failures must discard every self/peer/audio candidate, and must
  // not stop a healthy native peer even if the failing hook exhausts Lua quota.
  for(const auto& failure:std::vector<std::string>{
   "error('explicit')","while true do end","local blob=string.rep('x',16*1024*1024)",
   "scene.set_position('1',1,0,0)","scene.set_position(0,1,0,0)","scene.set_position(1.5,1,0,0)",
   "scene.set_position(1,0/0,0,0)","scene.set_position(1,'1',0,0)","scene.set_active(1,1)",
   "scene.set_velocity(1,math.huge,0,0)","scene.set_rotation(1,true,0,0)",
   "scene.set_scale(1,1,0/0,1)","scene.set_position(1,3.4028234e38,0,0);scene.translate(1,3.4028234e38,0,0)",
   "for i=1,64 do scene.translate(1,1,0,0) end"}){
   Write(root/"failure.lua","function on_update(e,dt) assert(scene.translate(1,5,0,0));e:translate(7,0,0);audio.play('Assets/sound.wav');"+failure+" end");
   EntityWorld entities;entities.CreateEntity("Target");const auto failing=entities.CreateEntity("Failing"),healthy=entities.CreateEntity("Healthy");entities.SetScript(failing,"failure.lua");entities.SetScript(healthy,"healthy");
   ScriptRuntime failed;failed.SetAudioRequestsEnabled(true);Require(failed.LoadAsset("failure.lua",root,error),error.c_str());ScriptCallbacks callbacks;callbacks.onUpdate=[](EntityRecord& record,float){record.transform.position.x+=1;};Require(failed.Register("healthy",callbacks),"Healthy peer registration failed");
   failed.Update(entities,0.1f);Require(!failed.GetLastError(failing).empty() && entities.Find(1)->transform.position.x==0 && entities.Find(failing)->transform.position.x==0 && entities.Find(healthy)->transform.position.x==1 && failed.TakeAudioRequests().empty(),"Unhandled command fault partially published self/peer/audio or blocked healthy peer");failed.Shutdown(entities);
  }
  Write(root/"queue.lua","function on_update(e,dt) assert(scene.translate(1,1,0,0));e:translate(1,0,0);for i=1,32 do audio.play('Assets/a.wav') end end");
  ScriptRuntime bounded;bounded.SetAudioRequestsEnabled(true);Require(bounded.LoadAsset("queue.lua",root,error),error.c_str());EntityWorld many;many.CreateEntity("Target");std::vector<EntityId> owners;
  for(int index=0;index<9;++index){const auto id=many.CreateEntity("Owner");many.SetScript(id,"queue.lua");owners.push_back(id);}bounded.Update(many,0.1f);
  const auto audio=bounded.TakeAudioRequests();Require(audio.size()==256 && many.Find(1)->transform.position.x==8 && many.Find(owners.back())->transform.position.x==0 && !bounded.GetLastError(owners.back()).empty(),"Audio queue refusal published last peer/self or damaged successful earlier hooks");bounded.Shutdown(many);
  // Direct factories require explicit writable host scope; neither an ordinary
  // read scope nor an expired scope grants mutation permission.
  auto view=std::make_shared<ScriptSceneView>();auto input=std::make_shared<Engine::InputSystem::InputFrame>();ScriptProperties defaults;
  auto factory=CreateLuaScriptFactory("function on_update(e,dt) assert(scene.translate(1,1,0,0)) end","direct",input,defaults,{},{},view);
  EntityWorld direct;direct.CreateEntity("Target");const auto reader=direct.CreateEntity("Reader");auto hooks=factory();bool refused=false;
  {ScriptSceneView::Scope scope(*view,direct);try{hooks.onUpdate(*direct.Find(reader),0);}catch(const LuaScriptError&){refused=true;}}
  Require(refused && direct.Find(1)->transform.position.x==0,"Readonly direct factory mutated World");
  hooks=factory();{ScriptSceneView::WriteScope scope(*view,direct);hooks.onUpdate(*direct.Find(reader),0);}Require(direct.Find(1)->transform.position.x==1,"Explicit direct write scope failed");
  refused=false;try{hooks.onUpdate(*direct.Find(reader),0);}catch(const LuaScriptError&){refused=true;}Require(refused && direct.Find(1)->transform.position.x==1,"Expired direct scope mutated World");
  refused=false;try{CreateLuaScriptFactory("scene.set_active(1,false);function on_update(e,dt) end","module",input,defaults,{},{},view);}catch(const LuaScriptError&){refused=true;}Require(refused,"Module initializer Scene mutation accepted");
  std::cout<<"PASS: Lua Scene command Create/variable/fixed/live-removed Destroy, candidate/multiple-parent reads, strict/missing validation, unhandled error/instruction/memory/64-operation/audio-queue rollback with healthy peers and readonly/write/expired/module scope gates\n";
  std::cout<<"Retained fixtures: "<<root.string()<<'\n';return 0;
 }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<"; fixtures="<<root.string()<<'\n';return 1;}
}
