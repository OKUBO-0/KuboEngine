#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "engine/script/ScriptRuntime.h"
#include "engine/script/LuaScript.h"
#include <Windows.h>
#include <fstream>
#include <iostream>
static void Need(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static void Write(const std::filesystem::path& path,const std::string& text){std::ofstream out(path,std::ios::binary);out<<text;out.close();Need(!out.fail(),"Lua UI fixture write failed");}
static void Load(Engine::Scripting::ScriptRuntime& runtime,const std::filesystem::path& root,const char* file){std::string error;Need(runtime.LoadAsset(file,root,error),error.c_str());}
int main(){using namespace Engine;using namespace Scene;using namespace Scripting;std::filesystem::path root;const auto previous=std::filesystem::current_path();try{
 root=std::filesystem::temp_directory_path()/(L"kubo-lua-ui-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));Need(std::filesystem::create_directory(root),"Refuse existing Lua UI fixture");std::filesystem::create_directory(root/"Assets");std::filesystem::create_directory(root/"Unrelated");Write(root/L"Assets/白.png","identity fixture, not decoding");Write(root/"Assets/wrong.lua","return {}");
 Write(root/"Assets/good.lua",R"(
 function on_create(e)
  local id=e:id();assert(scene.canvas(id)==nil and scene.sprite(id)==nil)
  assert(scene.set_canvas(id,{referenceSize={640,360},scaleMode='constantPixels',sortOrder=-3}))
  assert(scene.set_sprite(id,{asset='Assets/白.png',size={120,80},pivot={0.25,0.75},anchor={1,0.5},color={0.25,0.5,1,0.75},flipX=true,sortOrder=12}))
  local c=scene.canvas(id);assert(c.enabled and c.referenceSize[1]==640 and c.scaleMode=='constantPixels' and c.sortOrder==-3)
  local s=scene.sprite(id);assert(s.asset=='Assets/白.png' and #s.guid==32 and s.size[1]==120 and s.flipX and s.color[4]==0.75 and s.anchor[1]==1 and s.anchor[2]==0.5)
  s.size[1]=999;assert(scene.sprite(id).size[1]==120)
  local clone=assert(scene.clone(id));assert(clone==3 and scene.canvas(clone).sortOrder==-3 and scene.sprite(clone).flipX)
  assert(scene.set_canvas(2,{enabled=false}));assert(scene.canvas(2).referenceSize[1]==1280 and not scene.canvas(2).enabled)
  assert(scene.set_sprite(2,{}));assert(scene.sprite(2).asset=='' and scene.sprite(2).guid==nil)
  assert(scene.remove_sprite(2) and scene.sprite(2)==nil and not scene.remove_sprite(2))
  assert(not scene.set_canvas(999,{}) and not scene.remove_canvas(999))
 end
 function on_update(e,dt)
  local s=scene.sprite(e:id());s.color[1]=0.5;assert(scene.set_sprite(e:id(),s));assert(scene.sprite(e:id()).size[1]==120 and scene.sprite(e:id()).anchor[2]==0.5)
 end
 function on_fixed_update(e,dt) assert(scene.set_canvas(2,{sortOrder=42}));assert(scene.canvas(2).enabled) end
 function on_destroy(e) assert(scene.remove_canvas(2));assert(scene.set_sprite(2,{})) end
 )");
 Assets::AssetRegistry registry;std::string error;Need(registry.Scan(root/"Assets",error),error.c_str());const auto guid=registry.FindByPath(root/L"Assets/白.png")->guid,wrongGuid=registry.FindByPath(root/"Assets/wrong.lua")->guid;
 EntityWorld world;world.CreateEntity("Self");world.CreateEntity("Peer");world.SetScript(1,"Assets/good.lua");ScriptRuntime runtime;runtime.SetAssetOwner(registry,root);Load(runtime,root,"Assets/good.lua");std::filesystem::current_path(root/"Unrelated");runtime.Update(world,0.1f);
 Need(runtime.GetLastError(1).empty() && world.Find(1)->hasCanvas && world.Find(1)->hasSprite && world.Find(1)->sprite.assetGuid==guid && world.Find(1)->sprite.color.x==0.5f && world.Find(3)->sprite.size.x==120,"UI staged self/peer/clone/owner/cwd publication");world.RemoveScript(3);
 runtime.FixedUpdate(world,0.1f);Need(runtime.GetLastError(1).empty() && world.Find(2)->canvas.sortOrder==42 && world.Find(2)->canvas.referenceSize.x==1280,"UI Fixed/default full replacement");runtime.Shutdown(world);Need(!world.Find(2)->hasCanvas && world.Find(2)->hasSprite && world.Find(2)->sprite.assetPath.empty(),"UI Destroy publication");runtime.Clear();std::filesystem::current_path(previous);
 std::vector<std::string> failures={"error('rollback')","while true do end","local s=string.rep('x',16*1024*1024)",
  "scene.set_canvas(1,{enabled=1})","scene.set_canvas(1,{referenceSize={0,720}})","scene.set_canvas(1,{referenceSize={640}})","scene.set_canvas(1,{referenceSize={[1]=640,[3]=720}})","scene.set_canvas(1,{scaleMode=''})","scene.set_canvas(1,{scaleMode=1})","scene.set_canvas(1,{sortOrder=1e-100})","scene.set_canvas(1,{sortOrder=32768})","scene.set_canvas(1,{unknown=true})",
  "scene.set_sprite(1,{enabled=1})","scene.set_sprite(1,{size={-1e-100,1}})","scene.set_sprite(1,{size={16385,1}})","scene.set_sprite(1,{pivot={0,17}})","scene.set_sprite(1,{color={1,1,1}})","scene.set_sprite(1,{color={1,1,1,1,1}})","scene.set_sprite(1,{color={1,1,1,-1e-100}})","scene.set_sprite(1,{flipX=1})","scene.set_sprite(1,{sortOrder=0.5})","scene.set_sprite(1,{sortOrder=0/0})",
  "scene.set_sprite(1,{asset='../escape.png'})","scene.set_sprite(1,{asset='Assets/CON.png'})","scene.set_sprite(1,{asset='Assets/missing.png'})","scene.set_sprite(1,{asset='Assets/白.png',guid='ffffffffffffffff'})","scene.set_sprite(1,{asset='Assets/白.png',guid='"+wrongGuid+"'})","scene.set_sprite(1,{asset=3})","scene.set_sprite(1,{guid=false})","scene.set_sprite(1,{unknown=true})","scene.set_sprite(1,nil)","scene.set_canvas(1,{},1)","scene.sprite(1,2)","for i=1,64 do scene.set_sprite(1,{}) end"};
 for(const auto& suffix:failures){
  Write(root/"Assets/bad.lua","function on_update(e,dt) e:translate(7,0,0);assert(scene.set_canvas(1,{}));assert(scene.set_sprite(e:id(),{asset='Assets/白.png'}));audio.play('Assets/sound.wav');"+suffix+" end");
  EntityWorld entities;entities.CreateEntity("Target");entities.CreateEntity("Bad");entities.CreateEntity("Healthy");entities.SetScript(2,"Assets/bad.lua");entities.SetScript(3,"native");const auto high=entities.AllocationHighWatermark();ScriptRuntime failed;failed.SetAssetOwner(registry,root);failed.SetAudioRequestsEnabled(true);Load(failed,root,"Assets/bad.lua");ScriptCallbacks healthy;healthy.onUpdate=[](EntityRecord& value,float){value.transform.position.x+=1;};Need(failed.Register("native",healthy),"Healthy registration");failed.Update(entities,0.1f);
  Need(!failed.GetLastError(2).empty() && !entities.Find(1)->hasCanvas && !entities.Find(2)->hasSprite && entities.Find(2)->transform.position.x==0 && entities.Find(3)->transform.position.x==1 && failed.TakeAudioRequests().empty() && entities.AllocationHighWatermark()==high,"UI failed hook leaked self/peer/Audio/IDs or blocked healthy peer");failed.Shutdown(entities);
 }
 // Runtime Prefab admission must resolve Sprite assets too, not merely preserve
 // their JSON while allowing a bad typed reference into a spawned subtree.
 EntityWorld templateWorld;const auto templateId=templateWorld.CreateEntity("UI template");templateWorld.SetCanvas(templateId,{});SpriteComponent templateSprite;templateSprite.assetGuid=guid;templateSprite.assetPath="Assets/stale.png";templateWorld.SetSprite(templateId,templateSprite);Prefab::PrefabValue definition;Need(Prefab::CapturePrefabSubtree(templateWorld,templateId,definition,error),error.c_str());Write(root/"Assets/ui.prefab",definition.dump());
 auto invalid=definition;invalid["world"]["entities"][0]["sprite"]["guid"]=wrongGuid;Write(root/"Assets/bad.prefab",invalid.dump());Need(registry.Scan(root/"Assets",error),error.c_str());
 Write(root/"Assets/spawn.lua","function on_update(e,dt) local id=assert(scene.spawn_prefab('Assets/ui.prefab'));assert(scene.sprite(id).guid=='"+guid+"' and scene.sprite(id).asset=='Assets/白.png') end");
 EntityWorld spawned;spawned.SetScript(spawned.CreateEntity("Owner"),"Assets/spawn.lua");ScriptRuntime spawning;spawning.SetAssetOwner(registry,root);Load(spawning,root,"Assets/spawn.lua");spawning.Update(spawned,0.1f);Need(spawning.GetLastError(1).empty() && spawned.Find(2) && spawned.Find(2)->hasSprite,"UI Prefab typed admission/canonical hint");spawning.Shutdown(spawned);
 Write(root/"Assets/spawn-bad.lua","function on_update(e,dt) assert(scene.set_canvas(1,{}));scene.spawn_prefab('Assets/bad.prefab') end");EntityWorld refused;refused.SetScript(refused.CreateEntity("Owner"),"Assets/spawn-bad.lua");ScriptRuntime refusing;refusing.SetAssetOwner(registry,root);Load(refusing,root,"Assets/spawn-bad.lua");refusing.Update(refused,0.1f);Need(!refusing.GetLastError(1).empty() && refused.GetEntities().size()==1 && !refused.Find(1)->hasCanvas && refused.AllocationHighWatermark()==2,"Bad Prefab Sprite published prefix/IDs");refusing.Shutdown(refused);
 auto view=std::make_shared<ScriptSceneView>();ScriptProperties defaults;auto factory=CreateLuaScriptFactory("function on_update(e,dt) assert(scene.set_canvas(1,{}));assert(scene.set_sprite(1,{})) end","UI scopes",{},defaults,{},{},view);auto hook=factory();EntityWorld scoped;scoped.CreateEntity("Self");bool rejected=false;
 {ScriptSceneView::Scope scope(*view,scoped);try{hook.onUpdate(*scoped.Find(1),0);}catch(const LuaScriptError&){rejected=true;}}Need(rejected && !scoped.Find(1)->hasCanvas,"Readonly UI write accepted");hook=factory();
 {ScriptSceneView::WriteScope scope(*view,scoped);hook.onUpdate(*scoped.Find(1),0);}Need(scoped.Find(1)->hasCanvas && scoped.Find(1)->hasSprite,"Borrowed UI write failed");
 rejected=false;try{hook.onUpdate(*scoped.Find(1),0);}catch(const LuaScriptError&){rejected=true;}Need(rejected,"Expired UI scope accepted");rejected=false;try{CreateLuaScriptFactory("scene.set_sprite(1,{})","module UI",{},defaults,{},{},view);}catch(const LuaScriptError&){rejected=true;}Need(rejected,"Module UI write accepted");
 std::filesystem::current_path(previous);std::filesystem::remove_all(root);root.clear();std::cout<<"PASS: Lua Canvas/Sprite copy tables/full defaults/strict integer+vectors+fields, staged self/peer/clone/Create/Update/Fixed/Destroy, Unicode owner/cwd/typed texture and Prefab admission, "<<failures.size()<<" strict/error/quota+Audio rollback cases and readonly/write/expired/module scopes (not actual Player/pixels)\n";return 0;
}catch(const std::exception& e){std::filesystem::current_path(previous);std::cerr<<"FAIL: "<<e.what()<<'\n';if(!root.empty())std::cerr<<"LUA_UI_FIXTURE_RETAINED="<<root<<'\n';return 1;}}
