#include "engine/physics/BoundsBroadPhase.h"
#include "engine/physics/PhysicsWorld.h"
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>

void Require(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
int main() {
    using namespace Engine::Physics;using namespace Engine::Scene;
    try {
        BoundsBroadPhase tree;
        tree.Build({},0);Require(tree.Query({},0).empty(),"Empty BVH returned contacts");
        std::mt19937 random(761903);std::uniform_real_distribution<float> position(-30,30),extent(0,3);
        const auto makeBounds=[&]() {const Vector3 center{position(random),position(random),position(random)},half{extent(random),extent(random),extent(random)};return BroadPhaseBounds{center-half,center+half};};
        std::vector<BoundsBroadPhase::Proxy> proxies;
        for(size_t i=0;i<256;++i) proxies.push_back({i,makeBounds(),i%7!=0});
        tree.Build(proxies,proxies.size());
        for(size_t iteration=0;iteration<1500;++iteration) {
            const auto index=random()%proxies.size();proxies[index].bounds=makeBounds();proxies[index].enabled=iteration%9!=0;
            tree.Update(index,proxies[index].bounds,proxies[index].enabled);
            const auto query=makeBounds();const auto first=random()%(proxies.size()+1);
            std::vector<size_t> expected;
            for(const auto& proxy:proxies) if(proxy.enabled && proxy.index>=first && BoundsBroadPhase::Overlap(query,proxy.bounds)) expected.push_back(proxy.index);
            Require(tree.Query(query,first)==expected,"Refitted query disagrees with exhaustive overlap/storage order");
        }
        BroadPhaseBounds invalid{};invalid.min.x=std::numeric_limits<float>::quiet_NaN();
        tree.Build({{0,invalid,true},{1,{{0,0,0},{1,1,1}},true},{2,{{1,1,1},{2,2,2}},true}},3);
        Require(tree.Query({{0,0,0},{1,1,1}},0)==std::vector<size_t>({1,2}),"Invalid bounds or inclusive touch handling incorrect");
        tree.Update(0,{{0,0,0},{1,1,1}},true);Require(tree.Query({{0,0,0},{1,1,1}},0).size()==3,"Disabled leaf could not recover");
        tree.Update(1,invalid,true);Require(tree.Query({{0,0,0},{1,1,1}},0)==std::vector<size_t>({0,2}),"Nonfinite updated leaf poisoned ancestors");
        Require(tree.Query(invalid,0).empty(),"Invalid query not rejected");
        proxies.clear();for(size_t i=0;i<4096;++i) {const float x=float(i)*4;proxies.push_back({i,{{x,0,0},{x+1,1,1}},true});}
        tree.Build(proxies,proxies.size());size_t totalVisits=0;
        for(const auto& proxy:proxies) {size_t visited=0;Require(tree.Query(proxy.bounds,proxy.index+1,&visited).empty(),"Sparse proxies unexpectedly overlap");totalVisits+=visited;}
        Require(totalVisits<4096*40,"Sparse BVH still traverses quadratic branches");
        std::cout<<"PASS: 4096 sparse bounds queried in "<<totalVisits<<" BVH node visits (8386560 exhaustive pairs)\n";
        {
            EntityWorld world;const auto body=world.CreateEntity("Body"),left=world.CreateEntity("Left"),right=world.CreateEntity("New later contact");
            world.SetCollider(body,{});world.SetRigidBody(body,{{},1,true,false,false});world.SetCollider(left,{});world.SetCollider(right,{});
            world.Find(left)->transform.position.x=-0.6f;world.Find(right)->transform.position.x=1.25f;
            PhysicsWorld physics;physics.Step(world,0.01f);
            Require(physics.GetEvents().size()==2 && physics.GetEvents()[0].second==left && physics.GetEvents()[1].second==right,"Correction-created later contact omitted/reordered");
            Require(std::abs(world.Find(body)->transform.position.x-0.25f)<0.001f,"Sequential solver response changed");
        }
        {
            EntityWorld world;const auto wall=world.CreateEntity("Wall"),owner=world.CreateEntity("Owner"),child=world.CreateEntity("Child",owner),sibling=world.CreateEntity("Sibling",owner),probe=world.CreateEntity("Probe");
            world.SetCollider(wall,{});world.SetRigidBody(owner,{{},1,true,false,false});world.SetCollider(child,{});world.SetCollider(sibling,{});world.SetCollider(probe,{{0.1f,0.1f,0.1f},true});
            world.Find(child)->transform.position.x=-0.6f;world.Find(sibling)->transform.position.x=5;world.Find(probe)->transform.position.x=4.25f;
            PhysicsWorld physics;physics.Step(world,0.01f);
            Require(physics.GetEvents().size()==2 && physics.GetEvents()[0].first==wall && physics.GetEvents()[0].second==child && physics.GetEvents()[1].first==sibling && physics.GetEvents()[1].second==probe,"Compound owner refit failed to move all child proxies");
        }
        std::cout<<"PASS: random exhaustive parity, disabled/nonfinite recovery, sparse pruning and actual Physics correction/compound ordering\n";
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
    return 0;
}
