#include "engine/script/ScriptSceneStructure.h"
#include <cstdlib>
#include <new>
#include <iostream>
#include <stdexcept>

namespace {
bool tracking=false;size_t allocations=0,failAt=0;
void Require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
}
void* operator new(size_t bytes){
    if(tracking && ++allocations==failAt)throw std::bad_alloc();
    if(auto* p=std::malloc(bytes?bytes:1))return p;throw std::bad_alloc();
}
void* operator new[](size_t bytes){return ::operator new(bytes);}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,size_t) noexcept{std::free(p);}
void operator delete[](void* p,size_t) noexcept{std::free(p);}
int main(){try{
 using namespace Engine::Scene;using namespace Engine::Scripting;
 using Tx=ScriptSceneStructure;using R=Tx::Result;
 static_assert(noexcept(std::declval<EntityWorld&>().Swap(std::declval<EntityWorld&>())));
 EntityWorld world;world.CreateEntity("Root");world.CreateEntity("Child",1);world.CreateEntity("Grandchild",2);world.CreateEntity("Owner");
 world.Find(2)->transform.position={2,3,4};world.SetScript(4,"move.lua");world.Find(4)->script.started=true;
 // Owning fields must survive World publication without record moves/copies.
 world.Find(4)->name=std::string(300,'n');world.Find(4)->prefabLink=SceneValue(crude_json::object{});
 world.Find(4)->prefabLink["mapping"]=SceneValue(crude_json::array{});
 SceneValue reservation(crude_json::object{});reservation["entity"]=100.0;
 world.Find(4)->prefabLink["mapping"].get<crude_json::array>().push_back(reservation);
 ScriptSceneView view;Tx tx;EntityId created=777;
 const auto before=world.Capture().dump();const auto high=world.AllocationHighWatermark();
 Require(tx.Begin(&view)==R::Unavailable,"Unscoped structural admission");
 {ScriptSceneView::Scope scope(view,world);Require(tx.Begin(&view)==R::Unavailable,"Readonly structural admission");}
 {ScriptSceneView::WriteScope scope(view,world);
  Require(tx.Begin(&view)==R::Success && tx.Spawn("New",999,created)==R::Missing && created==777,"Missing parent silently became root");
  Require(tx.Spawn("",0,created)==R::Invalid && tx.Spawn(std::string(257,'x'),0,created)==R::Invalid && tx.Spawn(std::string_view("a\0b",3),0,created)==R::Invalid,"Invalid name accepted");
  Require(tx.Spawn("生成",2,created)==R::Success && created==101 && tx.Candidate()->Find(created)->parent==2,"Reserved ID/Unicode/candidate failed");
  Require(world.Capture().dump()==before && world.AllocationHighWatermark()==high && !world.Find(created),"Spawn leaked into live World");
  Require(tx.Destroy(2)==R::Success && !tx.Candidate()->Find(2) && tx.Candidate()->Find(3)->parent==0 && tx.Candidate()->Find(created)->parent==0 && tx.Candidate()->Find(3)->transform.scale.x==1,"Single deletion changed policy");
  Require(tx.Prepare() && tx.Spawn("Late",0,created)==R::Unavailable,"Prepared transaction mutable");
  allocations=0;failAt=1;tracking=true;const bool published=tx.PublishPrepared();tracking=false;failAt=0;
  Require(published && allocations==0 && !tx.PublishPrepared() && !tx.Candidate(),"Publication allocated/replayed/exposed retired World");
  Require(!world.Find(2) && world.Find(3)->parent==0 && world.Find(101)->parent==0 && world.Find(4)->script.started && world.Find(4)->name.size()==300 && world.AllocationHighWatermark()==102,"Publication corrupted owning data/allocator/runtime state");
  Require(tx.Begin(&view)==R::Success && tx.Destroy(101)==R::Success && tx.Spawn("After deletion",1,created)==R::Success && created==102,"Deleted ID reused");
  Require(tx.Destroy(1,true)==R::Success && !tx.Candidate()->Find(1) && !tx.Candidate()->Find(102) && tx.Candidate()->Find(3) && tx.RemovedCount()==3,"Subtree deletion removed unrelated roots");
  Require(tx.Prepare() && tx.PublishPrepared() && world.GetEntities().size()==2,"Subtree publication failed");
 }
 {ScriptSceneView::WriteScope scope(view,world);Require(tx.Begin(&view)==R::Success && tx.Spawn("Expired",0,created)==R::Success && tx.Prepare(),"Expiry setup");}
 const auto stable=world.Capture().dump();Require(!tx.PublishPrepared() && !tx.Candidate(),"Expired transaction published");
 {ScriptSceneView::WriteScope scope(view,world);Require(!tx.PublishPrepared(),"Fresh scope resurrected old token");
  Require(tx.Begin(&view)==R::Success && tx.Spawn("Nested",0,created)==R::Success,"Nested fixture");
  {ScriptSceneView::Scope nested(view,world);Require(!tx.Candidate() && !tx.Prepare(),"Nested readonly admitted structural write");}
  Require(tx.Prepare(),"Nested scope did not restore token");tx.Clear();Require(!tx.PublishPrepared() && world.Capture().dump()==stable,"Clear published");
  Require(tx.Begin(&view)==R::Success,"Operation limit setup");
  for(size_t i=0;i<64;++i)Require(tx.Spawn("Bounded",0,created)==R::Success,"Operation cap early");
  Require(tx.Spawn("Extra",0,created)==R::Limit && tx.Destroy(3)==R::Limit && tx.OperationCount()==64,"Operation cap bypass");tx.Clear();
 }
 EntityWorld forest;auto root=forest.CreateEntity("Tree");
 for(size_t i=0;i<64;++i)forest.CreateEntity("Leaf",root);
 {ScriptSceneView::WriteScope scope(view,forest);Require(tx.Begin(&view)==R::Success && tx.Destroy(root,true)==R::Limit && tx.OperationCount()==0 && tx.Candidate()->GetEntities().size()==65,"Aggregate deletion cap/partial subtree");}
 EntityWorld invalid;invalid.CreateEntity("a");invalid.CreateEntity("b");
 {ScriptSceneView::WriteScope scope(view,invalid);
  invalid.Find(1)->parent=2;invalid.Find(2)->parent=1;Require(tx.Begin(&view)==R::Invalid,"Cycle accepted");
  invalid.Find(1)->parent=999;invalid.Find(2)->parent=0;Require(tx.Begin(&view)==R::Invalid,"Missing ancestry accepted");
  invalid.GetEntities()[0].parent=0;invalid.GetEntities()[1].id=1;Require(tx.Begin(&view)==R::Invalid,"Duplicate identity accepted");
  invalid.GetEntities().resize(Tx::MaxEntities+1);Require(tx.Begin(&view)==R::Limit,"World count cap bypass");
 }
 EntityWorld exhausted;exhausted.CreateEntity("Last");exhausted.GetEntities()[0].id=EntityWorld::MaximumEntityId;
 {ScriptSceneView::WriteScope scope(view,exhausted);Require(tx.Begin(&view)==R::Success,"Exhaustion setup");created=777;
  Require(tx.Spawn("No ID",0,created)==R::Invalid && created==777 && tx.OperationCount()==0 && !tx.Prepare(),"Exhaustion consumed output/operation");}
 // Inject every ordinary C++ allocation in Begin/Spawn/Destroy/Prepare. The
 // live World and allocator remain intact; poisoned batches cannot publish.
 // Aligned allocators, concurrent hosts, Lua/audio/lifecycle are not covered.
 size_t totalPositions=0;
 for(unsigned operation=0;operation<4;++operation){
  size_t positions=0;
  for(size_t position=0;;++position){
   auto live=world;ScriptSceneView localView;Tx local;ScriptSceneView::WriteScope scope(localView,live);
   if(operation!=0)Require(local.Begin(&localView)==R::Success,"Allocation fixture begin");
   if(operation==3)Require(local.Spawn("Prepared",0,created)==R::Success,"Prepare fixture stage");
   const auto snapshot=live.Capture().dump();const auto allocator=live.AllocationHighWatermark();
   allocations=0;failAt=position;tracking=true;R result=R::Success;bool prepared=false;EntityId output=777;
   if(operation==0)result=local.Begin(&localView);
   else if(operation==1)result=local.Spawn(std::string_view("allocation target"),0,output);
   else if(operation==2)result=local.Destroy(4);
   else prepared=local.Prepare();
   tracking=false;failAt=0;
   if(position==0){positions=allocations;Require(positions>0 && result==R::Success && (operation!=3 || prepared),"Allocation baseline failed");totalPositions+=positions;}
   else {
    Require((operation==3 ? !prepared : result==R::AllocationFailure) && !local.Candidate() && !local.Prepare() && !local.PublishPrepared(),"Faulted structural transaction usable");
    Require(live.Capture().dump()==snapshot && live.AllocationHighWatermark()==allocator && (operation!=1 || output==777),"Allocation failure published/consumed live identity");
   }
   if(position==positions)break;
  }
 }
 std::cout<<"PASS: structural transaction core; scoped spawn/single/subtree deletion, reservation/high-water IDs, hierarchy/count/name/operation limits, owning state retention, no-allocation publication, and "<<totalPositions<<" native allocation failure positions (no Lua/host integration)\n";return 0;
}catch(const std::exception& e){tracking=false;std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
