#include "engine/script/ScriptSceneCommands.h"
#include "engine/script/ScriptRuntime.h"
#include <cstdlib>
#include <new>
#include <iostream>
#include <stdexcept>
namespace {
bool tracking=false;size_t allocations=0,failAt=0;
void Require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
}
void* operator new(size_t bytes){if(tracking && ++allocations==failAt)throw std::bad_alloc();if(auto* p=std::malloc(bytes?bytes:1))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,size_t) noexcept{std::free(p);}
void operator delete[](void* p,size_t) noexcept{std::free(p);}
int main(){try{
 using namespace Engine::Scene;using namespace Engine::Scripting;
 using Cmd=ScriptSceneCommands;using R=Cmd::Result;using Op=Cmd::Operation;
 EntityWorld world;world.CreateEntity("Parent");world.CreateEntity("Child",1);world.CreateEntity("Owner");
 world.SetRigidBody(2,{});world.SetScript(3,"owner");world.Find(3)->script.started=true;
 auto self=*world.Find(3);ScriptSceneView view;Cmd batch;const auto before=world.Capture().dump();
 EntityId created=777;
 {ScriptSceneView::WriteScope scope(view,world);batch.Begin(&view,self);
  Require(batch.Apply(1,Op::Position,{10,0,0})==R::Success && batch.Apply(2,Op::Velocity,{7,8,9})==R::Success && batch.Apply(3,Op::Translate,{1,0,0})==R::Success,"Pre-promotion scalar stage");
  Require(batch.Spawn("New",1,created)==R::Success && created==4 && batch.HasStructuralChanges() && batch.ReadWorld()->Find(4),"Lazy promotion/spawn");
  self.transform.position.y=2; // Legacy e: self edits remain a detached candidate.
  Require(batch.Apply(created,Op::Position,{2,0,0})==R::Success && batch.Apply(1,Op::Translate,{3,0,0})==R::Success,"Post-promotion scalar stage");
  Matrix4x4 matrix{};Require(batch.WorldMatrix(created,matrix) && matrix.m[3][0]==15 && batch.Velocity(2)->z==9 && batch.Transform(3)->position.y==2,"Read-your-writes across promotion");
  Require(batch.Destroy(2)==R::Success && batch.Apply(2,Op::Active)==R::Missing && !batch.Velocity(2) && !batch.Transform(2),"Deleted scalar peer resurrected");
  Require(batch.WorldMatrix(created,matrix) && matrix.m[3][0]==15 && world.Capture().dump()==before,"Deleted peer override broke unrelated query/live isolation");
  Require(batch.Prepare(),"Combined prepare");allocations=0;failAt=1;tracking=true;const bool published=batch.PublishPrepared();tracking=false;failAt=0;
  Require(published && allocations==0 && !batch.PublishPrepared(),"Combined publication allocated/replayed");
  Require(world.Find(1)->transform.position.x==13 && world.Find(created)->transform.position.x==2 && !world.Find(2) && world.Find(3)->transform.position.x==1 && world.Find(3)->transform.position.y==2 && world.Find(3)->script.started,"Combined publication dropped scalar/self/owning data");batch.Clear();
  self=*world.Find(3);batch.Begin(&view,self);
  Require(batch.Apply(3,Op::Translate,{5,0,0})==R::Success && batch.Destroy(3)==R::Success && !batch.Transform(3) && batch.Apply(3,Op::Active)==R::Missing && batch.Prepare() && batch.PublishPrepared() && !world.Find(3),"Structural self deletion refused/resurrected");batch.Clear();
 }
 EntityWorld limited;limited.CreateEntity("Owner");auto limitedSelf=*limited.Find(1);
 {ScriptSceneView::WriteScope scope(view,limited);batch.Begin(&view,limitedSelf);
  for(size_t i=0;i<63;++i)Require(batch.Apply(1,Op::Translate,{1,0,0})==R::Success,"Mixed cap early");
  Require(batch.Spawn("Last",0,created)==R::Success && batch.OperationCount()==64 && batch.Destroy(created)==R::Limit && batch.Apply(1,Op::Active)==R::Limit && batch.Prepare() && batch.PublishPrepared() && limited.Find(1)->transform.position.x==63,"Mixed scalar/structural operation cap");batch.Clear();
 }
 // Ordinary allocator injection across scalar -> structural -> scalar ->
 // delete -> Prepare. Failure at any site prevents all live publication.
 size_t positions=0;
 for(size_t position=0;;++position){
  EntityWorld live;live.CreateEntity("Parent");live.CreateEntity("Child",1);live.CreateEntity(std::string(300,'o'));live.SetRigidBody(2,{});
  auto owner=*live.Find(3);ScriptSceneView localView;ScriptSceneView::WriteScope scope(localView,live);Cmd local;local.Begin(&localView,owner);
  const auto snapshot=live.Capture().dump();const auto allocator=live.AllocationHighWatermark();
  Require(local.Apply(1,Op::Translate,{5,0,0})==R::Success,"Injection scalar fixture");
  allocations=0;failAt=position;tracking=true;EntityId output=777;
  R result=local.Spawn("Allocation target",1,output);
  if(result==R::Success)result=local.Apply(output,Op::Translate,{3,0,0});
  if(result==R::Success)result=local.Destroy(2);
  const bool prepared=result==R::Success && local.Prepare();tracking=false;failAt=0;
  if(position==0){positions=allocations;Require(positions>0 && prepared,"Combined allocation baseline");}
  else Require(!prepared && !local.Prepare() && !local.PublishPrepared(),"Combined native failure still publishable");
  Require(live.Capture().dump()==snapshot && live.AllocationHighWatermark()==allocator,"Combined native failure changed live World");
  if(position==positions)break;
 }
 // Native Runtime callbacks already permit structural World mutation. Create
 // must retain the reacquired record and never read a borrowed pointer after
 // storage replacement. Newly generated IDs wait for the next dispatch.
 EntityWorld runtimeWorld;const auto id=runtimeWorld.CreateEntity("Before Create");runtimeWorld.SetScript(id,"mutator");
 ScriptRuntime runtime;int updates=0,destroys=0,childCreates=0;EntityId child=0;
 Require(runtime.Register("child",{[&](EntityRecord&){++childCreates;},[](EntityRecord&,float){},{}}),"Child registration refused");
 Require(runtime.Register("mutator",{
  [&](EntityRecord& record){const auto ownerId=record.id;auto candidate=runtimeWorld;candidate.Find(ownerId)->name="After Create";
   child=candidate.CreateEntity("New scripted child");candidate.SetScript(child,"child");runtimeWorld.Swap(candidate);},
  [&](EntityRecord& record,float){Require(record.id==id && record.name=="After Create","Runtime used stale Create record");++updates;
   auto candidate=runtimeWorld;Require(candidate.DestroyEntity(id),"Runtime deletion fixture");runtimeWorld.Swap(candidate);},
  [&](EntityRecord& record){Require(record.id==id && record.name=="After Create","Destroy lost successful Create record");++destroys;}}),"Mutator registration refused");
 runtime.Update(runtimeWorld,0.1f);
 Require(updates==1 && destroys==1 && childCreates==0 && !runtimeWorld.Find(id) && runtimeWorld.Find(child),"Structural Create/Update/lifecycle dispatch");
 runtime.Update(runtimeWorld,0.1f);Require(childCreates==1,"Generated script not started next dispatch");runtime.Shutdown(runtimeWorld);
 // Fixed callbacks also replace storage. Shutdown Destroy may change another
 // World while the instance-ID snapshot remains safe; no recursive startup.
 EntityWorld fixedWorld;const auto fixedId=fixedWorld.CreateEntity("Fixed");fixedWorld.SetScript(fixedId,"fixed");
 ScriptRuntime fixed;int fixedTicks=0,fixedDestroys=0;
 Require(fixed.Register("fixed",{{},{},[&](EntityRecord& record){const auto ownerId=record.id;auto candidate=fixedWorld;
   candidate.DestroyEntity(ownerId);candidate.CreateEntity("Destroy-created");fixedWorld.Swap(candidate);++fixedDestroys;},
  [&](EntityRecord& record,float){const auto ownerId=record.id;auto candidate=fixedWorld;candidate.Find(ownerId)->transform.position.x+=1;fixedWorld.Swap(candidate);++fixedTicks;}}),"Fixed registration refused");
 fixed.FixedUpdate(fixedWorld,0.02f);Require(fixedTicks==1 && fixedWorld.Find(fixedId)->transform.position.x==1,"Fixed storage replacement");
 fixed.Shutdown(fixedWorld);fixed.Shutdown(fixedWorld);Require(fixedDestroys==1 && !fixedWorld.Find(fixedId) && fixed.GetRunningCount()==0,"Shutdown storage replacement repeated/lost Destroy");
 std::cout<<"PASS: scalar/structural candidate promotion, mixed cap, deleted peer/self reads, owning/self/no-allocation publication, "<<positions<<" allocation failure positions, and native Runtime Create/Update/Fixed/Shutdown storage replacement (Lua tested separately)\n";return 0;
}catch(const std::exception& e){tracking=false;std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
