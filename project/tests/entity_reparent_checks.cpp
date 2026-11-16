#include "engine/scene/entity/EntityWorld.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

void Require(bool ok,const char* why) {if(!ok) throw std::runtime_error(why);}
Matrix4x4 Pose(const Engine::Scene::EntityWorld& world,Engine::Scene::EntityId id) {
    Matrix4x4 result{};Require(world.GetWorldMatrix(id,result),"Missing World matrix");return result;
}
void Same(const Matrix4x4& a,const Matrix4x4& b) {
    for(int i=0;i<4;++i) for(int j=0;j<4;++j) Require(std::abs(double(a.m[i][j])-b.m[i][j])<0.001*std::max(1.0,std::abs(double(a.m[i][j]))),"World pose changed during keep-World reparent");
}
int main() {
    using namespace Engine::Scene;
    try {
        EntityWorld world;const auto a=world.CreateEntity("A"),b=world.CreateEntity("B"),child=world.CreateEntity("Child",a),grand=world.CreateEntity("Grandchild",child);
        world.Find(a)->transform={{7,2,-5},{0.2f,0.3f,-0.4f},{-2,2,2}};
        world.Find(b)->transform={{-4,3,8},{-0.1f,0.4f,0.2f},{3,3,3}};
        world.Find(child)->transform={{1,2,3},{0.5f,1.7f,0.8f},{1,2,-3}};
        world.Find(grand)->transform.position={3,-1,2};
        const auto before=Pose(world,child),descendant=Pose(world,grand);std::string error;
        Require(world.SetParent(child,b,true,error),error.c_str());Same(before,Pose(world,child));Same(descendant,Pose(world,grand));
        Require(world.SetParent(child,0,true,error),error.c_str());Same(before,Pose(world,child));Same(descendant,Pose(world,grand));
        const auto unchanged=world.Capture().dump();Require(world.SetParent(child,0,true,error) && world.Capture().dump()==unchanged,"No-op parent changed authored TRS");
        world.Find(b)->transform.scale={0,1,1};
        const auto singular=world.Capture().dump();Require(!world.SetParent(child,b,true,error) && !error.empty() && world.Capture().dump()==singular,"Singular failure changed parent/Transform");
        world.Find(b)->transform={{0,0,0},{0,0,0},{1e-38f,1e-38f,1e-38f}};
        const auto overflow=world.Capture().dump();Require(!world.SetParent(child,b,true,error) && world.Capture().dump()==overflow,"Out-of-float-range local TRS published");
        world.Find(b)->transform={{1,2,3},{0,0,0.7f},{2,3,1}};
        const auto shear=world.Capture().dump();Require(!world.SetParent(child,b,true,error) && world.Capture().dump()==shear,"Unrepresentable shear approximated/published");
        Require(!world.SetParent(child,grand,true,error) && world.Capture().dump()==shear,"Cycle accepted");
        const auto local=world.Find(child)->transform;Require(world.SetParent(child,b),"Legacy local-preserving parent rejected");
        Require(world.Find(child)->transform.position.x==local.position.x && world.Find(child)->transform.rotation.y==local.rotation.y && world.Find(child)->transform.scale.z==local.scale.z,"Legacy API changed local TRS");
        std::cout<<"PASS: mirrored/uniform rotated parent World preservation, descendants, root/no-op, near-gimbal Euler, singular/shear/cycle refusal and legacy local TRS\n";
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
    return 0;
}
