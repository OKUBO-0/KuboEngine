#include "engine/script/ScriptRuntime.h"
#include "engine/script/LuaScript.h"
#include <Windows.h>
#include <fstream>
#include <iostream>
using namespace Engine;using namespace Engine::Scene;using namespace Engine::Scripting;
namespace {
void Need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void Write(const std::filesystem::path& path,const std::string& text){std::ofstream file(path,std::ios::binary);file<<text;Need(bool(file),"Lua query fixture write failed");}
void Load(ScriptRuntime& runtime,const std::filesystem::path& root,const char* name){std::string error;if(!runtime.LoadAsset(name,root,error))throw std::runtime_error(error);}
void Setup(EntityWorld& world,const char* script){world.CreateEntity("Self");world.CreateEntity("Target");world.CreateEntity("Parent");world.SetParent(2,3);world.SetCollider(2,{});world.Find(2)->transform.position={10,0,0};world.SetScript(1,script);}
}
int main(){try{
 const auto root=std::filesystem::temp_directory_path()/(L"kubo-lua-query-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));Need(std::filesystem::create_directory(root),"Fresh Lua query fixture required");
 Write(root/"success.lua",R"(
 function on_create(e)
  local id,d,x,y,z,nx,ny,nz,tr=scene.raycast(0,0,0,2,0,0,20,e:id())
  assert(id==2 and d==9.5 and x==9.5 and y==0 and nx==-1 and ny==0 and nz==0 and not tr)
  assert(scene.set_position(3,2,0,0));assert(scene.set_position(2,3,0,0))
  assert(select(2,scene.raycast(0,0,0,1,0,0,20))==4.5)
  assert(scene.set_collider(2,1,1,1,true,0,0))
  id,d=scene.raycast(0,0,0,1,0,0,20);assert(id==2 and d==4)
  local hits=scene.overlap_box(5,0,0,0,0,0);assert(#hits==1 and hits[1]==2)
  assert(#scene.overlap_box(5,0,0,1,1,1,0,false)==0)
  assert(scene.set_active(3,false));assert(scene.raycast(0,0,0,1,0,0,20)==nil)
  assert(scene.set_active(3,true));assert(scene.remove_collider(2));assert(scene.raycast(0,0,0,1,0,0,20)==nil)
  assert(scene.set_collider(2,1,1,1,false,0,0))
  local clone=scene.clone(2,0);assert(clone and scene.raycast(0,0,0,1,0,0,20)==clone)
  assert(scene.destroy(clone));assert(scene.raycast(0,0,0,1,0,0,20)==2)
 end
 function on_update(e,dt)
  for i=1,64 do assert(scene.raycast(0,0,0,1,0,0,20)==2) end
  e:translate(0,1,0)
 end
 function on_fixed_update(e,dt) assert(scene.overlap_box(5,0,0,0,0,0)[1]==2);e:translate(1,0,0) end
 function on_destroy(e) assert(scene.raycast(0,0,0,1,0,0,20)==2) end
 )");
 EntityWorld world;Setup(world,"success.lua");ScriptRuntime runtime;Load(runtime,root,"success.lua");runtime.Update(world,0.1f);
 Need(runtime.GetLastError(1).empty() && world.Find(2)->transform.position.x==3 && world.Find(3)->transform.position.x==2 && world.Find(1)->transform.position.y==1,"Lua staged ray/overlap lifecycle failed");
 runtime.Update(world,0.1f);Need(runtime.GetLastError(1).empty() && world.Find(1)->transform.position.y==2,"Query budget failed to reset between hooks");runtime.FixedUpdate(world,0.01f);Need(runtime.GetLastError(1).empty() && world.Find(1)->transform.position.x==1,"Fixed query failed");runtime.Shutdown(world);
 const std::vector<std::string> errors={"error('rollback')","scene.raycast(0,0,0,0,0,0,10)","scene.raycast(0,0,0,1,0,0,-1)","scene.raycast('0',0,0,1,0,0,10)","scene.raycast(0,0,0,1,0,0,math.huge)","scene.raycast(0,0,0,1,0,0,10,-1)","scene.raycast(0,0,0,1,0,0,10,0,1)","scene.overlap_box(0,0,0,-1e-100,1,1)","scene.overlap_box(0/0,0,0,1,1,1)","scene.overlap_box(0,0,0,1,1,1,0,'true')","scene.raycast(1,2)","for i=1,65 do scene.raycast(0,0,0,1,0,0,100) end"};
 for(const auto& suffix:errors){
  Write(root/"bad.lua","function on_update(e,dt) e:translate(7,0,0);scene.translate(2,7,0,0);audio.play('Assets/sound.wav');"+suffix+" end");
  EntityWorld failed;Setup(failed,"bad.lua");auto good=failed.CreateEntity("Healthy");failed.SetScript(good,"native");ScriptRuntime scripts;scripts.SetAudioRequestsEnabled(true);Load(scripts,root,"bad.lua");ScriptCallbacks peer;peer.onUpdate=[](EntityRecord& e,float){e.transform.position.y+=1;};Need(scripts.Register("native",peer),"Healthy registration failed");scripts.Update(failed,0.1f);
  Need(!scripts.GetLastError(1).empty() && failed.Find(1)->transform.position.x==0 && failed.Find(2)->transform.position.x==10 && failed.Find(good)->transform.position.y==1 && scripts.TakeAudioRequests().empty(),"Failed query leaked self/peer/Audio prefix or blocked healthy peer");scripts.Shutdown(failed);
 }
 // Native work and result caps fail the complete hook, not a prefix or truncation.
 for(bool hitLimit:{false,true}){
  Write(root/"limit.lua",hitLimit?"function on_update(e,dt) e:translate(7,0,0);audio.play('a.wav');scene.overlap_box(0,0,0,100,100,100) end":"function on_update(e,dt) e:translate(7,0,0);audio.play('a.wav');for i=1,64 do scene.raycast(0,0,0,1,0,0,100) end end");
  EntityWorld limited;Setup(limited,"limit.lua");const size_t count=hitLimit?1028:10000;
  while(limited.GetEntities().size()<count){const auto id=limited.CreateEntity("Extra");if(hitLimit)limited.SetCollider(id,{});}
  ScriptRuntime scripts;scripts.SetAudioRequestsEnabled(true);Load(scripts,root,"limit.lua");scripts.Update(limited,0);
  Need(scripts.GetLastError(1).find("Physics query budget exceeded")!=std::string::npos && limited.Find(1)->transform.position.x==0 && scripts.TakeAudioRequests().empty(),"Native work/hit cap failed to isolate hook");scripts.Shutdown(limited);
 }
 auto view=std::make_shared<ScriptSceneView>();ScriptProperties defaults;auto factory=CreateLuaScriptFactory("function on_update(e,dt) assert(scene.raycast(0,0,0,1,0,0,20)==2) end","query scope",{},defaults,{},{},view);auto hook=factory();EntityWorld scoped;Setup(scoped,"none");
 {ScriptSceneView::Scope scope(*view,scoped);hook.onUpdate(*scoped.Find(1),0);}bool refused=false;try{hook.onUpdate(*scoped.Find(1),0);}catch(const LuaScriptError&){refused=true;}Need(refused,"Expired query scope accepted");
 refused=false;try{CreateLuaScriptFactory("scene.raycast(0,0,0,1,0,0,20)","module query",{},defaults,{},{},view);}catch(const LuaScriptError&){refused=true;}Need(refused,"Module initializer queried a World");
 std::cout<<"PASS: Lua current/staged hierarchical ray/overlap, clone/destroy/Collider edits, Create/Update/Fixed/Destroy, reset/readonly/expired/module scopes, "<<errors.size()<<" type/range/query-count rollback cases plus native work/hit caps and healthy peer/Audio isolation\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
