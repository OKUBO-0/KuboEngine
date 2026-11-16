#include "engine/physics/PhysicsWorld.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>

void Require(bool condition,const char* message) {if(!condition) throw std::runtime_error(message);}
int main() {
    using namespace Engine::Physics;using namespace Engine::Scene;
    const auto center=[](const EntityWorld& world,EntityId id) {
        Matrix4x4 matrix{};Require(world.GetWorldMatrix(id,matrix),"World pose unavailable");return Vector3{matrix.m[3][0],matrix.m[3][1],matrix.m[3][2]};
    };
    const auto close=[](float a,float b) {return std::abs(a-b)<0.002f;};
    try {
        {
            EntityWorld world;const auto parent=world.CreateEntity("Inactive parent"),body=world.CreateEntity("Child",parent),floor=world.CreateEntity("Floor");
            world.SetCollider(body,{});world.SetRigidBody(body,{{0,-2,0},1,true,true,true});world.Find(body)->transform.position.y=0.8f;
            world.SetCollider(floor,{{5,0.5f,5},false});world.Find(parent)->active=false;
            PhysicsWorld physics;physics.Step(world,0.01f);
            Require(world.Find(body)->transform.position.y==0.8f && world.Find(body)->rigidBody.velocity.y==-2 && !world.Find(body)->rigidBody.grounded && physics.GetEvents().empty(),"Inactive ancestor still simulated/collided");
            world.Find(parent)->active=true;physics.Step(world,0.01f);
            Require(close(center(world,body).y,1) && world.Find(body)->rigidBody.grounded,"Reactivated child did not resume");
            world.Find(body)->parent=body;physics.Step(world,0.01f);
            Require(!physics.GetDiagnostic().empty() && physics.GetEvents().empty(),"Cyclic direct edit was not rejected");
        }
        {
            EntityWorld world;const auto parent=world.CreateEntity("Rotated mirrored group"),shape=world.CreateEntity("Shape",parent),probe=world.CreateEntity("World probe");
            world.Find(parent)->transform={{10,20,30},{0,0,std::numbers::pi_v<float>/2},{-2,3,4}};
            world.Find(shape)->transform.position={1,0,0};world.SetCollider(shape,{{1,0.25f,0.5f},true});world.SetCollider(probe,{{0.1f,0.1f,0.1f},false});
            const auto shapeCenter=center(world,shape);Require(close(shapeCenter.x,10) && close(shapeCenter.y,18) && close(shapeCenter.z,30),"Hierarchy fixture transform incorrect");
            world.Find(probe)->transform.position={10.8f,18,30};PhysicsWorld physics;physics.Step(world,0.01f);
            Require(physics.GetEvents().size()==1 && physics.GetEvents()[0].trigger,"World rotated/negative/nonuniform bounds missed contact");
            world.Find(probe)->transform.position={10.86f,18,30};physics.Step(world,0.01f);Require(physics.GetEvents().empty(),"World transformed bounds oversized");
            world.Find(probe)->transform.position={1,0,0};physics.Step(world,0.01f);Require(physics.GetEvents().empty(),"Collider still used old local center");
        }
        {
            EntityWorld world;const auto parent=world.CreateEntity("Group"),floor=world.CreateEntity("Floor"),body=world.CreateEntity("Body",parent);
            world.Find(parent)->transform={{3,0,0},{0,0,std::numbers::pi_v<float>/2},{2,3,1}};
            world.Find(body)->transform.position={0.4f,0,0};world.SetCollider(body,{{0.25f,0.5f,0.25f},false});world.SetRigidBody(body,{{1,-2,0},1,true,false,false});
            world.SetCollider(floor,{{10,0.5f,10},false});PhysicsWorld physics;physics.Step(world,0.01f);
            const auto position=center(world,body);
            Require(close(position.x,3.01f) && close(position.y,1) && close(world.Find(body)->transform.position.x,0.5f) &&
                close(world.Find(body)->transform.position.y,-0.01f/3) && world.Find(body)->rigidBody.velocity.y==0 && world.Find(body)->rigidBody.grounded,
                "World integration/correction did not inverse-map to parent-local position");
        }
        {
            EntityWorld world;const auto parent=world.CreateEntity("Sheared group"),shape=world.CreateEntity("Rotated child",parent),probe=world.CreateEntity("Probe"),unused=world.CreateEntity("Collapsed visual only");
            world.Find(parent)->transform={{4,5,6},{0,0,std::numbers::pi_v<float>/4},{3,1,2}};
            world.Find(shape)->transform.rotation.z=std::numbers::pi_v<float>/6;world.Find(shape)->transform.scale={-1,2,1};
            world.Find(unused)->transform.scale={0,0,0};world.SetCollider(shape,{{1,0.25f,0.5f},true});world.SetCollider(probe,{{0.05f,0.05f,0.05f},false});
            Matrix4x4 matrix{};Require(world.GetWorldMatrix(shape,matrix),"Shear fixture unavailable");
            float maxX=-1e30f;
            for(const float x:{-1.0f,1.0f}) for(const float y:{-0.25f,0.25f}) for(const float z:{-0.5f,0.5f})
                maxX=std::max(maxX,x*matrix.m[0][0]+y*matrix.m[1][0]+z*matrix.m[2][0]+matrix.m[3][0]);
            world.Find(probe)->transform.position={maxX+0.04f,5,6};PhysicsWorld physics;physics.Step(world,0.01f);
            Require(physics.GetEvents().size()==1 && close(physics.GetEvents()[0].penetration,0.01f) && physics.GetDiagnostic().empty(),"Sheared corner envelope or irrelevant collapsed visual handling failed");
            world.Find(probe)->transform.position.x=maxX+0.06f;physics.Step(world,0.01f);Require(physics.GetEvents().empty(),"Sheared World envelope oversized");
            const auto fixed=world.CreateEntity("Static child",parent);world.Find(fixed)->transform.position={0.1f,0.3f,-0.2f};world.SetRigidBody(fixed,{{},1,false,false,false});
            const auto original=world.Find(fixed)->transform.position;
            for(int i=0;i<100;++i) physics.Step(world,0.01f);
            Require(world.Find(fixed)->transform.position.x==original.x && world.Find(fixed)->transform.position.y==original.y && world.Find(fixed)->transform.position.z==original.z,"Unmoved static body accumulated inverse-transform drift");
        }
        for(const bool reverse:{false,true}) {
            EntityWorld world;const auto parent=world.CreateEntity("Moving body"),attached=world.CreateEntity("Attached shape",parent),
                nested=world.CreateEntity("Independent body",parent),fixed=world.CreateEntity("Static body",parent);
            world.Find(parent)->transform.position={0,10,0};world.SetRigidBody(parent,{{2,0,0},1,true,false,false});
            world.Find(attached)->transform.position={2,0,0};world.SetCollider(attached,{{0.1f,0.1f,0.1f},true});
            world.Find(nested)->transform.position={5,0,0};world.SetRigidBody(nested,{{1,0,0},1,true,false,false});
            world.Find(fixed)->transform.position={-5,0,0};world.SetRigidBody(fixed,{{0,0,0},1,false,false,false});
            if(reverse) std::reverse(world.GetEntities().begin(),world.GetEntities().end());
            PhysicsWorld physics;for(int step=0;step<10;++step) physics.Step(world,0.1f);
            Require(close(center(world,parent).x,2) && close(center(world,attached).x,4) && close(center(world,nested).x,6) && close(center(world,fixed).x,-5),
                "Compound motion or independent nested/static bodies depended on insertion order");
        }
        {
            EntityWorld world;const auto body=world.CreateEntity("Compound owner"),child=world.CreateEntity("Child collider",body),other=world.CreateEntity("Other child",body),floor=world.CreateEntity("Floor");
            world.Find(body)->transform.position.y=0.8f;world.SetRigidBody(body,{{0,-2,0},1,true,false,false});
            world.SetCollider(child,{});world.SetCollider(other,{});world.SetCollider(floor,{{5,0.5f,5},false});
            PhysicsWorld physics;physics.Step(world,0.01f);
            Require(close(center(world,body).y,1) && world.Find(body)->rigidBody.grounded && world.Find(body)->rigidBody.velocity.y==0 && physics.GetEvents().size()==1,
                "Body-less child collider did not resolve on owner or self-collided");
            Require(world.Find(child)->transform.position.y==0 && !world.Find(child)->hasRigidBody,"Compound changed authored child pose/components");
        }
        {
            EntityWorld world;const auto parent=world.CreateEntity("Singular"),body=world.CreateEntity("Blocked body",parent),valid=world.CreateEntity("Healthy body");
            world.SetRigidBody(body,{{1,0,0},1,true,false,false});world.SetCollider(body,{});world.SetRigidBody(valid,{{1,0,0},1,true,false,false});
            world.Find(parent)->transform.scale={0,1,1};world.Find(valid)->transform.position={50,0,0};PhysicsWorld physics;physics.Step(world,0.1f);
            Require(!physics.GetDiagnostic().empty() && world.Find(body)->transform.position.x==0 && close(world.Find(valid)->transform.position.x,50.1f) && physics.GetEvents().empty(),"Singular parent poisoned healthy simulation");
            world.Find(parent)->transform.scale={1e-30f,1e-30f,1e-30f};world.Find(body)->rigidBody.velocity.x=1e10f;
            const auto beforeHealthy=world.Find(valid)->transform.position;physics.Step(world,0.1f);
            Require(physics.GetDiagnostic().find("parent-local")!=std::string::npos && world.Find(body)->transform.position.x==0 &&
                world.Find(body)->rigidBody.velocity.x==1e10f && world.Find(valid)->transform.position.x==beforeHealthy.x && physics.GetEvents().empty(),
                "Unrepresentable local publication partially mutated frame");
            world.Find(parent)->transform.scale={1,1,1};world.Find(body)->rigidBody.velocity.x=0;physics.Step(world,0.1f);
            Require(physics.GetDiagnostic().empty() && world.Find(valid)->transform.position.x>beforeHealthy.x,"Recovered hierarchy did not clear diagnostic/resume");
        }
        std::cout<<"PASS: ancestor activity, World transformed bounds, inverse parent correction, compound ownership, independent nested/static bodies, order and singular/publication isolation\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
