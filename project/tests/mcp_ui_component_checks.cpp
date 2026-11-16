#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include "engine/services/McpCommandBridge.h"
#include "engine/services/McpProtocolSession.h"
#include "engine/assets/AssetRegistry.h"
#include "engine/editor/EntityHistory.h"
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <new>
static long failAfter=-1;static bool counting=false;static size_t allocations=0;
void* operator new(size_t n){if(counting)++allocations;if(failAfter==0){failAfter=-1;throw std::bad_alloc();}if(failAfter>0)--failAfter;if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept{std::free(p);}void operator delete(void* p,size_t) noexcept{std::free(p);}
static void Need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static void Write(const std::filesystem::path& path,const char* text){std::ofstream out(path,std::ios::binary);out<<text;out.close();Need(!out.fail(),"MCP UI fixture write failed");}
int main(){using namespace Engine;using V=Scene::SceneValue;std::filesystem::path root;try{
 root=std::filesystem::temp_directory_path()/(L"kubo-mcp-ui-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));Need(std::filesystem::create_directory(root),"Refuse existing MCP UI fixture");std::filesystem::create_directory(root/"Assets");
 const auto image=root/L"Assets/白.png";Write(image,"identity fixture, not decoded image");Write(root/"Assets/script.lua","return {}");
 Assets::AssetRegistry registry;std::string error;Need(registry.Scan(root/"Assets",error),error.c_str());const auto guid=registry.FindByPath(image)->guid,scriptGuid=registry.FindByPath(root/"Assets/script.lua")->guid;
 Scene::EntityWorld world;const auto id=world.CreateEntity("UI"),peer=world.CreateEntity("Peer"),retired=world.CreateEntity("Retired");world.DestroyEntity(retired);world.Find(id)->script.started=true;world.Find(peer)->script.started=true;world.Find(peer)->rigidBody.grounded=true;
 Services::McpCommandBridge bridge(root,&registry);
 const auto request=[&](const char* component,const V& patch){V value;value["id"]=1.0;value["method"]="scene.set_component";value["params"]["entityId"]=double(id);value["params"]["component"]=component;value["params"]["patch"]=patch;return value;};
 const auto call=[&](const char* component,const V& patch){return bridge.Handle(request(component,patch),world);};
 Need(call("canvas",V::parse(R"({"referenceSize":[640,360],"scaleMode":"constantPixels","sortOrder":-2})")).contains("result"),"Canvas add failed");
 Need(call("canvas",V::parse(R"({"enabled":false})")).contains("result") && world.Find(id)->canvas.referenceSize.x==640 && world.Find(id)->canvas.sortOrder==-2,"Canvas partial patch lost fields");
 Need(!call("canvas",V::parse(R"({"enabled":false})"))["result"]["updated"].get<bool>(),"Canvas no-op changed");
 V assigned=V::parse(R"({"asset":"Assets/stale.png","size":[120,80],"color":[1,0.5,0,0.75],"flipX":true,"sortOrder":12})");assigned["guid"]=guid;
 Need(call("sprite",assigned).contains("result"),"Sprite live GUID add failed");
 Need(call("sprite",V::parse(R"({"anchor":[1,0.5]})")).contains("result") &&
    world.Find(id)->sprite.anchor.x==1 && world.Find(id)->sprite.anchor.y==0.5f,"Sprite anchor patch failed");
 const auto before=world.Capture();Need(call("sprite",V::parse(R"({"pivot":[0.2,0.8]})")).contains("result") && world.Find(id)->sprite.assetGuid==guid && world.Find(id)->sprite.size.x==120 && world.Find(id)->sprite.color.w==0.75f && world.Find(id)->sprite.flipX && world.AllocationHighWatermark()==retired+1,"Sprite partial patch lost fields/allocator");
 Editor::EntityHistory history;Need(history.Record(before,world,id,id,error),error.c_str());history.Commit();const auto edited=world.Capture().dump();auto selection=id;Need(history.Undo(world,selection,error) && world.Capture().dump()==before.dump() && history.Redo(world,selection,error) && world.Capture().dump()==edited,"MCP UI authored history snapshots failed");world.Find(id)->script.started=true;world.Find(peer)->script.started=true;world.Find(peer)->rigidBody.grounded=true;
 // Explicit Undo/Restore has its existing allocator semantics. Retire a fresh
 // identity afterwards to isolate the MCP no-reconstruction guarantee.
 world.DestroyEntity(world.CreateEntity("Retired after history"));const auto expectedNext=world.AllocationHighWatermark();
 const auto reject=[&](const char* component,const char* patch){const auto snapshot=world.Capture().dump();const auto high=world.AllocationHighWatermark();Need(call(component,V::parse(patch)).contains("error") && world.Capture().dump()==snapshot && world.AllocationHighWatermark()==high,"Refused UI patch published fields/IDs");};
 reject("sprite",R"({"anchor":[1.1,0]})");
 for(const auto* component:{"canvas","sprite"})for(const auto* patch:{"{}","[]","true",R"({"unknown":true})",R"({"enabled":1})",R"({"sortOrder":0.5})",R"({"sortOrder":1e-100})",R"({"sortOrder":32768})",R"({"sortOrder":-32769})"})reject(component,patch);
 for(const auto* patch:{R"({"referenceSize":[0,720]})",R"({"referenceSize":[640]})",R"({"referenceSize":[640,true]})",R"({"scaleMode":""})",R"({"scaleMode":2})"})reject("canvas",patch);
 for(const auto* patch:{R"({"size":[-1e-100,1]})",R"({"size":[16385,1]})",R"({"pivot":[0,17]})",R"({"color":[1,1,1]})",R"({"color":[1,1,1,-1e-100]})",R"({"asset":"../escape.png"})",R"({"asset":"Assets\\bad.png"})",R"({"asset":"Assets/CON.png"})",R"({"guid":"BAD"})",R"({"guid":"ffffffffffffffff"})"})reject("sprite",patch);
 {V wrong;wrong["guid"]=scriptGuid;const auto snapshot=world.Capture().dump();Need(call("sprite",wrong).contains("error") && world.Capture().dump()==snapshot,"Wrong-type GUID accepted");}
 V pathOnly;pathOnly["guid"]=nullptr;pathOnly["asset"]="Assets/白.png";Need(call("sprite",pathOnly).contains("result") && world.Find(id)->sprite.assetGuid.empty(),"Explicit GUID clear failed");
 Need(call("sprite",assigned).contains("result"),"Reassignment failed");
 for(const char* protocol:{"2025-11-25","2024-11-05"}){
  size_t forwarded=0;Services::McpProtocolSession session([&](const V& input){++forwarded;return bridge.Handle(input,world);});V init=V::parse(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"capabilities":{},"clientInfo":{"name":"ui","version":"1"}}})");init["params"]["protocolVersion"]=protocol;session.Handle(init);session.HandleText(R"({"jsonrpc":"2.0","method":"notifications/initialized"})");
  const auto tools=session.HandleText(R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})")["result"]["tools"];Need(tools.get<crude_json::array>().size()==14 && tools[6]["inputSchema"]["oneOf"].get<crude_json::array>().size()==10,"UI schema/tool count");
  const auto send=[&](const char* name,const char* patch){V value=V::parse(R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"scene_set_component","arguments":{}}})");value["params"]["arguments"]=request(name,V::parse(patch))["params"];return session.Handle(value);};
  Need(send("canvas",R"({"sortOrder":0.1})")["result"]["isError"].get<bool>() && send("sprite",R"({"color":[1,1]})")["result"]["isError"].get<bool>() && forwarded==0,"Invalid UI transport forwarded");
  const auto result=send("canvas",R"({"enabled":true})");Need(!result["result"]["isError"].get<bool>() && forwarded==1 && result["result"].contains("structuredContent")==(std::string(protocol)=="2025-11-25"),"UI protocol response");
 }
 // Injection permits optional allocation recovery only if the whole desired
 // component is published; all actual failures retain the entire old World.
 const auto baseline=world;const auto oldBytes=baseline.Capture().dump();const auto high=baseline.AllocationHighWatermark();const auto change=request("sprite",V::parse(R"({"size":[240,160],"color":[0,1,0,1]})"));
 auto counted=baseline;allocations=0;counting=true;const auto response=bridge.Handle(change,counted);counting=false;const auto positions=allocations;Need(response.contains("result"),"Counted UI bridge failed");const auto expected=counted.Capture().dump();size_t refusals=0;
 for(size_t fault=0;fault<positions;++fault){auto output=baseline;bool success=false;failAfter=static_cast<long>(fault);try{success=bridge.Handle(change,output).contains("result");}catch(const std::bad_alloc&){}failAfter=-1;
  const auto observed=output.Capture().dump();
  if(observed!=(success?expected:oldBytes) || output.AllocationHighWatermark()!=high || !output.Find(id)->script.started || !output.Find(peer)->script.started || !output.Find(peer)->rigidBody.grounded){
    std::cerr<<"UI_FAULT_POSITION="<<fault<<" success="<<success<<" old="<<(observed==oldBytes)<<" desired="<<(observed==expected)<<" high="<<output.AllocationHighWatermark()<<'/'<<high<<" self="<<output.Find(id)->script.started<<" peer="<<output.Find(peer)->script.started<<" grounded="<<output.Find(peer)->rigidBody.grounded<<'\n';throw std::runtime_error("UI allocation published partial/unrelated state");
  }if(!success)++refusals;
 }
 Need(refusals>0,"No UI allocation refusal exercised");
 Need(world.Find(id)->script.started && world.Find(peer)->script.started && world.Find(peer)->rigidBody.grounded && world.CreateEntity("After")==expectedNext,"UI edit reset transient/allocator");
 for(const auto* component:{"canvas","sprite"}){Need(call(component,nullptr)["result"]["updated"].get<bool>() && !call(component,nullptr)["result"]["updated"].get<bool>(),"UI remove/no-op");}
 std::filesystem::remove_all(root);root.clear();std::cout<<"PASS: MCP Canvas/Sprite schema10/tools14, partial/default/no-op/null removal/GUID clear, typed live Texture refs, strict transport+both protocols, history/unrelated transient/ID preservation and allocation positions="<<positions<<" refusals="<<refusals<<" (not live Editor IPC/pixels)\n";return 0;
}catch(const std::exception& e){failAfter=-1;counting=false;std::cerr<<"FAIL: "<<e.what()<<'\n';if(!root.empty())std::cerr<<"MCP_UI_FIXTURE_RETAINED="<<root<<'\n';return 1;}}
