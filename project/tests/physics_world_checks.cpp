#include "../engine/physics/PhysicsWorld.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void CheckMassResponse()
{
    using namespace Engine::Physics;
    using namespace Engine::Scene;
    for (const bool reverseOrder : {false, true}) {
        EntityWorld world;
        const auto first = world.CreateEntity("First");
        const auto second = world.CreateEntity("Second");
        const auto light = reverseOrder ? second : first;
        const auto heavy = reverseOrder ? first : second;
        world.Find(light)->transform.position.x = -0.4f;
        world.Find(heavy)->transform.position.x = 0.4f;
        world.SetCollider(light, {});
        world.SetCollider(heavy, {});
        world.SetRigidBody(light, {{4.0f, 0.0f, 2.0f}, 1.0f, true, false, false});
        world.SetRigidBody(heavy, {{0.0f, 0.0f, 0.0f}, 3.0f, true, false, false});
        PhysicsWorld physics;
        physics.Step(world, 0.01f);
        Require(std::abs(world.Find(light)->transform.position.x + 0.54f) < 0.001f &&
            std::abs(world.Find(heavy)->transform.position.x - 0.46f) < 0.001f,
            "Penetration correction ignored inverse mass or entity order");
        Require(std::abs(world.Find(light)->rigidBody.velocity.x - 1.0f) < 0.001f &&
            std::abs(world.Find(heavy)->rigidBody.velocity.x - 1.0f) < 0.001f,
            "Inelastic collision did not conserve pair momentum");
        Require(world.Find(light)->rigidBody.velocity.z == 2.0f,
            "Normal impulse changed tangential velocity");

        world.Find(light)->transform.position = {-0.4f, 0.0f, 0.0f};
        world.Find(heavy)->transform.position = {0.4f, 0.0f, 0.0f};
        world.Find(light)->rigidBody.velocity = {-2.0f, 0.0f, 0.0f};
        world.Find(heavy)->rigidBody.velocity = {2.0f, 0.0f, 0.0f};
        physics.Step(world, 0.01f);
        Require(world.Find(light)->rigidBody.velocity.x == -2.0f && world.Find(heavy)->rigidBody.velocity.x == 2.0f,
            "Separating contact erased outgoing velocity");

        world.Find(light)->transform.position = {-0.4f, 0.0f, 0.0f};
        world.Find(heavy)->transform.position = {0.4f, 0.0f, 0.0f};
        world.Find(light)->collider.isTrigger = true;
        physics.Step(world, 0.01f);
        Require(std::abs(world.Find(light)->transform.position.x + 0.42f) < 0.001f &&
            std::abs(world.Find(heavy)->transform.position.x - 0.42f) < 0.001f &&
            physics.GetEvents().size() == 1 && physics.GetEvents()[0].trigger,
            "Trigger contact applied a solid response");
    }

    EntityWorld world;
    const auto floor = world.CreateEntity("Floor");
    const auto body = world.CreateEntity("Jumping");
    world.SetCollider(floor, {});
    world.SetCollider(body, {});
    world.Find(body)->transform.position.y = 0.8f;
    world.SetRigidBody(body, {{0.0f, 2.0f, 0.0f}, 1.0f, true, false, true});
    PhysicsWorld physics;
    physics.Step(world, 0.01f);
    Require(world.Find(body)->rigidBody.velocity.y == 2.0f && !world.Find(body)->rigidBody.grounded,
        "Separating upward body lost its jump or remained grounded");
    world.Find(body)->rigidBody.mass = 0.0f; // Direct component edits must not divide by zero.
    physics.Step(world, 0.01f);
    Require(std::isfinite(world.Find(body)->transform.position.y), "Invalid mass poisoned simulation");
    world.Find(body)->collider.halfExtents.x = std::numeric_limits<float>::quiet_NaN();
    physics.Step(world, 0.01f);
    Require(physics.GetEvents().empty(), "Invalid collider emitted a collision event");
}

