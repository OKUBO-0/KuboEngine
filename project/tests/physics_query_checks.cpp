#include "../engine/physics/PhysicsWorld.h"
#include "../engine/script/ScriptSceneCommands.h"
#include <iostream>
#include <numbers>
#include <limits>
#include <stdexcept>
#include <cstdlib>
namespace {bool tracking=false,injected=false;size_t positions=0,failAt=0;}
void* operator new(size_t bytes){if(tracking && ++positions==failAt){injected=true;tracking=false;throw std::bad_alloc();}if(auto* memory=std::malloc(bytes?bytes:1))return memory;throw std::bad_alloc();}
void* operator new[](size_t bytes){return ::operator new(bytes);}
void operator delete(void* memory) noexcept{std::free(memory);}void operator delete[](void* memory) noexcept{std::free(memory);}
void operator delete(void* memory,size_t) noexcept{std::free(memory);}void operator delete[](void* memory,size_t) noexcept{std::free(memory);}
using namespace Engine;using namespace Engine::Physics;using namespace Engine::Scene;
void Need(bool value,const char* message){if(!value)throw std::runtime_error(message);}
bool Close(float a,float b){return std::abs(a-b)<0.001f;}
int main(){try{
    EntityWorld world;auto first=world.CreateEntity("First"),second=world.CreateEntity("Second"),inactive=world.CreateEntity("Inactive");
    world.SetCollider(first,{{1,1,1},false});world.Find(first)->transform.position={5,0,0};
    world.SetCollider(second,{{1,1,1},true});world.Find(second)->transform.position={8,0,0};world.SetCollider(inactive,{});world.Find(inactive)->active=false;
    const auto before=world.Capture().dump();PhysicsRayHit hit;std::vector<EntityId> ids;
    Need(PhysicsWorld::Raycast(world,{0,0,0},{2,0,0},20,hit)==PhysicsQueryResult::Success && hit.entity==first && hit.distance==4 && hit.position.x==4 && hit.normal.x==-1 && !hit.trigger,"Ray closest/distance normalization/normal failed");
    Need(PhysicsWorld::Raycast(world,{20,0,0},{-1,0,0},20,hit)==PhysicsQueryResult::Success && hit.entity==second && hit.distance==11 && hit.normal.x==1 && hit.trigger,"Negative ray or trigger failed");
    Need(PhysicsWorld::Raycast(world,{0,0,0},{1,0,0},20,hit,{first,false})==PhysicsQueryResult::Success && hit.entity==0,"Ignore/trigger filter failed");
    Need(PhysicsWorld::Raycast(world,{5,0,0},{1,0,0},0,hit)==PhysicsQueryResult::Success && hit.entity==first && hit.distance==0 && hit.normal.x==0,"Inside point ray failed");
    Need(PhysicsWorld::Raycast(world,{0,0,0},{1,0,0},4,hit)==PhysicsQueryResult::Success && hit.entity==first,"Inclusive maximum ray distance failed");
    Need(PhysicsWorld::OverlapBox(world,{6,0,0},{1,0,0},ids)==PhysicsQueryResult::Success && ids==std::vector<EntityId>{first,second},"Inclusive overlap/order failed");
    hit.entity=999;Need(PhysicsWorld::Raycast(world,{},{},1,hit)==PhysicsQueryResult::Invalid && hit.entity==999,"Invalid query replaced output");ids={999};
    Need(PhysicsWorld::OverlapBox(world,{}, {-1,0,0},ids)==PhysicsQueryResult::Invalid && ids[0]==999,"Invalid overlap replaced output");
    Need(PhysicsWorld::Raycast(world,{},{1,0,0},std::numeric_limits<float>::infinity(),hit)==PhysicsQueryResult::Invalid,"Nonfinite range accepted");
    Need(world.Capture().dump()==before,"Read-only queries changed World");
    world.Find(second)->transform.position={5,0,0};std::reverse(world.GetEntities().begin(),world.GetEntities().end());
    Need(PhysicsWorld::Raycast(world,{},{1,0,0},20,hit)==PhysicsQueryResult::Success && hit.entity==first,"Equal-distance tie depends on storage order");
    auto parent=world.CreateEntity("Parent"),child=world.CreateEntity("Child",parent);
    world.Find(parent)->transform={{10,20,30},{0,0,std::numbers::pi_v<float>/2},{-2,3,4}};world.Find(child)->transform.position={1,0,0};world.SetCollider(child,{{1,0.25f,0.5f},false});
    Need(PhysicsWorld::Raycast(world,{0,18,30},{1,0,0},20,hit)==PhysicsQueryResult::Success && hit.entity==child && Close(hit.distance,9.25f),"Rotated mirrored nonuniform hierarchical World bounds failed");
    world.Find(parent)->active=false;Need(PhysicsWorld::OverlapBox(world,{10,18,30},{2,2,2},ids)==PhysicsQueryResult::Success && ids.empty(),"Inactive ancestor queried");world.Find(parent)->active=true;
    world.Find(parent)->transform={{0,0,0},{0,0,0},{1e-12f,1,1}};world.Find(child)->transform={{0,0,0},{0,0,0},{1e12f,1,1}};
    Need(PhysicsWorld::OverlapBox(world,{},{1,1,1},ids)==PhysicsQueryResult::Success && ids.empty(),"Rejected singular ancestor was compensated back into query");
    world.Find(parent)->parent=child;Need(PhysicsWorld::OverlapBox(world,{},{1,1,1},ids)==PhysicsQueryResult::Success && ids.empty(),"Cyclic hierarchy queried or looped");world.Find(parent)->parent=0;
    PhysicsQueryBudget budget;for(size_t i=0;i<PhysicsQueryBudget::MaxQueries;++i)Need(PhysicsWorld::Raycast(world,{},{1,0,0},20,hit,{}, {},&budget)==PhysicsQueryResult::Success,"Budget admitted too few queries");
    hit.entity=999;Need(PhysicsWorld::Raycast(world,{},{1,0,0},20,hit,{}, {},&budget)==PhysicsQueryResult::Limit && budget.exhausted && hit.entity==999,"Query count limit did not preserve output");
    budget={};budget.work=PhysicsQueryBudget::MaxWork;Need(PhysicsWorld::Raycast(world,{},{1,0,0},20,hit,{}, {},&budget)==PhysicsQueryResult::Limit,"Work budget exceeded silently");
    EntityWorld many;for(size_t i=0;i<PhysicsQueryBudget::MaxHits+1;++i){const auto id=many.CreateEntity("Shape");many.SetCollider(id,{});}ids={999};
    Need(PhysicsWorld::OverlapBox(many,{},{2,2,2},ids)==PhysicsQueryResult::Limit && ids==std::vector<EntityId>{999},"Overlap result truncated instead of failing atomically");
    // Query the staged self/peer scalar state and structural Collider candidate.
    EntityWorld staged;auto self=staged.CreateEntity("Self"),target=staged.CreateEntity("Target");staged.SetCollider(target,{});staged.Find(target)->transform.position={10,0,0};
    Scripting::ScriptSceneView view;Scripting::ScriptSceneCommands commands;auto detached=*staged.Find(self);
    {Scripting::ScriptSceneView::WriteScope scope(view,staged);commands.Begin(&view,detached);
        Need(commands.Apply(target,Scripting::ScriptSceneCommands::Operation::Position,{3,0,0})==Scripting::ScriptSceneCommands::Result::Success,"Staged move failed");
        Need(commands.Raycast({},{1,0,0},10,hit)==Scripting::ScriptSceneCommands::Result::Success && hit.entity==target && hit.distance==2.5f && staged.Find(target)->transform.position.x==10,"Query missed staged peer transform or published it early");
        Need(commands.SetCollider(target,{{1,1,1},true})==Scripting::ScriptSceneCommands::Result::Success,"Staged Collider failed");
        Need(commands.Raycast({},{1,0,0},10,hit)==Scripting::ScriptSceneCommands::Result::Success && hit.distance==2 && hit.trigger,"Query missed staged Collider");
        Need(commands.Apply(target,Scripting::ScriptSceneCommands::Operation::Active,{},false)==Scripting::ScriptSceneCommands::Result::Success,"Staged active failed");
        Need(commands.OverlapBox({3,0,0},{1,1,1},ids)==Scripting::ScriptSceneCommands::Result::Success && ids.empty(),"Query missed post-promotion active override");commands.Clear();
    }
    Need(staged.Find(target)->transform.position.x==10 && !staged.Find(target)->collider.isTrigger && staged.Find(target)->active,"Read-only query or uncommitted commands leaked");
    Need(commands.Raycast({},{1,0,0},10,hit)==Scripting::ScriptSceneCommands::Result::Unavailable,"Expired query scope accepted");
    const auto stable=staged.Capture().dump();size_t total=0;
    for(bool overlap:{false,true}){
        PhysicsRayHit oldHit;oldHit.entity=999;std::vector<EntityId> oldIds{999};positions=0;failAt=0;tracking=true;
        const auto control=overlap?PhysicsWorld::OverlapBox(staged,{10,0,0},{1,1,1},oldIds):PhysicsWorld::Raycast(staged,{},{1,0,0},20,oldHit);tracking=false;
        Need(control==PhysicsQueryResult::Success,"Allocation control query failed");const auto count=positions;total+=count;
        for(size_t fault=1;fault<=count;++fault){
            oldHit.entity=999;oldIds={999};positions=0;failAt=fault;injected=false;tracking=true;
            const auto result=overlap?PhysicsWorld::OverlapBox(staged,{10,0,0},{1,1,1},oldIds):PhysicsWorld::Raycast(staged,{},{1,0,0},20,oldHit);tracking=false;
            Need(injected && result==PhysicsQueryResult::AllocationFailure && (overlap?oldIds==std::vector<EntityId>{999}:oldHit.entity==999) && staged.Capture().dump()==stable,"Query allocation failure leaked native output or World");
        }
    }
    std::cout<<"QUERY_ORDINARY_ALLOCATION_FAILURE_POSITIONS="<<total<<'\n';
    std::cout<<"PASS: native current-pose AABB rays/overlaps, stable IDs/filters/boundaries/hierarchy, singular/cycle exclusion, bounded all-or-nothing results and staged Script query view\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
