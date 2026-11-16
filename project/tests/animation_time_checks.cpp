#include "../engine/3d/model/AnimationTime.h"
#include "../engine/scene/core/SceneManager.h"
#include <iostream>
#include <limits>
#include <stdexcept>

void Require(bool condition, const char* message)
{
    if (!condition) { throw std::runtime_error(message); }
}
int main()
{
    using Engine::Graphics3D::AdvanceAnimationSeconds;
    for (int fps : {30, 60, 144}) {
        float time = 0;
        for (int i = 0; i < fps; ++i) { time = AdvanceAnimationSeconds(time, 10, 1.0f / fps, 1, true); }
        Require(std::abs(time - 1.0f) < 0.0001f, "Animation must advance one second at every frame rate");
    }
    Require(AdvanceAnimationSeconds(1, 10, 0, 1, true) == 1, "Zero dt must pause");
    Require(AdvanceAnimationSeconds(1, 10, -1, 1, true) == 1, "Negative dt must not reverse");
    Require(AdvanceAnimationSeconds(1, 10, std::numeric_limits<float>::quiet_NaN(), 1, true) == 1,
        "NaN dt must not poison animation");
    Require(AdvanceAnimationSeconds(1, 10, 1, std::numeric_limits<float>::infinity(), true) == 1,
        "Infinite speed must not poison animation");
    Require(AdvanceAnimationSeconds(1, 2, 5, 1, true) == 0, "Long steps must loop");
    Require(AdvanceAnimationSeconds(1, 2, 5, 1, false) == 2, "Non-looping clips must clamp");
    Require(AdvanceAnimationSeconds(1, 0, 1, 1, true) == 0, "Zero-duration clips must be safe");
    using Engine::Graphics3D::AdvanceControllerNormalizedTime;
    for(bool loop : {false,true}) {
        Require(AdvanceControllerNormalizedTime(0,0,0,0,1,loop)==0 &&
            AdvanceControllerNormalizedTime(0,0,0,1,0,loop)==0,"Paused zero-duration clip completed");
        const auto completed=AdvanceControllerNormalizedTime(0,0,0,0.01f,1,loop);
        Require(completed==1 && AdvanceControllerNormalizedTime(completed,0,0,100,2,loop)==1,
            "Zero-duration completion manufactured cycles");
    }
    Require(AdvanceControllerNormalizedTime(2,0.5f,1,0.5f,2,true)==3,
        "Loop normalized interval lost unwrapped cycles");
    Require(AdvanceControllerNormalizedTime(0.2,0.5f,1,0.3f,1,false)==0.5,
        "Nonloop normalized interval stopped following sampled clock");
    Require(AdvanceControllerNormalizedTime(1e9,0,1,100,2,true)==1e9 &&
        AdvanceControllerNormalizedTime(0,0,-1,1,1,true)==0 &&
        AdvanceControllerNormalizedTime(0,0,0,std::numeric_limits<float>::quiet_NaN(),1,true)==0,
        "Invalid duration/delta or normalized cap changed");
    auto* scenes = Engine::Scene::SceneManager::GetInstance();
    scenes->Update(0.125f);
    Require(scenes->GetFrameDeltaTime() == 0.125f, "Scene manager must retain supplied dt");
    scenes->Update(-1);
    Require(scenes->GetFrameDeltaTime() == 0, "Scene manager must reject invalid dt");
    scenes->Update();
    Require(scenes->GetFrameDeltaTime() == 1.0f / 60, "Legacy scene update must remain compatible");
    scenes->Finalize();
    std::cout << "PASS: animation frame rates, pause, invalid dt/speed, loop/clamp, scene dt compatibility\n";
}
