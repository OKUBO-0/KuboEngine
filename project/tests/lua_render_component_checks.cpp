#include "engine/script/ScriptRuntime.h"
#include "engine/script/LuaScript.h"
#include <Windows.h>
#include <fstream>
#include <iostream>
static void Need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static void Write(const std::filesystem::path& path,const std::string& text){std::ofstream out(path,std::ios::binary);out<<text;out.close();Need(!out.fail(),"Render Lua fixture write failed");}
static void Load(Engine::Scripting::ScriptRuntime& runtime,const std::filesystem::path& root,const char* name){std::string error;Need(runtime.LoadAsset(name,root,error),error.c_str());}
int main(){using namespace Engine;using namespace Scene;using namespace Scripting;try{
 const auto root=std::filesystem::temp_directory_path()/(L"kubo-lua-render-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));Need(std::filesystem::create_directory(root),"Refuse existing render fixture");
 Write(root/"good.lua",R"(
 function on_create(e)
  local id=e:id();assert(scene.light(id)==nil and scene.material(id)==nil)
  assert(scene.set_light(id,{type='point',color={0.25,0.5,1},intensity=8,range=16}))
  assert(scene.set_material(id,{lighting=true,color={0.25,0.5,1,1},shininess=128,roughness=0.25,reflectionStrength=0.5,textureInfluence=0.75}))
  local l=scene.light(id);assert(l.type=='point' and l.enabled and l.intensity==8 and l.range==16 and l.color[2]==0.5)
  local m=scene.material(id);assert(m.lighting and m.shininess==128 and m.color[4]==1)
  local copy=assert(scene.clone(id));assert(copy==3 and scene.light(copy).intensity==8 and scene.material(copy).roughness==0.25)
  assert(scene.set_light(2,{type='spot',innerAngle=0.25,outerAngle=0.5}))
  assert(scene.remove_light(2) and scene.light(2)==nil and not scene.remove_light(2))
  assert(scene.set_light(2,{enabled=false}));assert(scene.light(2).type=='directional' and not scene.light(2).enabled)
  assert(not scene.set_light(999,{}) and not scene.remove_material(999))
 end
 function on_update(e,dt)
  local m=scene.material(e:id());m.roughness=0.5;assert(scene.set_material(e:id(),m));assert(scene.material(e:id()).roughness==0.5)
 end
 function on_fixed_update(e,dt)
  assert(scene.set_light(2,{type='spot',innerAngle=0.25,outerAngle=0.5}));assert(scene.light(2).type=='spot')
 end
 function on_destroy(e)
  assert(scene.remove_light(2));assert(scene.set_material(2,{}));assert(not scene.material(2).lighting)
 end
 )");
 EntityWorld world;world.CreateEntity("Self");world.CreateEntity("Peer");world.SetScript(1,"good.lua");ScriptRuntime runtime;Load(runtime,root,"good.lua");runtime.Update(world,0.1f);
 Need(runtime.GetLastError(1).empty() && world.Find(1)->hasLight && world.Find(1)->light.kind==LightKind::Point && world.Find(1)->material.roughness==0.5f && world.Find(3)->hasMaterial,"Lua staged/self/peer/clone render publication failed");world.RemoveScript(3);
 runtime.FixedUpdate(world,1.0f/60);Need(runtime.GetLastError(1).empty() && world.Find(2)->light.kind==LightKind::Spot,"Fixed render mutation failed");runtime.Shutdown(world);Need(!world.Find(2)->hasLight && world.Find(2)->hasMaterial && !world.Find(2)->material.lighting,"Destroy render mutation failed");
 const std::vector<std::string> failures={"error('rollback')","while true do end","local s=string.rep('x',16*1024*1024)",
  "scene.set_light(1,{type='sphere'})","scene.set_light(1,{type=1})","scene.set_light(1,{enabled=1})","scene.set_light(1,{intensity=-1e-100})","scene.set_light(1,{intensity=math.huge})",
  "scene.set_light(1,{range=0})","scene.set_light(1,{decay='2'})","scene.set_light(1,{innerAngle=1,outerAngle=0.5})","scene.set_light(1,{outerAngle=0/0})",
  "scene.set_light(1,{color={1,1}})","scene.set_light(1,{color={1,1,1,1}})","scene.set_light(1,{color={1,-1e-100,1}})","scene.set_light(1,{color={[1]=1,[3]=1}})","scene.set_light(1,{unknown=true})",
  "scene.set_material(1,{lighting='true'})","scene.set_material(1,{color={1,1,1}})","scene.set_material(1,{color={1,1,1,1,1}})","scene.set_material(1,{roughness=-1e-100})","scene.set_material(1,{reflectionStrength=1.1})",
  "scene.set_material(1,{textureInfluence=0/0})","scene.set_material(1,{shininess=0})","scene.set_material(1,{unknown=1})","scene.set_material(1,nil)","scene.set_light(1,{},1)","scene.material(1,2)",
  "for i=1,64 do scene.set_material(1,{}) end"};
 for(const auto& suffix:failures){
  Write(root/"bad.lua","function on_update(e,dt) e:translate(7,0,0);assert(scene.set_light(1,{type='point',intensity=8}));assert(scene.set_material(e:id(),{lighting=true}));audio.play('Assets/sound.wav');"+suffix+" end");
  EntityWorld entities;entities.CreateEntity("Target");entities.CreateEntity("Bad");entities.CreateEntity("Healthy");entities.SetScript(2,"bad.lua");entities.SetScript(3,"native");const auto high=entities.AllocationHighWatermark();ScriptRuntime failed;failed.SetAudioRequestsEnabled(true);Load(failed,root,"bad.lua");ScriptCallbacks healthy;healthy.onUpdate=[](EntityRecord& entity,float){entity.transform.position.x+=1;};Need(failed.Register("native",healthy),"Healthy peer registration failed");failed.Update(entities,0.1f);
  Need(!failed.GetLastError(2).empty() && !entities.Find(1)->hasLight && !entities.Find(2)->hasMaterial && entities.Find(2)->transform.position.x==0 && entities.Find(3)->transform.position.x==1 && failed.TakeAudioRequests().empty() && entities.AllocationHighWatermark()==high,"Failed render hook leaked World/self/Audio/IDs or blocked peer");failed.Shutdown(entities);
 }
 Write(root/"queue.lua","function on_update(e,dt) assert(scene.set_light(1,{intensity=e:id()}));assert(scene.set_material(e:id(),{lighting=true}));for i=1,32 do audio.play('Assets/sound.wav') end end");
 EntityWorld queue;queue.CreateEntity("Target");for(int i=0;i<9;++i){const auto id=queue.CreateEntity("Owner");queue.SetScript(id,"queue.lua");}ScriptRuntime queued;queued.SetAudioRequestsEnabled(true);Load(queued,root,"queue.lua");queued.Update(queue,0.1f);
 Need(queued.TakeAudioRequests().size()==256 && queue.Find(1)->light.intensity==9 && !queue.Find(10)->hasMaterial && !queued.GetLastError(10).empty(),"Audio admission failure published render prefix");queued.Shutdown(queue);
 auto view=std::make_shared<ScriptSceneView>();ScriptProperties defaults;auto factory=CreateLuaScriptFactory("function on_update(e,dt) assert(scene.set_light(1,{}));assert(scene.set_material(1,{})) end","render scope",{},defaults,{},{},view);auto hook=factory();EntityWorld scoped;scoped.CreateEntity("Self");bool refused=false;
 {ScriptSceneView::Scope scope(*view,scoped);try{hook.onUpdate(*scoped.Find(1),0);}catch(const LuaScriptError&){refused=true;}}Need(refused && !scoped.Find(1)->hasLight,"Readonly render mutation accepted");hook=factory();
 {ScriptSceneView::WriteScope scope(*view,scoped);hook.onUpdate(*scoped.Find(1),0);}Need(scoped.Find(1)->hasLight && scoped.Find(1)->hasMaterial,"Borrowed self render publication failed");
 refused=false;try{hook.onUpdate(*scoped.Find(1),0);}catch(const LuaScriptError&){refused=true;}Need(refused,"Expired render scope accepted");
 refused=false;try{CreateLuaScriptFactory("scene.set_light(1,{})","module render",{},defaults,{},{},view);}catch(const LuaScriptError&){refused=true;}Need(refused,"Module initializer mutated render data");
 std::cout<<"PASS: Lua Light/Material tables/defaults/full replacement/staged self+peer/clone/removal, Create/Update/Fixed/Destroy, "<<failures.size()<<" strict/error/quota rollback cases, Audio queue and readonly/write/expired/module scopes retained="<<root<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
