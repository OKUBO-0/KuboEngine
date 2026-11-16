#include "engine/script/LuaScript.h"
#include <iostream>
#include <cstdlib>
#include <new>
#include <stdexcept>
namespace {bool observing=false;size_t allocations=0,failureAt=0;void Require(bool value,const char* why){if(!value)throw std::runtime_error(why);}}
void* operator new(size_t bytes){if(observing && ++allocations==failureAt){observing=false;throw std::bad_alloc();}if(auto* value=std::malloc(bytes?bytes:1))return value;throw std::bad_alloc();}
void* operator new[](size_t bytes){return ::operator new(bytes);}
void operator delete(void* value) noexcept{std::free(value);}
void operator delete[](void* value) noexcept{std::free(value);}
void operator delete(void* value,size_t) noexcept{std::free(value);}
void operator delete[](void* value,size_t) noexcept{std::free(value);}
int main(){try{
 using namespace Engine::Scene;using namespace Engine::Scripting;
 auto view=std::make_shared<ScriptSceneView>();auto audio=std::make_shared<ScriptAudioRequests>();audio->SetEnabled(true);ScriptProperties defaults;
 auto factory=CreateLuaScriptFactory("function on_update(e,dt) audio.stop_all();assert(scene.translate(2,1,0,0));e:translate(2,0,0) end","publication allocation",{},defaults,audio,{},view);
 const auto fixture=[](){EntityWorld world;const auto id=world.CreateEntity(std::string(300,'n'));world.CreateEntity("Peer");auto& record=*world.Find(id);record.hasScript=true;record.script.assetPath="test.lua";record.script.properties["label"]=std::string(256,'p');record.prefabLink["metadata"]=std::string(256,'b');return world;};
 auto observed=fixture();auto callbacks=factory();
 {ScriptSceneView::WriteScope scope(*view,observed);allocations=0;failureAt=0;observing=true;callbacks.onUpdate(*observed.Find(1),0);observing=false;}
 const auto positions=allocations;Require(observed.Find(1)->transform.position.x==2 && observed.Find(2)->transform.position.x==1 && audio->Take().size()==1 && positions>0,"Observed publication failed");callbacks={};
 for(size_t fault=1;fault<=positions;++fault){auto world=fixture();const auto before=world.Capture().dump();auto hook=factory();bool refused=false;
  {ScriptSceneView::WriteScope scope(*view,world);allocations=0;failureAt=fault;observing=true;try{hook.onUpdate(*world.Find(1),0);}catch(const std::exception&){refused=true;}observing=false;}
  const auto pending=audio->Take();
  const bool preserved=refused && world.Capture().dump()==before && pending.empty();
  if(!preserved)std::cerr<<"PUBLICATION_FAULT_POSITION="<<fault<<" OBSERVED_POSITIONS="<<positions<<" SELF_X="<<world.Find(1)->transform.position.x<<" PEER_X="<<world.Find(2)->transform.position.x<<" AUDIO_PENDING="<<pending.size()<<'\n';
  Require(preserved,"Native allocation fault partially published self/peer/metadata/Audio");
 }
 std::cout<<"LUA_SCENE_PUBLICATION_ALLOCATION_POSITIONS="<<positions<<'\n';
 std::cout<<"PASS: every observed native hook allocation failure preserves full self/peer metadata and Audio queue before all-scalar publication\n";return 0;
}catch(const std::exception& error){observing=false;std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
