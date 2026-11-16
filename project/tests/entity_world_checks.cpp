#include "../engine/scene/entity/EntityWorld.h"

#include <Windows.h>
#include <iostream>
#include <stdexcept>
#include <cmath>
#include <numbers>
#include <limits>

namespace {
void Require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}
}

int main()
{
    using namespace Engine::Scene;
    const auto directory = std::filesystem::temp_directory_path() /
        ("kubo-entity-world-checks-" + std::to_string(GetCurrentProcessId()));
    try {
        EntityWorld world;
        const EntityId root = world.CreateEntity("Root");
        const EntityId child = world.CreateEntity("Child", root);
        Require(world.SetModel(child, "Assets/Models/cube.glb"), "Model component was not assigned");
        Require(world.SetCollider(child,{{0.5f,0.5f,0.5f},false,0.25f,0.75f}),"Physics material was not assigned");
        Require(!world.SetParent(root, child), "Hierarchy cycle must be rejected");
        world.Find(root)->transform.position = {10.0f, 0.0f, 0.0f};
        world.Find(root)->transform.scale = {2.0f, 3.0f, 1.0f};
        world.Find(root)->transform.rotation.z = std::numbers::pi_v<float> / 2.0f;
        world.Find(child)->transform.position = {1.0f, 0.0f, 0.0f};
        Matrix4x4 composed{};
        Require(world.GetWorldMatrix(child, composed), "World transform composition failed");
        Require(std::abs(composed.m[3][0] - 10.0f) < 0.001f && std::abs(composed.m[3][1] - 2.0f) < 0.001f,
            "Parent rotation, scale and translation were not applied to child position");

        const auto snapshot = world.Capture();
        EntityWorld restored;
        std::string error;
        Require(restored.Restore(snapshot, error), error.c_str());
        Require(restored.GetEntities().size() == 2, "Entity count was not restored");
        Require(restored.Find(child) && restored.Find(child)->parent == root, "Parent was not restored");
        Require(restored.Find(child)->hasModel, "Model component was not restored");
        Require(restored.Find(child)->collider.friction==0.25f && restored.Find(child)->collider.restitution==0.75f,"Physics material roundtrip failed");
        auto legacy=snapshot;
        for(auto& entity:legacy["entities"].get<crude_json::array>()) {entity["collider"].erase("friction");entity["collider"].erase("restitution");}
        EntityWorld legacyWorld;
        Require(legacyWorld.Restore(legacy,error) && legacyWorld.Find(child)->collider.friction==0 && legacyWorld.Find(child)->collider.restitution==0,"Legacy material defaults changed");
        auto cyclic = snapshot;
        cyclic["entities"][0]["parent"] = double(child);
        const auto before = restored.Capture().dump();
        Require(!restored.Restore(cyclic, error), "Cyclic scene must be rejected");
        Require(restored.Capture().dump() == before, "Rejected cyclic scene mutated the world");

        const float nan = std::numeric_limits<float>::quiet_NaN();
        Require(!restored.SetCollider(child, {{nan, 1.0f, 1.0f}, false}), "Non-finite collider accepted");
        Require(!restored.SetRigidBody(child, {{nan, 0.0f, 0.0f}, 1.0f, true, true, false}), "Non-finite velocity accepted");
        auto invalidPhysics = snapshot;
        invalidPhysics["entities"][0]["rigidBody"]["mass"] = -1.0;
        Require(!restored.Restore(invalidPhysics, error), "Negative persisted mass accepted");
        invalidPhysics = snapshot;
        invalidPhysics["entities"][0]["collider"]["halfExtents"][0] = -1.0;
        Require(!restored.Restore(invalidPhysics, error), "Negative persisted half extent accepted");
        invalidPhysics = snapshot;
        invalidPhysics["entities"][0]["transform"]["position"][0] = 1.0e100;
        Require(!restored.Restore(invalidPhysics, error), "Float-overflowing transform accepted");
        Require(restored.Capture().dump() == before, "Invalid physics input mutated the scene");
        for(const auto key:{"friction","restitution"}) {
            for(const auto& invalid:{crude_json::value(-0.1),crude_json::value(1.1),crude_json::value(std::numeric_limits<double>::infinity()),crude_json::value("bad"),crude_json::value(true),crude_json::value(nullptr)}) {
                auto bad=snapshot;bad["entities"][1]["collider"][key]=invalid;
                Require(!restored.Restore(bad,error) && restored.Capture().dump()==before,"Invalid persisted material mutated world");
            }
        }
        for(const float invalid:{-0.1f,1.1f,nan,std::numeric_limits<float>::infinity()}) {
            auto material=restored.Find(child)->collider;material.friction=invalid;
            Require(!restored.SetCollider(child,material),"Invalid friction setter accepted");
            material=restored.Find(child)->collider;material.restitution=invalid;
            Require(!restored.SetCollider(child,material),"Invalid restitution setter accepted");
        }
        Require(restored.Capture().dump()==before,"Rejected material setter changed world");

        const auto path = directory / L"world.json";
        Require(world.Save(path, error), error.c_str());
        EntityWorld loaded;
        Require(loaded.Load(path, error), error.c_str());
        Require(loaded.GetEntities().size() == 2, "Saved EntityWorld did not round-trip");
        Require(loaded.Find(child)->collider.friction==0.25f && loaded.Find(child)->collider.restitution==0.75f,"Saved materials did not roundtrip");

        Require(loaded.DestroyEntity(root), "Root entity was not destroyed");
        Require(loaded.Find(child) && loaded.Find(child)->parent == 0, "Children were not reparented");
        std::filesystem::remove_all(directory);
        std::cout << "PASS: entity hierarchy, component roundtrip, cycle rejection, persistence\n";
        return 0;
    } catch (const std::exception& exception) {
        std::filesystem::remove_all(directory);
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
