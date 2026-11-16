#include "engine/script/ScriptSceneCommands.h"
#include <iostream>
#include <stdexcept>
#include <cstdlib>
#include <new>
#include <limits>
#include <numbers>
namespace {
bool countAllocations=false;size_t allocations=0;
void Require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
bool Near(float a,float b){return std::abs(a-b)<0.001f;}
}
void* operator new(size_t bytes){if(countAllocations)++allocations;if(auto* value=std::malloc(bytes?bytes:1))return value;throw std::bad_alloc();}
void* operator new[](size_t bytes){return ::operator new(bytes);}
void operator delete(void* value) noexcept{std::free(value);}
void operator delete[](void* value) noexcept{std::free(value);}
void operator delete(void* value,size_t) noexcept{std::free(value);}
void operator delete[](void* value,size_t) noexcept{std::free(value);}
int main(){try{
 using namespace Engine::Scene;using namespace Engine::Scripting;
 using Op=ScriptSceneCommands::Operation;using R=ScriptSceneCommands::Result;
 EntityWorld world;world.CreateEntity("Parent");world.CreateEntity("Child",1);world.CreateEntity("Reader");
 world.Find(1)->transform.position={10,20,30};world.Find(1)->transform.rotation={0,0,std::numbers::pi_v<float>/2};world.Find(1)->transform.scale={-2,3,1};
 world.Find(2)->transform.position={1,2,3};world.Find(2)->active=false;world.SetRigidBody(2,{});
 auto self=*world.Find(3);ScriptSceneView view;ScriptSceneCommands batch;
 const auto before=world.Capture().dump();
 batch.Begin(&view,self);Require(batch.Apply(2,Op::Position,{4,5,6})==R::Unavailable,"Unscoped command accepted");
 {ScriptSceneView::Scope readonly(view,world);batch.Begin(&view,self);
  Require(!batch.Writable() && batch.Apply(2,Op::Position,{4,5,6})==R::Unavailable && batch.Transform(2)->position.x==1,"Read scope admitted mutation or lost reads");}
 {ScriptSceneView::WriteScope scope(view,world);batch.Begin(&view,self);
  Require(batch.Writable() && batch.Apply(1,Op::Translate,{1,0,0})==R::Success && batch.Apply(2,Op::Position,{2,2,3})==R::Success && batch.Apply(2,Op::Translate,{1,0,0})==R::Success && batch.Apply(2,Op::Velocity,{7,8,9})==R::Success && batch.Apply(2,Op::Active,{},true)==R::Success,"Staging failed");
  bool active=false;Require(batch.Transform(2)->position.x==3 && batch.Velocity(2)->z==9 && batch.Active(2,active) && active && !batch.Velocity(1),"Read-your-writes/components failed");
  Matrix4x4 matrix{};Require(batch.WorldMatrix(2,matrix) && Near(matrix.m[3][0],5) && Near(matrix.m[3][1],14) && Near(matrix.m[3][2],33),"Multiple ancestor candidates not composed");
  Require(world.Capture().dump()==before && batch.OperationCount()==5 && batch.TargetCount()==2,"Staging published or counted wrong");
  const auto calls=batch.OperationCount();Require(batch.Apply(999,Op::Position,{1,2,3})==R::Missing && batch.Apply(1,Op::Velocity,{1,2,3})==R::Missing && batch.Apply(0,Op::Active)==R::Invalid && batch.Apply(2,Op::Position,{(std::numeric_limits<float>::infinity)(),0,0})==R::Invalid && batch.OperationCount()==calls,"Invalid/missing command changed batch");
  Require(batch.Apply(3,Op::Translate,{1,2,3})==R::Success && self.transform.position.x==1 && world.Find(3)->transform.position.x==0,"Self candidate not isolated");
  Require(batch.Prepare() && world.Capture().dump()==before && batch.Apply(2,Op::Active)==R::Unavailable,"Prepare published or admitted later staging");
  batch.PublishPrepared();Require(world.Find(1)->transform.position.x==11 && world.Find(2)->transform.position.x==3 && world.Find(2)->active && world.Find(2)->rigidBody.velocity.z==9 && world.Find(3)->transform.position.x==0 && world.Find(2)->parent==1 && world.Find(2)->name=="Child","Prepared scalar publication failed/changed unrelated fields");
 }
 Require(!batch.Writable() && !batch.Prepare(),"Expired write scope accepted preparation");
 {ScriptSceneView::WriteScope another(view,world);Require(!batch.Writable() && !batch.Prepare(),"New scope reused expired batch token");batch.Begin(&view,self);
  Require(batch.Apply(2,Op::Translate,{1,0,0})==R::Success,"Nested fixture stage failed");
  {ScriptSceneView::Scope read(view,world);Require(!batch.Writable() && !batch.Prepare(),"Nested readonly scope leaked write permission");}
  Require(batch.Writable() && batch.Prepare(),"Nested scope failed to restore original token");batch.Clear();batch.PublishPrepared();Require(world.Find(2)->transform.position.x==3,"Cleared batch published");
  batch.Begin(&view,*world.Find(3));Require(!batch.Writable() && batch.Apply(3,Op::Translate,{1,0,0})==R::Unavailable,"Live self record bypassed transaction");
 }
 // Missing later targets and missing later components refuse the whole batch.
 {ScriptSceneView::WriteScope scope(view,world);batch.Begin(&view,self);
  Require(batch.Apply(1,Op::Translate,{2,0,0})==R::Success && batch.Apply(2,Op::Velocity,{1,0,0})==R::Success,"Refusal fixture failed");
  world.RemoveRigidBody(2);Require(!batch.Prepare() && world.Find(1)->transform.position.x==11,"Component refusal partially published");batch.PublishPrepared();Require(world.Find(1)->transform.position.x==11,"Failed preparation still published");
  batch.Begin(&view,self);Require(batch.Apply(1,Op::Translate,{2,0,0})==R::Success && batch.Apply(2,Op::Translate,{2,0,0})==R::Success && world.DestroyEntity(2) && !batch.Prepare() && world.Find(1)->transform.position.x==11,"Missing-target refusal partially published");
 }
 {ScriptSceneView::WriteScope scope(view,world);batch.Begin(&view,self);
  for(size_t i=0;i<ScriptSceneCommands::MaxOperations;++i)Require(batch.Apply(1,Op::Translate,{1,0,0})==R::Success,"Repeated target limit too early");
  Require(batch.Apply(1,Op::Translate,{1,0,0})==R::Limit && batch.OperationCount()==64 && batch.TargetCount()==1 && world.Find(1)->transform.position.x==11,"Repeated operations escaped cap");batch.Clear();
  batch.Begin(&view,self);Require(batch.Apply(1,Op::Position,{(std::numeric_limits<float>::max)(),0,0})==R::Success,"Overflow setup failed");const auto calls=batch.OperationCount();
  Require(batch.Apply(1,Op::Translate,{(std::numeric_limits<float>::max)(),0,0})==R::Invalid && batch.OperationCount()==calls && batch.Transform(1)->position.x==(std::numeric_limits<float>::max)(),"Overflow changed candidate");batch.Clear();
 }
 // Prepared batches also fail closed at publication, not only preparation.
 {ScriptSceneView::WriteScope scope(view,world);batch.Begin(&view,self);Require(batch.Apply(1,Op::Translate,{5,0,0})==R::Success && batch.Prepare(),"Expired publish setup failed");}
 Require(!batch.PublishPrepared() && world.Find(1)->transform.position.x==11,"Expired prepared batch published");
 {ScriptSceneView::WriteScope scope(view,world);batch.Begin(&view,self);
  Require(batch.Apply(3,Op::Position,{(std::numeric_limits<float>::max)(),0,0})==R::Success && batch.Apply(1,Op::Translate,{5,0,0})==R::Success,"Self failure setup failed");
  self.transform.position.x=(std::numeric_limits<float>::infinity)();Require(!batch.Prepare() && !batch.PublishPrepared() && world.Find(1)->transform.position.x==11,"Invalid self partially published peers");self=*world.Find(3);
  Require(batch.Apply(EntityWorld::MaximumEntityId+1,Op::Active)==R::Invalid && batch.Apply(1,static_cast<Op>(99))==R::Invalid,"Invalid ID/operation accepted");
 }
 // Rotation/scale and publication-time component revalidation.
 {ScriptSceneView::WriteScope scope(view,world);world.SetRigidBody(1,{});batch.Begin(&view,self);
  Require(batch.Apply(1,Op::Rotation,{0,0,0})==R::Success && batch.Apply(1,Op::Scale,{-1,2,3})==R::Success && batch.Apply(1,Op::Velocity,{9,0,0})==R::Success && batch.Prepare(),"Rotation/scale staging failed");
  world.RemoveRigidBody(1);Require(!batch.PublishPrepared() && world.Find(1)->transform.scale.x==-2,"Publication-time component removal partially wrote TRS");
  batch.Begin(&view,self);Require(batch.Apply(1,Op::Rotation,{0,0,0})==R::Success && batch.Apply(1,Op::Scale,{-1,2,3})==R::Success && batch.Prepare() && batch.PublishPrepared() && world.Find(1)->transform.rotation.z==0 && world.Find(1)->transform.scale.x==-1,"Rotation/scale publication failed");
 }
 // Full unique-target capacity and the complete prepare/query/publish path must
 // use no C++ heap allocation. The test fixture itself is outside this interval.
 EntityWorld many;const auto owner=many.CreateEntity("Owner");for(size_t i=0;i<64;++i)many.CreateEntity("Target",i==0?0:2);
 auto manySelf=*many.Find(owner);ScriptSceneView manyView;
 {ScriptSceneView::WriteScope scope(manyView,many);batch.Begin(&manyView,manySelf);allocations=0;countAllocations=true;bool accepted=true;
  for(size_t i=0;i<64;++i)accepted=accepted && batch.Apply(i+2,Op::Position,{float(i),0,0})==R::Success;
  Matrix4x4 matrix{};accepted=accepted && batch.WorldMatrix(65,matrix) && batch.Prepare() && batch.PublishPrepared();countAllocations=false;
  Require(accepted && allocations==0 && batch.TargetCount()==64 && many.Find(65)->transform.position.x==63,"Unique capacity/allocation-free path failed");
 }
 // Multiple override API rejects duplicate/missing identities with output held.
 std::array<EntityTransformOverride,2> overrides{{{1,world.Find(1)->transform},{1,world.Find(1)->transform}}};Matrix4x4 held{};held.m[0][0]=42;
 Require(!world.GetWorldMatrix(1,held,overrides) && held.m[0][0]==42,"Duplicate override changed output");overrides[1].id=999;
 Require(!world.GetWorldMatrix(1,held,overrides) && held.m[0][0]==42,"Missing override changed output");
 auto removed=*world.Find(3);Require(world.DestroyEntity(3),"Removed-self fixture failed");
 {ScriptSceneView::WriteScope scope(view,world);batch.Begin(&view,removed);Require(batch.Apply(3,Op::Active)==R::Missing && batch.Apply(1,Op::Scale,{0,0,0})==R::Success && batch.WorldMatrix(1,held) && batch.Prepare(),"Removed Destroy/remaining target/zero scale failed");batch.PublishPrepared();}
 std::cout<<"PASS: scoped scalar Scene command staging, read-your-writes/multiple ancestor transforms, self isolation, exact operation/unique target caps, overflow/missing/component/expired/nested/read-only refusal and allocation-free prepare/query/publication (core only)\n";return 0;
}catch(const std::exception& error){countAllocations=false;std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
