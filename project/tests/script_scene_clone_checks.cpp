#include "engine/script/ScriptSceneCommands.h"
#include <cstdlib>
#include <new>
#include <iostream>
#include <stdexcept>
namespace {
bool tracking=false;size_t allocations=0,failAt=0;
void Require(bool b,const char* message){if(!b)throw std::runtime_error(message);}
}
void* operator new(size_t n){if(tracking && ++allocations==failAt)throw std::bad_alloc();if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,size_t) noexcept{std::free(p);}
void operator delete[](void* p,size_t) noexcept{std::free(p);}
int main(){try{
 using namespace Engine::Scene;using namespace Engine::Scripting;
 using R=ScriptSceneCommands::Result;using Op=ScriptSceneCommands::Operation;
 EntityWorld seed;seed.CreateEntity(std::string(300,'n'));seed.CreateEntity("Child",1);seed.CreateEntity("Grandchild",2);seed.CreateEntity("Destination");
 seed.Find(1)->transform={{10,0,0},{0,0,0},{-2,3,1}};seed.Find(2)->transform.position={1,2,3};seed.Find(3)->transform.position={2,0,0};seed.Find(4)->transform.position={100,0,0};
 Require(seed.SetModel(1,"Assets/model.obj","0123456789abcdef0123456789abcdef") && seed.SetCamera(1,{}) && seed.SetCollider(1,{}) && seed.SetRigidBody(1,{{4,5,6},3,true,false,true}) && seed.SetScript(1,"Assets/source.lua") && seed.SetScriptProperty(1,"label",std::string("日本語")),"Component fixture failed");
 Engine::AudioSystem::AudioComponent sound;sound.assetPath="Assets/sound.wav";sound.loop=true;sound.volume=0.5f;Require(seed.SetAudio(1,sound),"Audio fixture failed");seed.Find(1)->script.started=true;
 Engine::AnimationSystem::AnimatorComponent animator;animator.clip=std::string(256,'a');animator.speed=2.5f;animator.loop=false;animator.playOnAwake=false;
 Require(seed.SetAnimator(1,animator),"Animator clone fixture failed");
 auto childAnimator=animator;childAnimator.clip="child clip";childAnimator.speed=0.75f;childAnimator.loop=true;
 Require(seed.SetAnimator(2,childAnimator),"Child Animator clone fixture failed");
 SceneValue reservation;reservation["entity"]=100.0;seed.Find(1)->prefabLink["mapping"]=crude_json::array{reservation};
 auto world=seed;auto self=*world.Find(1);ScriptSceneView view;ScriptSceneCommands commands;EntityId root=777,empty=0;
 {ScriptSceneView::WriteScope scope(view,world);commands.Begin(&view,self);const auto before=world.Capture().dump();
  Require(commands.Apply(1,Op::Translate,{1,0,0})==R::Success && commands.Spawn("Empty",0,empty)==R::Success && empty==101,"Prefix/Prefab reservation failed");
  Require(commands.Apply(1,Op::Translate,{2,0,0})==R::Success && commands.Apply(1,Op::Velocity,{9,8,7})==R::Success,"Post-promotion staging failed");self.transform.position.y=5;
  Require(commands.Clone(1,4,root)==R::Success && root==102 && commands.CreatedCount()==4 && commands.OperationCount()==5,"Aggregate clone identity/call count failed");
  const auto* candidate=commands.ReadWorld();const auto* copy=candidate->Find(root);
  Require(copy && copy->parent==4 && copy->name==self.name && copy->transform.position.x==13 && copy->transform.position.y==5 && copy->transform.scale.x==-2 && copy->hasModel && copy->model.assetGuid==self.model.assetGuid && copy->hasCamera && copy->hasCollider && copy->hasRigidBody && copy->rigidBody.mass==3 && copy->rigidBody.velocity.x==9 && !copy->rigidBody.grounded && copy->hasScript && !copy->script.started && copy->script.properties==self.script.properties && copy->hasAudio && copy->audio.assetPath==sound.assetPath && copy->audio.loop && copy->audio.volume==0.5f && copy->prefabLink.is_null(),"Clone lost components/latest candidate or copied transient/link state");
  Require(candidate->Find(103)->parent==root && candidate->Find(104)->parent==103,"Clone child IDs not remapped");Matrix4x4 matrix{};
  Require(copy->hasAnimator && copy->animator.clip==animator.clip && copy->animator.speed==2.5f && !copy->animator.loop && !copy->animator.playOnAwake &&
   candidate->Find(103)->hasAnimator && candidate->Find(103)->animator.clip==childAnimator.clip && candidate->Find(103)->animator.speed==0.75f &&
   candidate->Find(103)->animator.loop && !candidate->Find(104)->hasAnimator,"Runtime subtree clone lost authored Animator settings/absence");
  Require(commands.WorldMatrix(104,matrix) && matrix.m[3][0]==107 && matrix.m[3][1]==11 && matrix.m[3][2]==3 && world.Capture().dump()==before,"Clone hierarchy/local TRS/live isolation failed");
  Require(commands.Prepare(),"Clone preparation failed");allocations=0;failAt=1;tracking=true;const bool published=commands.PublishPrepared();tracking=false;failAt=0;
  Require(published && allocations==0 && world.Find(root) && world.Find(1)->script.started && world.Find(1)->rigidBody.grounded && !world.Find(root)->script.started && !world.Find(root)->rigidBody.grounded,"Publication allocated or reset source transients");commands.Clear();
  auto changedAnimator=animator;changedAnimator.clip="independent clone";
  Require(world.SetAnimator(root,changedAnimator) && world.Find(1)->animator.clip==animator.clip && world.Find(root)->animator.clip==changedAnimator.clip,
   "Published clone shares mutable Animator string ownership");
 }
 // Creations are counted even when immediately deleted. A multi-node clone
 // cannot multiply the 64-call budget into thousands of generated Entities.
 EntityWorld limits;limits.CreateEntity("Root");limits.CreateEntity("Child",1);auto owner=*limits.Find(1);
 {ScriptSceneView::WriteScope scope(view,limits);commands.Begin(&view,owner);
  for(int i=0;i<32;++i)Require(commands.Clone(1,0,root)==R::Success,"Clone aggregate cap early");
  Require(commands.CreatedCount()==64 && commands.OperationCount()==32 && commands.Clone(1,0,root)==R::Limit && commands.Spawn("Extra",0,empty)==R::Limit,"Clone aggregate creation cap bypassed");
  Require(commands.Destroy(root,true)==R::Success && commands.Clone(1,0,root)==R::Limit,"Deleting clones refunded creation budget");commands.Clear();
  commands.Begin(&view,owner);root=777;Require(commands.Clone(999,0,root)==R::Missing && commands.Clone(1,999,root)==R::Missing && commands.Clone(0,0,root)==R::Invalid && root==777 && commands.OperationCount()==0,"Missing/invalid clone consumed identity");
 }
 // Invalid selected component data must not publish preceding scalar changes.
 auto invalid=seed;invalid.Find(1)->rigidBody.mass=-1;auto invalidSelf=*invalid.Find(4);
 {ScriptSceneView::WriteScope scope(view,invalid);commands.Begin(&view,invalidSelf);root=777;
  Require(commands.Apply(4,Op::Translate,{5,0,0})==R::Success && commands.Clone(1,0,root)==R::Invalid && root==777 && !commands.Prepare() && !commands.PublishPrepared() && invalid.Find(4)->transform.position.x==100,"Invalid component clone partially published");}
 auto invalidAnimator=seed;invalidAnimator.Find(1)->animator.speed=-1;auto invalidAnimatorSelf=*invalidAnimator.Find(4);
 {ScriptSceneView::WriteScope scope(view,invalidAnimator);commands.Begin(&view,invalidAnimatorSelf);root=777;
  Require(commands.Apply(4,Op::Translate,{5,0,0})==R::Success && commands.Clone(1,0,root)!=R::Success && root==777 && !commands.Prepare() && !commands.PublishPrepared() &&
   invalidAnimator.Find(4)->transform.position.x==100 && invalidAnimator.Find(1)->animator.speed==-1,"Invalid Animator clone partially published preceding edits");}
 // Fail every observed ordinary allocation across promotion/template validation/
 // private component copying/Prepare. Restore's bool failure may be Invalid,
 // but it must poison the transaction just like a propagated allocation error.
 size_t positions=0;
 for(size_t position=0;;++position){auto live=seed;auto record=*live.Find(4);ScriptSceneView localView;ScriptSceneView::WriteScope scope(localView,live);ScriptSceneCommands local;local.Begin(&localView,record);
  const auto snapshot=live.Capture().dump();const auto high=live.AllocationHighWatermark();Require(local.Apply(4,Op::Translate,{5,0,0})==R::Success,"Allocation prefix failed");root=777;
  allocations=0;failAt=position;tracking=true;const auto result=local.Clone(1,4,root);const bool prepared=result==R::Success && local.Prepare();tracking=false;failAt=0;
  if(position==0){positions=allocations;Require(prepared && positions>0 && root==101,"Clone baseline failed");}
  else Require(!prepared && !local.Prepare() && !local.PublishPrepared(),"Faulted clone transaction could publish");
  Require(live.Capture().dump()==snapshot && live.AllocationHighWatermark()==high,"Clone allocation fault modified live World/allocator");if(position==positions)break;
 }
 std::cout<<"PASS: native component subtree clone, fresh/reserved IDs and local hierarchy, latest scalar/self candidate, shared asset refs and copied properties, fresh script/grounded state, no links/no-allocation publication, aggregate creation cap/refusal and "<<positions<<" ordinary allocation fault positions (no Lua/render/device integration)\n";return 0;
}catch(const std::exception& e){tracking=false;std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
