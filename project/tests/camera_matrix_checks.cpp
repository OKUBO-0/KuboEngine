#include "../engine/camera/Camera.h"
#include "MyMath.h"
#include <imgui.h>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <limits>
#include <numbers>
#include "../engine/editor/ScenePicking.h"

// MyMath also contains UI helpers. No UI is allowed in this CPU test;
// these two link dependencies deliberately fail if unexpectedly called.
namespace ImGui {
void Text(const char*, ...) { throw std::runtime_error("Unexpected ImGui call in Camera CPU test"); }
void SameLine(float, float) { throw std::runtime_error("Unexpected ImGui call in Camera CPU test"); }
}

namespace {
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
bool Near(float a, float b) { return std::abs(a - b) < 0.001f; }
void Same(const Matrix4x4& actual, const Matrix4x4& expected)
{
    for (int row = 0; row < 4; ++row) for (int col = 0; col < 4; ++col)
        Require(Near(actual.m[row][col], expected.m[row][col]), "Camera matrix mismatch");
}
}
int main()
{
    try {
        Engine::CameraSystem::Camera camera;
        camera.SetTranslate({1, 2, -5});
        camera.SetRotate({0.2f, 0.3f, 0.4f});
        camera.Update();
        const auto local = MyMath::MakeAffineMatrix(Vector3{1, 1, 1}, Vector3{0.2f, 0.3f, 0.4f}, Vector3{1, 2, -5});
        Same(camera.GetWorldMatrix(), local);
        const auto authored = MyMath::MakeAffineMatrix(Vector3{1, 1, 1}, Vector3{0.6f, 0.8f, 0.1f}, Vector3{20, 30, 40});
        camera.SetWorldMatrix(authored);
        camera.SetFovY(0.8f);
        camera.SetNearClip(0.2f);
        camera.SetFarClip(200);
        camera.SetAspectRatio(2);
        camera.Update();
        Same(camera.GetWorldMatrix(), authored);
        Same(camera.GetViewMatrix(), authored.Inverse());
        const auto projection = MyMath::MakePerspectiveFovMatrix(0.8f, 2, 0.2f, 200);
        Same(camera.GetProjectionMatrix(), projection);
        Same(camera.GetViewProjectionMatrix(), authored.Inverse() * projection);
        camera.SetOrthographic(true, 10);
        camera.Update();
        const auto ortho = camera.GetProjectionMatrix();
        Require(camera.IsOrthographic() && Near(ortho.m[0][0], 0.1f) && Near(ortho.m[1][1], 0.2f) &&
            Near(ortho.m[3][3], 1) && Near(ortho.m[2][3], 0), "Centered orthographic span/aspect/perspective divide is incorrect");
        Require(Near(0.2f * ortho.m[2][2] + ortho.m[3][2], 0) &&
            Near(200 * ortho.m[2][2] + ortho.m[3][2], 1), "Orthographic near/far depth is not DX12 zero-to-one");
        Same(camera.GetViewProjectionMatrix(), authored.Inverse() * ortho);
        camera.SetAspectRatio(1);
        camera.SetOrthographic(true, 20);
        camera.Update();
        Require(Near(camera.GetProjectionMatrix().m[0][0], 0.1f) && Near(camera.GetProjectionMatrix().m[1][1], 0.1f), "Orthographic height/aspect update failed");
        camera.SetAspectRatio(2);
        camera.SetOrthographic(true, (std::numeric_limits<float>::max)());
        camera.Update();
        Require(std::isfinite(camera.GetProjectionMatrix().m[0][0]) && camera.GetProjectionMatrix().m[0][0] > 0,
            "Orthographic width overflowed for a finite large height");
        camera.SetOrthographic(false);
        camera.Update();
        Same(camera.GetProjectionMatrix(), projection);
        const auto position = camera.GetWorldPosition();
        Require(Near(position.x, 20) && Near(position.y, 30) && Near(position.z, 40), "GPU camera position remains local");
        camera.ClearWorldMatrix();
        camera.Update();
        Same(camera.GetWorldMatrix(), local);
        Require(Near(camera.GetWorldPosition().x, 1), "Clearing camera override did not restore local position");
        using namespace Engine::Editor;
        camera.SetTranslate({0,0,-5}); camera.SetRotate({0,0,0}); camera.Update();
        ScenePickSegment ray;
        Require(MakeScenePickSegment(0,0,camera.GetViewProjectionMatrix(),ray) && Near(ray.nearPoint.x,0) && ray.nearPoint.z < 0, "Perspective center ray failed");
        double fraction=0;
        Require(IntersectSceneBounds(ray,{{-1,-1,-1},{1,1,1}},fraction) && fraction>0 && fraction<1, "Center ray missed visible bounds");
        Require(!IntersectSceneBounds(ray,{{-1,-1,-20},{1,1,-10}},fraction), "Ray selected bounds behind near clip");
        camera.SetOrthographic(true,10); camera.Update();
        Require(MakeScenePickSegment(0.5f,0.5f,camera.GetViewProjectionMatrix(),ray) && Near(ray.nearPoint.x,5) && Near(ray.nearPoint.y,2.5f) && Near(ray.nearPoint.x,ray.farPoint.x), "Orthographic parallel unprojection failed");
        Require(!MakeScenePickSegment(2,0,camera.GetViewProjectionMatrix(),ray) && !MakeScenePickSegment(0,0,Matrix4x4{},ray), "Invalid NDC/singular projection accepted");
        auto boundsWorld=MyMath::MakeAffineMatrix(Vector3{-2,3,4},Vector3{0,0,0},Vector3{10,20,30}); SceneBounds transformed;
        Require(TransformSceneBounds({{-1,-1,-1},{1,1,1}},boundsWorld,transformed) && Near(transformed.min.x,8) && Near(transformed.max.z,34), "Negative/nonuniform scaled bounds failed");
        boundsWorld=MyMath::MakeAffineMatrix(Vector3{-2,3,4},Vector3{0,0,std::numbers::pi_v<float>*0.5f},Vector3{10,20,30});
        Require(TransformSceneBounds({{-1,-1,-1},{1,1,1}},boundsWorld,transformed) && Near(transformed.min.x,7) && Near(transformed.max.y,22), "Rotated negative/nonuniform bounds failed");
        Require(!IntersectSceneBounds({{0,0,0},{0,0,0}},{{-1,-1,-1},{1,1,1}},fraction), "Degenerate ray accepted");
        std::cout << "PASS: production Camera authored world/view/projection matrices, world position and local fallback\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
