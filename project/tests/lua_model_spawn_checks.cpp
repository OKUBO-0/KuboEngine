#include "engine/script/ScriptRuntime.h"
#include "engine/script/LuaScript.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <cstdlib>
#include <new>
namespace {bool tracking=false;size_t allocations=0,failAt=0;}
void* operator new(size_t n){if(tracking && ++allocations==failAt)throw std::bad_alloc();if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{std::free(p);}void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,size_t) noexcept{std::free(p);}void operator delete[](void* p,size_t) noexcept{std::free(p);}
namespace {void Require(bool b,const char* why){if(!b)throw std::runtime_error(why);}void Write(const std::filesystem::path& p,const std::string& bytes){std::ofstream out(p,std::ios::binary);out<<bytes;out.close();Require(!out.fail(),"Fixture write");}}
int main(){using namespace Engine;using namespace Scripting;namespace fs=std::filesystem;const auto previous=fs::current_path();const auto root=fs::temp_directory_path()/(L"kubo-lua-model-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
try{
 fs::create_directories(root/L"Assets/日本語");fs::create_directories(root/"Unrelated");Write(root/L"Assets/日本語/モデル.obj","not GPU/import proof");
 Write(root/"Assets/model.lua",R"(
function on_create(e)
 local id=scene.spawn_model('生成','Assets/日本語/モデル.obj',e:id());assert(id==2 and scene.parent(id)==e:id())
 assert(scene.set_position(id,2,0,0) and scene.world_position(id)==2)
 assert(scene.spawn_model('Missing parent','Assets/日本語/モデル.obj',999)==nil)
end
function on_update(e,dt) e:translate(dt,0,0) end
function on_fixed_update(e,dt) local id=scene.spawn_model('Fixed','Assets/日本語/モデル.obj');assert(scene.destroy(id)) end
function on_destroy(e) assert(scene.spawn_model('Destroy','Assets/日本語/モデル.obj')) end
)");
 Assets::AssetRegistry registry;std::string error;Require(registry.Scan(root/"Assets",error),error.c_str());const auto guid=registry.FindByPath(root/L"Assets/日本語/モデル.obj")->guid;
 Scene::EntityWorld world;world.CreateEntity("Owner");world.SetScript(1,"Assets/model.lua");ScriptRuntime runtime;runtime.SetAssetOwner(registry,root);Require(runtime.LoadAsset("Assets/model.lua",root,error),error.c_str());fs::current_path(root/"Unrelated");
 runtime.Update(world,0.1f);Require(runtime.GetLastError(1).empty() && world.Find(2) && world.Find(2)->hasModel && world.Find(2)->model.assetGuid==guid && world.Find(1)->transform.position.x==0.1f,"Lua Create Model owner/cwd publication");
 runtime.FixedUpdate(world,0.1f);Require(runtime.GetLastError(1).empty() && world.GetEntities().size()==2,"Fixed Model generation/deletion");runtime.Shutdown(world);Require(runtime.GetLastError(1).empty() && world.GetEntities().size()==3 && world.Find(4)->name=="Destroy","Destroy Model asset owner");runtime.Clear();
 Write(root/"Assets/guid.lua","function on_update(e,dt) local id=scene.spawn_model('GUID','Assets/old.obj',0,'"+guid+"');assert(id and scene.exists(id)) end");
 Scene::EntityWorld gw;gw.CreateEntity("GUID owner");gw.SetScript(1,"Assets/guid.lua");ScriptRuntime gr;gr.SetAssetOwner(registry,root);Require(gr.LoadAsset("Assets/guid.lua",root,error),error.c_str());gr.Update(gw,0.1f);Require(gr.GetLastError(1).empty() && gw.Find(2)->model.assetGuid==guid && gw.Find(2)->model.assetPath=="Assets/日本語/モデル.obj","Lua explicit GUID stale hint");gr.Shutdown(gw);gr.Clear();
 for(const auto& failure:std::vector<std::string>{"error('after generation')","while true do end","local x=string.rep('x',16*1024*1024)","scene.spawn_model('Bad','../outside.obj')","scene.spawn_model('Bad','Assets/missing.obj')","scene.spawn_model('Bad','Assets/model.lua')","scene.spawn_model('Bad',123)","scene.spawn_model('Bad','Assets/日本語/モデル.obj',-1)","scene.spawn_model('Bad','Assets/日本語/モデル.obj',1.5)","scene.spawn_model('Bad','Assets/日本語/モデル.obj',0,true)","scene.spawn_model('Bad',string.char(255))","for i=1,64 do scene.spawn_model('Budget','Assets/日本語/モデル.obj') end"}){
  Write(root/"Assets/failure.lua","function on_update(e,dt) e:translate(7,0,0);audio.stop_all();assert(scene.spawn_model('Discarded','Assets/日本語/モデル.obj'));"+failure+" end");
  Scene::EntityWorld failed;failed.CreateEntity("Bad");failed.SetScript(1,"Assets/failure.lua");failed.CreateEntity("Healthy");failed.SetScript(2,"healthy");const auto high=failed.AllocationHighWatermark();ScriptRuntime fr;fr.SetAssetOwner(registry,root);fr.SetAudioRequestsEnabled(true);Require(fr.Register("healthy",{{},[](Scene::EntityRecord& e,float){e.transform.position.x+=1;},{}}) && fr.LoadAsset("Assets/failure.lua",root,error),"Failure assets");fr.Update(failed,0.1f);
  Require(!fr.GetLastError(1).empty() && failed.GetEntities().size()==2 && failed.AllocationHighWatermark()==high && failed.Find(1)->transform.position.x==0 && failed.Find(2)->transform.position.x==1 && fr.TakeAudioRequests().empty(),"Model fault leaked Scene/self/ID/Audio or blocked healthy peer");fr.Shutdown(failed);fr.Clear();
 }
 Scene::EntityWorld noOwner;noOwner.CreateEntity("No owner");noOwner.SetScript(1,"Assets/guid.lua");ScriptRuntime nr;Require(nr.LoadAsset("Assets/guid.lua",root,error),error.c_str());nr.Update(noOwner,0.1f);Require(!nr.GetLastError(1).empty() && noOwner.GetEntities().size()==1,"Missing asset owner allowed Lua generation");nr.Shutdown(noOwner);nr.Clear();
 Write(root/"Assets/queue.lua","function on_update(e,dt) e:translate(1,0,0);assert(scene.spawn_model('Queued','Assets/日本語/モデル.obj'));for i=1,32 do audio.stop_all() end end");
 Scene::EntityWorld queued;for(int i=0;i<9;++i){const auto id=queued.CreateEntity("Queue owner");queued.SetScript(id,"Assets/queue.lua");}ScriptRuntime qr;qr.SetAssetOwner(registry,root);qr.SetAudioRequestsEnabled(true);Require(qr.LoadAsset("Assets/queue.lua",root,error),error.c_str());qr.Update(queued,0.1f);
 Require(queued.GetEntities().size()==17 && queued.AllocationHighWatermark()==18 && queued.Find(8)->transform.position.x==1 && queued.Find(9)->transform.position.x==0 && !qr.GetLastError(9).empty() && qr.TakeAudioRequests().size()==256,"Full Audio queue leaked Model/self/allocator");qr.Shutdown(queued);qr.Clear();
 ScriptRuntime guarded;bool refused=false;guarded.SetAssetOwner(registry,root);Require(guarded.Register("guard",{{},[&](Scene::EntityRecord&,float){try{guarded.SetAssetOwner(registry,root);}catch(const std::logic_error&){refused=true;}},{}}),"Owner guard callback");Scene::EntityWorld guardWorld;guardWorld.CreateEntity("Guard");guardWorld.SetScript(1,"guard");guarded.Update(guardWorld,0.1f);Require(refused && guarded.GetLastError(1).empty(),"Asset owner replacement allowed during dispatch");guarded.Shutdown(guardWorld);guarded.Clear();
 auto view=std::make_shared<ScriptSceneView>();auto audio=std::make_shared<ScriptAudioRequests>();audio->SetEnabled(true);ScriptProperties defaults;ScriptAssetView assets(registry,root);
 auto factory=CreateLuaScriptFactory("function on_update(e,dt) e:translate(7,0,0);assert(scene.spawn_model('Allocated','Assets/日本語/モデル.obj'));audio.stop_all() end","Model allocation",{},defaults,audio,{},view);size_t positions=0;
 for(size_t position=0;;++position){Scene::EntityWorld live;live.CreateEntity(std::string(300,'n'));auto callbacks=factory();audio->Clear();const auto before=live.Capture().dump();const auto high=live.AllocationHighWatermark();ScriptSceneView::WriteScope scope(*view,live,&assets);
  allocations=0;failAt=position;tracking=true;bool fault=false;try{callbacks.onUpdate(*live.Find(1),0);}catch(...){fault=true;}tracking=false;failAt=0;const auto requests=audio->Take();
  if(position==0){positions=allocations;Require(!fault && positions>0 && live.Find(2)->hasModel && live.Find(1)->transform.position.x==7 && requests.size()==1,"Native Lua Model allocation baseline");}
  else Require(fault && live.Capture().dump()==before && live.AllocationHighWatermark()==high && requests.empty(),"Native Lua Model allocation fault leaked self/Scene/Audio/ID");if(position==positions)break;
 }
 fs::current_path(previous);std::cout<<"PASS: Lua Model generation Create/Update/Fixed/Destroy, explicit fixed asset owner/Unicode/GUID/cwd, strict/missing/type/quota/cap faults discard Scene/self/ID/Audio and isolate healthy peer; no-owner/replacement refusal, full Audio queue and "<<positions<<" ordinary hook allocation failures (no GPU/actual Editor/Player proof)\nRetained fixture: "<<root.string()<<'\n';return 0;
}catch(const std::exception& e){tracking=false;fs::current_path(previous);std::cerr<<"FAIL: "<<e.what()<<"; fixture="<<root.string()<<'\n';return 1;}}
