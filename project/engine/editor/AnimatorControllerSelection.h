#pragma once
#include "../assets/AnimatorControllerAsset.h"
#include "../animation/AnimatorComponent.h"
namespace Engine::Editor {
// Authored selection only. Real Model/layer compatibility belongs to Object3D.
inline bool PrepareAnimatorControllerSelection(const Base::ProjectContext& project,
    const Assets::AssetRegistry& registry,const AnimationSystem::AnimatorComponent& current,
    const std::string& guid,AnimationSystem::AnimatorComponent& output,std::string& error) {
    try {
        if(!current.IsValid() || !AnimationSystem::AnimatorComponent::ValidControllerGuid(guid))
            throw std::runtime_error("Invalid Animator settings or Controller GUID");
        if(!guid.empty()) {
            Assets::HeldAnimatorControllerAsset lease;
            if(!lease.Open(project,registry,guid,error))return false;
        }
        auto candidate=current;candidate.controllerGuid=guid;
        output.Swap(candidate);error.clear();return true;
    }catch(const std::exception& exception){error=exception.what();return false;}
}
}
