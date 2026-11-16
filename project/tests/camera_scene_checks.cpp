#include "../engine/scene/entity/EntityWorld.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace {
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
bool Near(float a, float b) { return std::abs(a - b) < 0.001f; }
void CheckRigid(const Matrix4x4& matrix)
{
    for (int row = 0; row < 3; ++row) {
        float length = 0;
        for (int col = 0; col < 3; ++col) length += matrix.m[row][col] * matrix.m[row][col];
        Require(Near(length, 1.0f), "Camera retained inherited scale");
        for (int other = row + 1; other < 3; ++other) {
            float dot = 0;
            for (int col = 0; col < 3; ++col) dot += matrix.m[row][col] * matrix.m[other][col];
            Require(Near(dot, 0.0f), "Camera retained inherited shear");
        }
    }
    const auto identity = matrix * matrix.Inverse();
    for (int row = 0; row < 4; ++row) for (int col = 0; col < 4; ++col)
        Require(Near(identity.m[row][col], row == col ? 1.0f : 0.0f), "Camera world/view inverse is invalid");
}
}

int main()
{
    using namespace Engine::Scene;
    try {
        EntityWorld world;
        const auto parent = world.CreateEntity("Camera rig");
        const auto first = world.CreateEntity("Main camera", parent);
        const auto second = world.CreateEntity("Backup camera");
        EntityId selected = 99;
        Matrix4x4 matrix{};
        Require(!world.GetPrimaryCamera(selected, matrix) && selected == 0, "Empty scene selected a camera");
        Require(world.SetCamera(first, {true, 0.7f, 0.2f, 200.0f}), "Valid projection rejected");
        Require(world.SetCamera(second, {}), "Backup camera creation failed");
        auto* rig = world.Find(parent);
        rig->transform.position = {10, 0, 0};
        rig->transform.scale = {2, 3, 4};
        rig->transform.rotation.z = std::numbers::pi_v<float> / 2.0f;
        world.Find(first)->transform.position = {1, 0, 0};
        Require(world.GetPrimaryCamera(selected, matrix) && selected == first, "Primary selection is incorrect");
        Require(Near(matrix.m[3][0], 10) && Near(matrix.m[3][1], 2), "Camera lost composed parent position");
        Require(Near(matrix.m[0][0], 0) && Near(matrix.m[0][1], 1), "Camera lost parent rotation");
        CheckRigid(matrix);
        world.Find(first)->transform.rotation = {0.3f, 0.6f, 0.9f};
        Require(world.GetPrimaryCamera(selected, matrix), "Sheared hierarchy camera could not be resolved");
        CheckRigid(matrix);
        std::reverse(world.GetEntities().begin(), world.GetEntities().end());
        Require(world.GetPrimaryCamera(selected, matrix) && selected == first, "Selection depends on vector order");
        world.Find(parent)->active = false;
        Require(world.GetPrimaryCamera(selected, matrix) && selected == second, "Inactive ancestor camera selected");
        world.Find(parent)->active = true;
        world.Find(first)->camera.primary = false;
        Require(world.GetPrimaryCamera(selected, matrix) && selected == second, "Non-primary camera selected");
        world.Find(first)->camera.primary = true;
        world.Find(parent)->transform.scale.x = 0;
        Require(world.GetPrimaryCamera(selected, matrix) && selected == second, "Singular camera frame selected");
        world.Find(parent)->transform.scale.x = 2;
        world.Find(first)->transform.position.x = std::numeric_limits<float>::infinity();
        Require(world.GetPrimaryCamera(selected, matrix) && selected == second, "Non-finite camera frame selected");
        world.Find(first)->transform.position.x = 1;
        world.Find(first)->camera.nearClip = -1;
        Require(world.GetPrimaryCamera(selected, matrix) && selected == second, "Invalid direct-edited projection selected");
        world.Find(first)->camera.nearClip = 0.2f;
        const auto before = world.Capture().dump();
        for (const auto invalid : {CameraComponent{true, 0, 0.1f, 100},
            CameraComponent{true, std::numbers::pi_v<float>, 0.1f, 100},
            CameraComponent{true, 0.5f, 0, 100}, CameraComponent{true, 0.5f, 2, 1},
            CameraComponent{true, 0.5f, 1, 1},
            CameraComponent{true, std::numeric_limits<float>::quiet_NaN(), 0.1f, 100},
            CameraComponent{true, 0.5f, std::numeric_limits<float>::denorm_min(), 0.1f},
            CameraComponent{true, 0.5f, 1.0e25f, 2.0e25f}})
            Require(!world.SetCamera(first, invalid), "Invalid camera settings accepted");
        Require(world.Capture().dump() == before, "Invalid SetCamera mutated the scene");
        const auto snapshot = world.Capture();
        EntityWorld restored;
        std::string error;
        Require(restored.Restore(snapshot, error), error.c_str());
        Require(restored.Find(first)->hasCamera && Near(restored.Find(first)->camera.fovY, 0.7f), "Camera did not round-trip");
        Require(restored.GetPrimaryCamera(selected, matrix) && selected == first, "Restored camera selection changed");
        EntityWorld orthoWorld;
        auto orthoSnapshot = snapshot;
        orthoSnapshot["entities"][1]["camera"]["projection"] = "orthographic";
        orthoSnapshot["entities"][1]["camera"]["orthographicHeight"] = 12.0;
        Require(orthoWorld.Restore(orthoSnapshot, error) && orthoWorld.Find(first)->camera.orthographic &&
            Near(orthoWorld.Find(first)->camera.orthographicHeight, 12), "Orthographic camera did not restore");
        Require(orthoWorld.Capture().dump() == orthoSnapshot.dump(), "Orthographic fields did not round-trip");
        auto olderCamera = orthoSnapshot;
        for (auto& entity : olderCamera["entities"].get<crude_json::array>()) {
            entity["camera"].get<crude_json::object>().erase("projection");
            entity["camera"].get<crude_json::object>().erase("orthographicHeight");
        }
        Require(orthoWorld.Restore(olderCamera, error) && !orthoWorld.Find(first)->camera.orthographic &&
            Near(orthoWorld.Find(first)->camera.orthographicHeight, 10), "Old camera fields did not default to perspective");
        const auto unchangedOrtho = orthoWorld.Capture().dump();
        for (auto height : {0.0, -1.0, 1.0e-45, 1.0e100}) {
            auto invalid = orthoSnapshot; invalid["entities"][1]["camera"]["orthographicHeight"] = height;
            Require(!orthoWorld.Restore(invalid, error) && orthoWorld.Capture().dump() == unchangedOrtho, "Invalid orthographic height changed scene");
        }
        for (const auto* mode : {"unknown", "Orthographic", ""}) {
            auto invalid = orthoSnapshot; invalid["entities"][1]["camera"]["projection"] = mode;
            Require(!orthoWorld.Restore(invalid, error) && orthoWorld.Capture().dump() == unchangedOrtho, "Unknown projection mode changed scene");
        }
        const auto restoredBefore = restored.Capture().dump();
        for (const auto* field : {"enabled", "primary", "fovY", "nearClip", "farClip", "orthographicHeight"}) {
            auto bad = snapshot;
            bad["entities"][1]["camera"][field] = "invalid";
            Require(!restored.Restore(bad, error), "Malformed serialized camera accepted");
            Require(restored.Capture().dump() == restoredBefore, "Rejected camera changed the world");
        }
        auto bad = snapshot;
        bad["entities"][1]["camera"]["fovY"] = 1.0e100;
        Require(!restored.Restore(bad, error), "Float-overflowing camera projection accepted");
        bad = snapshot;
        bad["entities"][1]["camera"]["nearClip"] = 300.0;
        Require(!restored.Restore(bad, error), "Serialized near/far order accepted");
        Require(restored.Capture().dump() == restoredBefore, "Rejected camera projection changed the world");
        auto legacy = snapshot;
        for (auto& entity : legacy["entities"].get<crude_json::array>())
            entity.get<crude_json::object>().erase("camera");
        Require(restored.Restore(legacy, error), "Older scenes without cameras are incompatible");
        Require(!restored.GetPrimaryCamera(selected, matrix), "Legacy scene gained an authored camera");
        world.RemoveCamera(first);
        world.Find(second)->active = false;
        Require(!world.GetPrimaryCamera(selected, matrix), "Removed/disabled camera selected");
        std::cout << "PASS: camera projection validation, persistence, hierarchy, rigid frame and deterministic selection\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
