#include "engine/script/LuaScript.h"
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
void Require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void Write(const std::filesystem::path& path,const std::string& bytes){std::ofstream out(path,std::ios::binary);out<<bytes;out.close();Require(!out.fail(),"Scene-view fixture write failed");}
}
int main(){
    using namespace Engine::Scene;using namespace Engine::Scripting;
    const auto fixture=std::filesystem::temp_directory_path()/(L"kubo-lua-scene-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try {
        EntityWorld world,other;
        auto view=std::make_shared<ScriptSceneView>();Require(!view->World(),"Scene view retained initial World");
        {ScriptSceneView::Scope outer(*view,world);Require(view->World()==&world,"Scope not installed");
            try{ScriptSceneView::Scope inner(*view,other);Require(view->World()==&other,"Nested scope not installed");throw std::runtime_error("sentinel");}catch(const std::runtime_error&){ }
            Require(view->World()==&world,"Exception failed to restore nested scope");}
        Require(!view->World(),"Scope retained borrowed World");
        std::filesystem::create_directories(fixture);
        Write(fixture/"read.lua",R"(
function on_create(e)
  assert(scene.entity_count()==8 and scene.find('duplicate')==3 and scene.find('missing')==nil)
  assert(scene.find('n\0m')==6 and #scene.name(6)==3)
  assert(scene.find('隠れた物体')==5 and scene.active(5)==false)
  assert(scene.name(e:id())=='Reader' and scene.parent(2)==1 and scene.parent(1)==0)
  local x,y,z=scene.position(2);assert(x==2 and y==3 and z==4)
  local x,y,z=scene.scale(2);assert(x==-1 and y==2 and z==3)
  local x,y,z=scene.rotation(2);assert(math.abs(x-0.1)<0.001 and math.abs(y-0.2)<0.001 and math.abs(z-0.3)<0.001)
  assert(not scene.exists(9007199254740991))
  e:translate(1,0,0)
  local x,y,z=scene.position(e:id());assert(x==1 and y==0 and z==0)
end
function on_update(e,dt)
  local target=scene.find('Target')
  if target then local x=scene.position(target);e:translate(x,scene.entity_count(),0)
  else
    assert(not scene.exists(2) and scene.position(2)==nil and scene.rotation(2)==nil and scene.scale(2)==nil)
    assert(scene.name(2)==nil and scene.parent(2)==nil and scene.active(2)==nil)
    e:translate(0,scene.entity_count(),0)
  end
end
function on_fixed_update(e,dt) assert(scene.exists(e:id()));e:translate(0,0,3) end
function on_destroy(e)
  e:translate(0,10,0)
  if scene.exists(e:id()) then local x,y,z=scene.position(e:id());local a,b,c=e:position();assert(y==b)
  else assert(scene.position(e:id())==nil and scene.name(e:id())==nil) end
end
)");
        const auto parent=world.CreateEntity("Parent"),target=world.CreateEntity("Target",parent);
        world.Find(parent)->transform.position={10,0,0};world.Find(target)->transform.position={2,3,4};world.Find(target)->transform.rotation={0.1f,0.2f,0.3f};world.Find(target)->transform.scale={-1,2,3};
        world.CreateEntity("duplicate");world.CreateEntity("duplicate");const auto hidden=world.CreateEntity("隠れた物体",parent);world.Find(hidden)->active=false;world.CreateEntity(std::string("n\0m",3));
        const auto a=world.CreateEntity("Reader"),b=world.CreateEntity("Reader");world.SetScript(a,"read");world.SetScript(b,"read");
        const auto unchangedTarget=world.Find(target)->transform;ScriptRuntime runtime;std::string error;
        auto reordered=world.Capture();auto& records=reordered["entities"].get<crude_json::array>();std::reverse(records.begin(),records.end());Require(world.Restore(reordered,error),error.c_str());
        Require(runtime.LoadResolvedAsset("read",fixture/"read.lua",error),error.c_str());
        runtime.Update(world,0.01f);for(const auto id:{a,b})Require(runtime.GetLastError(id).empty() && world.Find(id)->transform.position.x==3 && world.Find(id)->transform.position.y==8,"Scene create/update/local/self-candidate reads failed");
        runtime.FixedUpdate(world,1.0f/60);for(const auto id:{a,b})Require(runtime.GetLastError(id).empty() && world.Find(id)->transform.position.z==3,"Scene fixed view unavailable");
        Require(world.Find(target)->transform.position.x==unchangedTarget.position.x && world.Find(parent)->transform.position.x==10 && world.Find(hidden)->active==false,"Read API changed another Entity");
        Require(world.DestroyEntity(target),"Target removal failed");runtime.Update(world,0.01f);Require(runtime.GetLastError(a).empty() && world.Find(a)->transform.position.y==15,"Deleted target ID was retained or missing result wrong");
        world.Find(a)->script.enabled=false;runtime.Update(world,0.01f);Require(runtime.GetLastError(a).empty() && world.Find(a)->transform.position.y==25,"Live destroy view/self candidate missing");
        Require(world.DestroyEntity(b),"Reader removal failed");runtime.Update(world,0.01f);Require(runtime.GetLastError(b).empty() && !world.Find(b),"Removed destroy record appeared as live Entity");runtime.Shutdown(world);runtime.Clear();
        EntityWorld hierarchy;const auto root=hierarchy.CreateEntity("Root"),child=hierarchy.CreateEntity("Child",root),leaf=hierarchy.CreateEntity("Leaf",child),observer=hierarchy.CreateEntity("Observer");
        hierarchy.Find(root)->transform.position={10,20,30};hierarchy.Find(root)->transform.rotation={0,0,1.57079632679f};hierarchy.Find(root)->transform.scale={-2,3,1};
        hierarchy.Find(child)->transform.position={1,2,3};hierarchy.Find(child)->transform.rotation={0,0.3f,0};hierarchy.Find(child)->transform.scale={0.5f,2,1};hierarchy.Find(leaf)->transform.position={2,0,0};hierarchy.Find(observer)->transform.position={7,0,0};
        Matrix4x4 composed{};Require(hierarchy.GetWorldMatrix(child,composed) && std::abs(composed.m[3][0]-4)<0.001f && std::abs(composed.m[3][1]-18)<0.001f && composed.m[3][2]==33,"Analytic rotated/mirrored parent composition wrong");
        const auto beforeHierarchy=hierarchy.Capture().dump();auto overlay=*hierarchy.Find(root);overlay.transform.position.x+=1;overlay.parent=999;
        Require(hierarchy.GetWorldMatrix(child,composed,overlay) && std::abs(composed.m[3][0]-5)<0.001f && std::abs(composed.m[3][1]-18)<0.001f && hierarchy.Capture().dump()==beforeHierarchy,"Candidate TRS changed live hierarchy or used candidate parent");
        Matrix4x4 unchanged{};unchanged.m[0][0]=42;auto missing=overlay;missing.id=999;Require(!hierarchy.GetWorldMatrix(child,unchanged,missing) && unchanged.m[0][0]==42,"Missing override published failed output");
        Write(fixture/"world.lua",R"(
function on_update(e,dt)
  local x,y,z=scene.world_position(2);assert(math.abs(x-4)<0.001 and math.abs(y-18)<0.001 and math.abs(z-33)<0.001)
  local m=scene.world_matrix(2);assert(#m==16 and m[13]==x and m[14]==y and m[15]==z and m[16]==1)
  local expected={0,-math.cos(0.3),-0.5*math.sin(0.3),0,-6,0,0,0,0,-2*math.sin(0.3),math.cos(0.3),0,4,18,33,1}
  for i=1,16 do assert(math.abs(m[i]-expected[i])<0.001) end
  local lx,ly,lz=scene.world_position(3);assert(math.abs(lx-4)<0.001 and math.abs(ly-(18-2*math.cos(0.3)))<0.001 and math.abs(lz-(33-math.sin(0.3)))<0.001)
  e:translate(1,0,0)
  local a,b,c=scene.world_position(2);assert(math.abs(a-5)<0.001 and math.abs(b-18)<0.001 and math.abs(c-33)<0.001)
  local n=scene.world_matrix(2);for i=1,12 do assert(math.abs(n[i]-m[i])<0.0001) end
  assert(math.abs(n[13]-m[13]-1)<0.001)
  local own=scene.world_position(e:id());assert(own==11)
  assert(scene.world_position(999)==nil and scene.world_matrix(999)==nil)
end
)");
        hierarchy.SetScript(root,"world");ScriptRuntime composedRuntime;Require(composedRuntime.LoadResolvedAsset("world",fixture/"world.lua",error),error.c_str());composedRuntime.Update(hierarchy,0.01f);
        Require(composedRuntime.GetLastError(root).empty() && hierarchy.Find(root)->transform.position.x==11 && hierarchy.Find(child)->transform.position.x==1,"Lua world position/matrix/self ancestor candidate failed");composedRuntime.Shutdown(hierarchy);composedRuntime.Clear();
        auto hierarchyView=std::make_shared<ScriptSceneView>();ScriptProperties matrixDefaults;
        auto invalidFactory=CreateLuaScriptFactory("function on_update(e,dt) assert(scene.world_position(2)==nil and scene.world_matrix(2)==nil) end","invalid hierarchy",{},matrixDefaults,{}, {},hierarchyView);
        auto invalidCallbacks=invalidFactory();hierarchy.Find(root)->parent=child;
        {ScriptSceneView::Scope scope(*hierarchyView,hierarchy);invalidCallbacks.onUpdate(*hierarchy.Find(root),0.01f);}
        Require(!hierarchy.GetWorldMatrix(child,unchanged) && unchanged.m[0][0]==42,"Cycle published partial matrix");
        hierarchy.Find(root)->parent=999;{ScriptSceneView::Scope scope(*hierarchyView,hierarchy);invalidCallbacks.onUpdate(*hierarchy.Find(root),0.01f);}
        hierarchy.Find(root)->parent=0;hierarchy.Find(root)->transform.scale.x=std::numeric_limits<float>::infinity();
        {ScriptSceneView::Scope scope(*hierarchyView,hierarchy);invalidCallbacks.onUpdate(*hierarchy.Find(root),0.01f);}
        auto zeroFactory=CreateLuaScriptFactory("function on_update(e,dt) local x,y,z=scene.world_position(2);assert(x==11 and y==20 and z==30 and #scene.world_matrix(2)==16) end","zero scale",{},matrixDefaults,{}, {},hierarchyView);auto zeroCallbacks=zeroFactory();hierarchy.Find(root)->transform.scale={0,0,0};
        {ScriptSceneView::Scope scope(*hierarchyView,hierarchy);zeroCallbacks.onUpdate(*hierarchy.Find(root),0.01f);}
        auto destroyFactory=CreateLuaScriptFactory("function on_update(e,dt) end;function on_destroy(e) assert(scene.world_position(e:id())==nil and scene.world_matrix(e:id())==nil);local x=scene.world_position(4);assert(x==7) end","removed world query",{},matrixDefaults,{}, {},hierarchyView);auto destroyCallbacks=destroyFactory();auto retained=*hierarchy.Find(root);Require(hierarchy.DestroyEntity(root),"Hierarchy removal failed");
        {ScriptSceneView::Scope scope(*hierarchyView,hierarchy);destroyCallbacks.onDestroy(retained);}
        const std::string invalid[]{"scene.exists(0)","scene.position(-1)","scene.name(1.5)","scene.exists(9007199254740992)","scene.exists('1')","scene.find('')","scene.find(1)","scene.find(string.rep('x',1025))"};
        for(const auto& expression:invalid){
            Write(fixture/"bad.lua","function on_update(e,dt) "+expression+";e:translate(100,0,0) end");ScriptRuntime bad;Require(bad.LoadResolvedAsset("bad",fixture/"bad.lua",error),error.c_str());EntityWorld faults;const auto id=faults.CreateEntity("Fault");faults.SetScript(id,"bad");bad.Update(faults,0.01f);Require(!bad.GetLastError(id).empty() && faults.Find(id)->transform.position.x==0,"Invalid scene argument escaped fault isolation");bad.Shutdown(faults);bad.Clear();
        }
        Write(fixture/"module.lua","scene.entity_count();function on_update(e,dt) end");ScriptRuntime module;Require(!module.LoadResolvedAsset("module",fixture/"module.lua",error),"Module-scope Scene read accepted");
        ScriptProperties defaults;auto factory=CreateLuaScriptFactory("function on_update(e,dt) assert(scene.entity_count()==1) end","direct",{},defaults,{}, {},view);auto callbacks=factory();EntityWorld direct;const auto id=direct.CreateEntity("Direct");
        {ScriptSceneView::Scope scope(*view,direct);callbacks.onUpdate(*direct.Find(id),0.01f);}
        bool refused=false;try{callbacks.onUpdate(*direct.Find(id),0.01f);}catch(const LuaScriptError&){refused=true;}Require(refused && !view->World(),"Retained factory reused expired host World");
        ScriptRuntime throwing;ScriptCallbacks native;native.onUpdate=[](auto&,float){throw std::runtime_error("native sentinel");};throwing.Register("native",native);EntityWorld nativeWorld;const auto nativeId=nativeWorld.CreateEntity("Native");nativeWorld.SetScript(nativeId,"native");
        bool nativeThrown=false;try{throwing.Update(nativeWorld,0.01f);}catch(const std::runtime_error& exception){nativeThrown=std::string(exception.what())=="native sentinel";}Require(nativeThrown,"Native failure swallowed");throwing.Shutdown(nativeWorld);throwing.Clear();
        std::error_code cleanup;std::filesystem::remove_all(fixture,cleanup);
        std::cout<<"PASS: scoped Lua Scene/local/world position and matrix reads, rotated/mirrored hierarchy and candidate ancestor, nil cycle/missing/nonfinite, zero-scale and lifetime/fault guards\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<"; retained fixture "<<fixture<<'\n';return 1;}
}
