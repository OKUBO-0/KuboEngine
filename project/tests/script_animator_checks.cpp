#include "engine/script/ScriptSceneCommands.h"
#include <cstdlib>
#include <new>
#include <iostream>
namespace { bool tracking=false;size_t allocations=0,failAt=0;void Require(bool v,const char* why){if(!v)throw std::runtime_error(why);} }
void* operator new(size_t n){if(tracking && ++allocations==failAt)throw std::bad_alloc();if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{std::free(p);}void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,size_t) noexcept{std::free(p);}void operator delete[](void* p,size_t) noexcept{std::free(p);}
int main(){try{
 using namespace Engine::Scene;using namespace Engine::Scripting;using R=ScriptSceneCommands::Result;using Op=ScriptSceneCommands::Operation;
 EntityWorld seed;seed.CreateEntity("Self");seed.CreateEntity("Peer");const std::string clip(256,'a');
 ScriptSceneView view;ScriptSceneCommands commands;auto self=*seed.Find(1);
 {ScriptSceneView::Scope scope(view,seed);commands.Begin(&view,self);Require(commands.SetAnimator(1,clip,1,true,true)==R::Unavailable,"Read-only scope mutated Animator");}
 EntityWorld world=seed;self=*world.Find(1);
 {ScriptSceneView::WriteScope scope(view,world);commands.Begin(&view,self);const auto before=world.Capture().dump();
  Require(commands.Apply(2,Op::Translate,{3,0,0})==R::Success && commands.SetAnimator(1,clip,0.5,false,false)==R::Success && commands.SetAnimator(2,"peer",2,true,true)==R::Success,"Animator staging failed");
  Require(commands.Animator(1) && commands.Animator(1)->clip==clip && commands.Animator(2)->speed==2 && world.Capture().dump()==before,"Staged query/live isolation failed");
  EntityId clone=0;Require(commands.Clone(1,0,clone)==R::Success && commands.Animator(clone)->clip==clip,"Clone lost latest staged Animator");
  Require(commands.RemoveAnimator(2)==R::Success && !commands.Animator(2) && commands.RemoveAnimator(2)==R::Missing && commands.SetAnimator(999,"default",1,true,true)==R::Missing,"Remove/missing semantics failed");
  Require(commands.Prepare(),"Prepare failed");tracking=true;allocations=0;failAt=1;const auto published=commands.PublishPrepared();tracking=false;failAt=0;
  Require(published && allocations==0 && world.Find(1)->hasAnimator && !world.Find(2)->hasAnimator && world.Find(2)->transform.position.x==3 && world.Find(clone)->animator.clip==clip,"Publication allocated/lost combined settings/scalars");commands.Clear();
 }
 {ScriptSceneView::WriteScope scope(view,world);self=*world.Find(1);commands.Begin(&view,self);for(int i=0;i<64;++i)Require(commands.SetAnimator(1,"default",1,true,true)==R::Success,"Animator budget early");Require(commands.RemoveAnimator(1)==R::Limit,"Animator bypassed operation cap");commands.Clear();}
 self=*world.Find(1);commands.Begin(&view,self);Require(!commands.Animator(1) && commands.SetAnimator(1,"default",1,true,true)==R::Unavailable,"Expired scope admitted Animator");
 size_t positions=0;
 for(size_t position=0;;++position){auto live=seed;auto record=*live.Find(1);ScriptSceneView owned;ScriptSceneView::WriteScope scope(owned,live);ScriptSceneCommands staged;staged.Begin(&owned,record);const auto before=live.Capture().dump();const auto high=live.AllocationHighWatermark();const auto* pointer=live.Find(1);
  Require(staged.Apply(2,Op::Translate,{4,0,0})==R::Success,"Fault prefix failed");tracking=true;allocations=0;failAt=position;const auto result=staged.SetAnimator(1,clip,1,true,true);const bool prepared=result==R::Success && staged.Prepare();tracking=false;failAt=0;
  if(position==0){positions=allocations;Require(prepared && positions>0,"Fault baseline failed");}else Require(!prepared && !staged.Prepare() && !staged.PublishPrepared(),"Faulted Animator transaction publishable");
  Require(live.Capture().dump()==before && live.AllocationHighWatermark()==high && live.Find(1)==pointer,"Animator allocation failure changed live World/allocator/pointer");if(position==positions)break;
 }
 std::cout<<"PASS: native scoped Animator settings/query/removal, scalar/clone candidate composition,64-operation budget,no-allocation publication and "<<positions<<" ordinary allocation failure positions (no Lua/GPU)\n";return 0;
}catch(const std::exception& error){tracking=false;std::cerr<<error.what()<<'\n';return 1;}}
