#include "engine/script/ScriptSceneCommands.h"
#include "engine/script/ScriptRuntime.h"
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <new>
namespace {bool tracking=false;size_t allocations=0,failAt=0;
void Need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void Write(const std::filesystem::path& p,const std::string& bytes){std::ofstream file(p,std::ios::binary);file<<bytes;file.close();Need(!file.fail(),"Fixture write failed");}}
void* operator new(size_t n){if(tracking && ++allocations==failAt)throw std::bad_alloc();if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{std::free(p);}void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,size_t) noexcept{std::free(p);}void operator delete[](void* p,size_t) noexcept{std::free(p);}
int main(){namespace fs=std::filesystem;using namespace Engine;using namespace Scripting;using R=ScriptSceneCommands::Result;using A=ScriptAssetView::Result;using Budget=ScriptPrefabWorkBudget;
 const auto root=fs::temp_directory_path()/(L"kubo-prefab-budget-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
 try{
  Budget bytes;Need(bytes.Bytes(Budget::MaxBytes) && !bytes.Bytes(1) && bytes.GetUsage().bytes==Budget::MaxBytes && bytes.GetUsage().exceeded && !bytes.Reference(),"Byte boundary/poison failed");
  Budget refs;for(size_t i=0;i<Budget::MaxReferences;++i)Need(refs.Reference(),"Reference boundary early");Need(!refs.Reference() && refs.GetUsage().references==Budget::MaxReferences,"Reference overflow accepted");
  Budget checks;Need(checks.FileChecks(Budget::MaxFileChecks) && !checks.FileChecks() && checks.GetUsage().fileChecks==Budget::MaxFileChecks,"Filesystem boundary failed");Budget overflow;Need(!overflow.Bytes(size_t(-1)) && overflow.GetUsage().bytes==0,"Byte count wrapped");
  fs::create_directories(root/"Assets");Write(root/"Assets/model.obj","v 0 0 0\n");Scene::EntityWorld source;const auto id=source.CreateEntity("Template");source.CreateEntity("Child",id);Need(source.SetModel(id,"Assets/model.obj"),"Model setup failed");
  Prefab::PrefabValue definition;std::string error;if(!Prefab::CapturePrefabSubtree(source,id,definition,error))throw std::runtime_error(error);const auto plain=definition.dump();Write(root/"Assets/small.prefab",plain);Need(plain.size()<2*1024*1024,"Fixture too large");const auto padded=plain+std::string(2*1024*1024-plain.size(),' ');Write(root/"Assets/heavy.prefab",padded);
  Assets::AssetRegistry registry;if(!registry.Scan(root/"Assets",error))throw std::runtime_error(error);ScriptAssetView assets(registry,root);
  Prefab::PrefabValue output;Budget initial;Need(assets.Prefab("Assets/small.prefab","",output,&initial)==A::Success && initial.GetUsage().bytes>plain.size() && initial.GetUsage().references==2 && initial.GetUsage().fileChecks>0,"Successful admission work not counted");const auto held=output.dump();
  Budget entityLimit;Need(assets.Prefab("Assets/small.prefab","",output,&entityLimit,1)==A::Limit && output.dump()==held && entityLimit.GetUsage().references==1 && !entityLimit.GetUsage().exceeded,"Oversized subtree reached nested references or mutated output");
  Need(assets.Prefab("Assets/small.prefab","",output,nullptr,0)==A::Invalid && output.dump()==held,"Invalid entity budget accepted");
  for(int kind=0;kind<3;++kind){Budget exhausted;if(kind==0)Need(exhausted.Bytes(Budget::MaxBytes),"Byte seed");else if(kind==1){for(size_t i=0;i<Budget::MaxReferences;++i)Need(exhausted.Reference(),"Reference seed");}else Need(exhausted.FileChecks(Budget::MaxFileChecks),"Check seed");
   Need(assets.Prefab("Assets/small.prefab","",output,&exhausted)==A::Limit && exhausted.GetUsage().exceeded && output.dump()==held,"Exhausted work budget changed output or fell back");}
  Scene::EntityWorld world;world.CreateEntity("Owner");auto self=*world.Find(1);ScriptSceneView view;ScriptSceneCommands commands;Scene::EntityId born=777;
  {ScriptSceneView::WriteScope scope(view,world,&assets);commands.Begin(&view,self);for(int i=0;i<32;++i)Need(commands.SpawnPrefab("Assets/small.prefab","",0,born)==R::Success,"64-node fixture quota early");const auto usage=commands.PrefabAdmissionUsage();born=777;
   tracking=true;allocations=0;failAt=1;const auto pf=commands.SpawnPrefab("Assets/missing.prefab","",0,born);const auto model=commands.SpawnModel("Missing","Assets/missing.obj","",0,born);tracking=false;failAt=0;
   Need(pf==R::Limit && model==R::Limit && allocations==0 && born==777 && commands.CreatedCount()==64 && commands.PrefabAdmissionUsage().bytes==usage.bytes && commands.PrefabAdmissionUsage().references==usage.references && commands.PrefabAdmissionUsage().fileChecks==usage.fileChecks,"Full creation quota performed admission/allocation or changed output");Need(commands.Prepare() && commands.PublishPrepared() && world.GetEntities().size()==65,"Native quota refusal lost existing prefix publication contract");}
  commands.Clear();Need(commands.PrefabAdmissionUsage().bytes==0 && commands.PrefabAdmissionUsage().references==0 && !commands.PrefabAdmissionUsage().exceeded,"Clear did not reset per-hook work");
  Scene::EntityWorld full;Scene::SceneValue snapshot;snapshot["version"]=1.0;snapshot["entities"]=crude_json::array{};const auto node=Scene::SceneValue::parse(R"({"id":1,"name":"Entity","parent":0,"active":true,"model":"","transform":{"position":[0,0,0],"rotation":[0,0,0],"scale":[1,1,1]}})");for(int i=1;i<=10000;++i){auto item=node;item["id"]=double(i);snapshot["entities"].push_back(std::move(item));}if(!full.Restore(snapshot,error))throw std::runtime_error(error);self=*full.Find(1);
  {ScriptSceneView::WriteScope scope(view,full,&assets);commands.Begin(&view,self);born=777;tracking=true;allocations=0;failAt=1;const auto result=commands.SpawnPrefab("Assets/missing.prefab","",0,born);tracking=false;failAt=0;Need(result==R::Limit && allocations==0 && born==777 && commands.PrefabAdmissionUsage().fileChecks==0,"Full World performed file admission or lazy World promotion");}
  Scene::EntityWorld native;native.CreateEntity("Native");self=*native.Find(1);const auto before=native.Capture().dump();const auto high=native.AllocationHighWatermark();
  {ScriptSceneView::WriteScope scope(view,native,&assets);commands.Begin(&view,self);Need(commands.Apply(1,ScriptSceneCommands::Operation::Translate,{7,0,0})==R::Success,"Prefix setup");for(int i=0;i<3;++i)Need(commands.SpawnPrefab("Assets/heavy.prefab","",0,born)==R::Success,"Native bytes budget early");born=777;Need(commands.SpawnPrefab("Assets/heavy.prefab","",0,born)==R::Limit && born==777 && commands.CreatedCount()==6 && commands.PrefabAdmissionUsage().exceeded && commands.PrefabAdmissionUsage().bytes<=Budget::MaxBytes && !commands.Prepare() && !commands.PublishPrepared() && native.Capture().dump()==before && native.AllocationHighWatermark()==high,"Native cumulative read limit leaked prefix/IDs or stayed publishable");}
  const std::string bad="function on_update(e,dt) e:translate(7,0,0);audio.stop_all();for i=1,4 do assert(scene.spawn_prefab('Assets/heavy.prefab')) end end";
  Write(root/"Assets/bad.lua",bad);Write(root/"Assets/healthy.lua","function on_update(e,dt) for i=1,3 do assert(scene.spawn_prefab('Assets/heavy.prefab')) end;e:translate(1,0,0) end");Scene::EntityWorld lua;lua.CreateEntity("Bad");lua.SetScript(1,"Assets/bad.lua");lua.CreateEntity("Healthy");lua.SetScript(2,"Assets/healthy.lua");ScriptRuntime runtime;runtime.SetAssetOwner(registry,root);runtime.SetAudioRequestsEnabled(true);if(!runtime.LoadAsset("Assets/bad.lua",root,error) || !runtime.LoadAsset("Assets/healthy.lua",root,error))throw std::runtime_error(error);
  // The healthy VM reads >6MiB each hook. Two hooks only succeed if its
  // budget resets, independently of the faulted VM (which is not retried).
  for(int frame=1;frame<=2;++frame){runtime.Update(lua,0.1f);Need(runtime.GetLastError(1).find("Prefab admission 8 MiB")!=std::string::npos && runtime.GetLastError(2).empty() && lua.GetEntities().size()==size_t(2+6*frame) && lua.AllocationHighWatermark()==Scene::EntityId(3+6*frame) && lua.Find(1)->transform.position.x==0 && lua.Find(2)->transform.position.x==float(frame) && runtime.TakeAudioRequests().empty(),"Lua native admission cap leaked World/self/ID/Audio or blocked/reset a peer");}runtime.Shutdown(lua);runtime.Clear();
  Need(fs::file_size(root/"Assets/heavy.prefab")==padded.size(),"Work limit rewrote Prefab source");
  std::cout<<"PASS: per-hook Prefab 8MiB/512-reference/4096-check counters, boundary+overflow/output isolation, zero-allocation creation/World preflight, nested-count early rejection, cumulative native/Lua limit rollback and healthy peer/reset (not hard OS time/RSS or Model byte budget)\n";return 0;
 }catch(const std::exception& e){tracking=false;failAt=0;std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
