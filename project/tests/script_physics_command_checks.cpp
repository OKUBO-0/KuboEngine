#include "engine/script/ScriptSceneCommands.h"
#include <cstdlib>
#include <new>
#include <iostream>
#include <limits>
namespace {bool tracking=false;size_t allocations=0,failAt=0;void Need(bool value,const char* why){if(!value)throw std::runtime_error(why);}}
void* operator new(size_t n){if(tracking && ++allocations==failAt)throw std::bad_alloc();if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{std::free(p);}void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,size_t) noexcept{std::free(p);}void operator delete[](void* p,size_t) noexcept{std::free(p);}
int main(){try{
 using namespace Engine;using namespace Scene;using namespace Scripting;using R=ScriptSceneCommands::Result;using Op=ScriptSceneCommands::Operation;
 EntityWorld seed;seed.CreateEntity("Self");seed.CreateEntity("Peer");
 Physics::ColliderComponent collider;collider.halfExtents={2,3,4};collider.isTrigger=true;collider.friction=0.25f;collider.restitution=0.75f;
 Physics::RigidBodyComponent body;body.velocity={4,5,6};body.mass=2;body.useGravity=false;body.grounded=true;
 seed.SetRigidBody(1,{});seed.SetRigidBody(2,{});seed.SetCollider(1,{});seed.SetCollider(2,{});
 ScriptSceneView view;ScriptSceneCommands commands;auto self=*seed.Find(1);
 {ScriptSceneView::Scope scope(view,seed);commands.Begin(&view,self);Need(commands.SetCollider(1,collider)==R::Unavailable && commands.RemoveCollider(1)==R::Unavailable && commands.SetRigidBody(1,body)==R::Unavailable && commands.RemoveRigidBody(1)==R::Unavailable,"Readonly physics writes admitted");}
 auto world=seed;self=*world.Find(1);const auto before=world.Capture().dump();
 {ScriptSceneView::WriteScope scope(view,world);commands.Begin(&view,self);
  Need(commands.Apply(1,Op::Velocity,{8,0,0})==R::Success && commands.Apply(2,Op::Velocity,{9,0,0})==R::Success && commands.Apply(2,Op::Translate,{3,0,0})==R::Success,"Scalar prefix failed");
  Need(commands.SetRigidBody(1,body)==R::Success && commands.SetRigidBody(2,body)==R::Success && commands.SetCollider(1,collider)==R::Success,"Physics replace failed");
  Physics::RigidBodyComponent read;Need(commands.RigidBody(1,read) && read.velocity.x==4 && read.mass==2 && !read.grounded && commands.RigidBody(2,read) && read.velocity.x==4,"Earlier velocity patch overrode replacement");
  self.rigidBody.velocity.x=7;Need(commands.RigidBody(1,read) && read.velocity.x==7,"Detached self velocity not visible");
  Need(commands.Apply(2,Op::Velocity,{10,0,0})==R::Success && commands.RigidBody(2,read) && read.velocity.x==10,"Later velocity not visible");
  EntityId clone=0;Need(commands.Clone(1,0,clone)==R::Success && commands.RigidBody(clone,read) && read.velocity.x==7 && commands.Collider(clone)->halfExtents.y==3,"Clone lost latest Physics/scalars");
  Need(commands.RemoveRigidBody(1)==R::Success && !commands.RigidBody(1,read) && !commands.Velocity(1) && commands.Apply(1,Op::Velocity,{1,0,0})==R::Missing && commands.RemoveRigidBody(1)==R::Missing,"Self removal query/velocity incorrect");
  Need(commands.RemoveRigidBody(2)==R::Success && commands.RemoveCollider(2)==R::Success && !commands.Collider(2),"Peer removal failed");
  Need(commands.SetRigidBody(1,body)==R::Success && commands.SetRigidBody(2,body)==R::Success && commands.RemoveCollider(1)==R::Success && commands.SetCollider(1,collider)==R::Success,"Re-add failed");
  Need(world.Capture().dump()==before && commands.Prepare(),"Physics leaked before publication/prepare failed");
  tracking=true;allocations=0;failAt=1;const auto published=commands.PublishPrepared();tracking=false;failAt=0;
  Need(published && allocations==0 && world.Find(1)->rigidBody.velocity.x==4 && world.Find(2)->rigidBody.velocity.x==4 && world.Find(2)->transform.position.x==3 && !world.Find(2)->hasCollider && world.Find(clone)->rigidBody.velocity.x==7,"Publication allocated or lost replacement/removal order");commands.Clear();
 }
 {ScriptSceneView::WriteScope scope(view,world);self=*world.Find(1);commands.Begin(&view,self);
  auto bad=collider;bad.halfExtents.x=-1;Need(commands.SetCollider(1,bad)==R::Invalid,"Negative extent accepted");bad=collider;bad.friction=1.1f;Need(commands.SetCollider(1,bad)==R::Invalid,"Friction range accepted");bad=collider;bad.restitution=std::numeric_limits<float>::quiet_NaN();Need(commands.SetCollider(1,bad)==R::Invalid,"NaN restitution accepted");
  auto badBody=body;badBody.mass=0;Need(commands.SetRigidBody(1,badBody)==R::Invalid,"Zero mass accepted");badBody=body;badBody.velocity.x=std::numeric_limits<float>::infinity();Need(commands.SetRigidBody(1,badBody)==R::Invalid,"Infinite velocity accepted");
  Need(commands.OperationCount()==0 && !commands.ReplacesWorld() && commands.SetCollider(999,collider)==R::Missing && commands.RemoveRigidBody(0)==R::Invalid,"Invalid/missing edit changed transaction");
  for(int i=0;i<64;++i)Need(commands.SetCollider(1,collider)==R::Success,"Physics quota early");
  Need(commands.RemoveCollider(1)==R::Limit && commands.SetRigidBody(1,body)==R::Limit && commands.OperationCount()==64,"Physics quota bypassed");commands.Clear();
 }
 self=*world.Find(1);commands.Begin(&view,self);Need(!commands.Collider(1) && commands.SetRigidBody(1,body)==R::Unavailable,"Expired scope admitted Physics");commands.Clear();
 // Each first-edit route and its final Prepare is swept at every observed
 // ordinary allocation. Live World/ID storage and its address must stay intact.
 size_t totalPositions=0;
 for(int action=0;action<4;++action){size_t positions=0;
  for(size_t position=0;;++position){auto live=seed;auto record=*live.Find(1);ScriptSceneView owned;ScriptSceneView::WriteScope scope(owned,live);ScriptSceneCommands staged;staged.Begin(&owned,record);
   const auto snapshot=live.Capture().dump();const auto high=live.AllocationHighWatermark();const auto* pointer=live.Find(1);
   Need(staged.Apply(2,Op::Translate,{11,0,0})==R::Success,"Fault scalar prefix failed");
   tracking=true;allocations=0;failAt=position;
   const auto result=action==0?staged.SetCollider(1,collider):action==1?staged.RemoveCollider(1):action==2?staged.SetRigidBody(1,body):staged.RemoveRigidBody(1);
   const bool prepared=result==R::Success && staged.Prepare();tracking=false;failAt=0;
   if(position==0){positions=allocations;totalPositions+=positions;Need(prepared && positions>0,"Physics allocation baseline failed");}
   else Need(!prepared && !staged.Prepare() && !staged.PublishPrepared(),"Faulted Physics edit publishable");
   Need(live.Capture().dump()==snapshot && live.AllocationHighWatermark()==high && live.Find(1)==pointer,"Faulted Physics edit changed live World/IDs/address");
   if(position==positions)break;
  }
 }
 std::cout<<"PASS: native Physics component settings/removal/query, self+peer velocity replacement ordering, scalar+clone composition, scope/64-op/validation gates, no-allocation publication and "<<totalPositions<<" ordinary allocation failure positions (no Lua/GPU)\n";return 0;
}catch(const std::exception& error){tracking=false;std::cerr<<error.what()<<'\n';return 1;}}
