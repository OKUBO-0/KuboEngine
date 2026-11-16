#include "../engine/editor/EditorCameraState.h"
#include <iostream>
#include <limits>
#include <stdexcept>
namespace {
void Require(bool ok, const char* text) { if (!ok) throw std::runtime_error(text); }
bool Near(float a, float b) { return std::abs(a-b) < 0.0001f; }
}
int main() {
    using namespace Engine::Editor;
    try {
        EditorCameraState camera;
        EditorCameraMotion input; input.navigate = true; input.forward = 1;
        camera.Update(input, 0.1f, false);
        Require(Near(camera.position.z, -10), "Unfocused camera moved");
        camera.Update(input, 0.1f, true);
        Require(Near(camera.position.z, -9.5f), "Fly movement distance is incorrect");
        camera.Reset(); for (int i=0; i<60; ++i) camera.Update(input, 1.0f/60, true);
        Require(Near(camera.position.z, -5), "Fly movement depends on frame rate");
        input.accelerate = true; camera.Reset(); camera.Update(input, 100, true);
        Require(Near(camera.position.z, -8.5f), "Hitch clamp/acceleration failed");
        input.forward = 0; input.mouseX = 100000; input.mouseY = 100000;
        camera.Update(input, 0.1f, true);
        Require(camera.rotation.x <= 1.5f && std::abs(camera.rotation.y) <= std::numbers::pi_v<float>, "Rotation was not bounded");
        camera.Reset();
        Require(camera.Focus({10,20,30}) && Near(camera.position.x, 10) && Near(camera.position.y, 20) && Near(camera.position.z, 20), "Focus did not preserve orientation/offset");
        const auto old = camera.position;
        Require(!camera.Focus({std::numeric_limits<float>::infinity(),0,0}) && Near(camera.position.x, old.x), "Invalid focus changed camera");
        input = {}; input.wheel = 2;
        camera.Update(input, 0.1f, true);
        Require(Near(camera.position.z, 22), "Perspective wheel did not dolly");
        camera.orthographic = true;
        camera.Update(input, 0.1f, true);
        Require(camera.height < 10 && Near(camera.position.z, 22), "Orthographic wheel moved camera instead of zooming");
        for (int i=0; i<100; ++i) camera.Update(input, 0.1f, true);
        Require(camera.height >= 0.01f, "Orthographic zoom crossed its lower limit");
        const auto zoom = camera.height;
        input.wheel = std::numeric_limits<float>::quiet_NaN();
        camera.Update(input, 0.1f, true);
        Require(camera.height == zoom, "Nonfinite input changed editor state");
        camera.Reset();
        Require(!camera.orthographic && Near(camera.height,10) && Near(camera.position.y,2), "View reset failed");
        Require(camera.FrameBounds({{-5,-5,-5},{5,5,5}}) && camera.position.z < -40 && camera.farClip>=1000, "Perspective bounds framing failed");
        camera.orthographic=true;
        Require(camera.FrameBounds({{-5,-5,-5},{5,5,5}}) && camera.height>20, "Orthographic bounds framing failed");
        const auto framedPosition=camera.position;
        Require(!camera.FrameBounds({{1,1,1},{-1,-1,-1}}) && Near(camera.position.z,framedPosition.z), "Invalid framing changed camera");
        std::cout << "PASS: editor camera navigation gate, frame timing/hitch, rotation bounds, focus, dolly/zoom and invalid input isolation\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