void CheckMaterials()
{
    using namespace Engine::Physics;using namespace Engine::Scene;
    const auto close=[](float a,float b) {return std::abs(a-b)<0.001f;};
    for(const bool reverse:{false,true}) {
        EntityWorld world;const auto a=world.CreateEntity("A"),b=world.CreateEntity("B");
        const auto light=reverse ? b : a,heavy=reverse ? a : b;
        world.SetCollider(light,{{0.5f,0.5f,0.5f},false,0.25f,1.0f});
        world.SetCollider(heavy,{{0.5f,0.5f,0.5f},false,1.0f,0.0f});
        world.SetRigidBody(light,{{4,0,0},1,true,false,false});world.SetRigidBody(heavy,{{0,0,0},3,true,false,false});
        const auto reset=[&](Vector3 lv,Vector3 hv) {
            world.Find(light)->transform.position={-0.4f,0,0};world.Find(heavy)->transform.position={0.4f,0,0};
            world.Find(light)->rigidBody.velocity=lv;world.Find(heavy)->rigidBody.velocity=hv;
        };
        PhysicsWorld physics;reset({4,0,0},{0,0,0});physics.Step(world,0.01f);
        Require(close(world.Find(light)->rigidBody.velocity.x,-2) && close(world.Find(heavy)->rigidBody.velocity.x,2),"Elastic mass-weighted response or order independence failed");
        const double energy=world.Find(light)->rigidBody.velocity.x*world.Find(light)->rigidBody.velocity.x+3.0*world.Find(heavy)->rigidBody.velocity.x*world.Find(heavy)->rigidBody.velocity.x;
        Require(std::abs(energy-16)<0.001,"Elastic collision changed kinetic energy");
        world.Find(light)->collider.restitution=0.5f;reset({4,0,0},{0,0,0});physics.Step(world,0.01f);
        Require(close(world.Find(light)->rigidBody.velocity.x,-0.5f) && close(world.Find(heavy)->rigidBody.velocity.x,1.5f),"Partial restitution/momentum failed");
        world.Find(light)->collider.restitution=0;reset({4,0,4},{0,0,0});physics.Step(world,0.01f);
        Require(close(world.Find(light)->rigidBody.velocity.z,2.5f) && close(world.Find(heavy)->rigidBody.velocity.z,0.5f),"Geometric friction blend/Coulomb cap/momentum failed");
        reset({4,0,1},{0,0,0});physics.Step(world,0.01f);
        Require(close(world.Find(light)->rigidBody.velocity.z,0.25f) && close(world.Find(heavy)->rigidBody.velocity.z,0.25f),"Friction reversed rather than stopping relative tangent motion");
        world.Find(heavy)->collider.friction=0;reset({4,0,4},{0,0,0});physics.Step(world,0.01f);
        Require(close(world.Find(light)->rigidBody.velocity.z,4) && world.Find(heavy)->rigidBody.velocity.z==0,"Zero material did not disable pair friction");
        world.Find(heavy)->collider.friction=1;world.Find(light)->collider.restitution=1;
        reset({-2,0,4},{2,0,0});physics.Step(world,0.01f);
        Require(world.Find(light)->rigidBody.velocity.x==-2 && world.Find(light)->rigidBody.velocity.z==4 && world.Find(heavy)->rigidBody.velocity.x==2,"Material affected separating contact");
        world.Find(light)->collider.isTrigger=true;reset({4,0,4},{0,0,0});physics.Step(world,0.01f);
        Require(world.Find(light)->rigidBody.velocity.x==4 && world.Find(light)->rigidBody.velocity.z==4 && physics.GetEvents().size()==1 && physics.GetEvents()[0].trigger,"Material affected trigger contact");
        world.Find(light)->collider.isTrigger=false;
        const auto extreme=std::numeric_limits<float>::max()*0.9f;reset({extreme,0,0},{-extreme,0,0});
        physics.Step(world,std::numeric_limits<float>::denorm_min());
        Require(std::isfinite(world.Find(light)->rigidBody.velocity.x) && world.Find(light)->rigidBody.velocity.x==extreme && world.Find(heavy)->rigidBody.velocity.x==-extreme,"Overflowing bounce poisoned or partially applied pair velocity");
    }
    EntityWorld world;const auto floor=world.CreateEntity("Floor"),body=world.CreateEntity("Body");
    world.SetCollider(floor,{{5,0.5f,5},false,0.25f,0.0f});world.SetCollider(body,{{0.5f,0.5f,0.5f},false,1.0f,1.0f});
    world.SetRigidBody(body,{{5,-2,0},1,true,false,false});world.Find(body)->transform.position.y=0.8f;
    PhysicsWorld physics;physics.Step(world,0.01f);
    Require(close(world.Find(body)->rigidBody.velocity.y,2) && close(world.Find(body)->rigidBody.velocity.x,3) && !world.Find(body)->rigidBody.grounded,"Static surface friction/bounce/grounded response failed");
    world.Find(body)->transform.position={0,0.8f,0};world.Find(body)->rigidBody.velocity={0,-0.1f,0};physics.Step(world,0.01f);
    Require(world.Find(body)->rigidBody.velocity.y==0 && world.Find(body)->rigidBody.grounded,"Low-speed restitution threshold did not preserve support");
    world.Find(body)->transform.position={0,0.8f,0};world.Find(body)->rigidBody.velocity={2,-2,0};
    world.Find(body)->collider.friction=std::numeric_limits<float>::quiet_NaN();world.Find(body)->collider.restitution=std::numeric_limits<float>::infinity();
    physics.Step(world,0.01f);
    Require(world.Find(body)->rigidBody.velocity.x==2 && world.Find(body)->rigidBody.velocity.y==0,"Nonfinite direct material edit poisoned simulation");
}
}

int main()
{
    using namespace Engine::Physics;
    using namespace Engine::Scene;
    try {
        CheckMassResponse();
        CheckMaterials();
        EntityWorld world;
        const EntityId floor = world.CreateEntity("Floor");
        const EntityId player = world.CreateEntity("Player");
        world.Find(floor)->transform.position = {0.0f, -1.0f, 0.0f};
        world.Find(player)->transform.position = {0.0f, 2.0f, 0.0f};
        Require(world.SetCollider(floor, {{5.0f, 0.5f, 5.0f}, false}), "Floor collider was not assigned");
        Require(world.SetCollider(player, {{0.5f, 0.5f, 0.5f}, false}), "Player collider was not assigned");
        Require(world.SetRigidBody(player, {{0.0f, 0.0f, 0.0f}, 1.0f, true, true, false}), "Player body was not assigned");

        PhysicsWorld physics;
        for (int frame = 0; frame < 120; ++frame) physics.Step(world, 1.0f / 60.0f);
        const auto* playerRecord = world.Find(player);
        Require(playerRecord && std::fabs(playerRecord->transform.position.y - 0.0f) < 0.01f, "Player did not rest on floor");
        Require(playerRecord->rigidBody.grounded, "Player was not marked grounded");
        Require(!physics.GetEvents().empty(), "Collision event was not emitted");

        const EntityId trigger = world.CreateEntity("Trigger");
        world.Find(trigger)->transform.position = {0.0f, 0.0f, 0.0f};
        Require(world.SetCollider(trigger, {{1.0f, 1.0f, 1.0f}, true}), "Trigger collider was not assigned");
        physics.Step(world, 1.0f / 60.0f);
        bool foundTrigger = false;
        for (const auto& event : physics.GetEvents()) foundTrigger = foundTrigger || event.trigger;
        Require(foundTrigger, "Trigger collision event was not emitted");
        std::cout << "PASS: gravity, mass-weighted AABB response, pair momentum, separation, grounded and trigger events\n";
        std::cout << "PASS: elastic/partial bounce, friction blend/cap/stop, momentum/energy, static support threshold, order/trigger/separation and overflow/nonfinite isolation\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
