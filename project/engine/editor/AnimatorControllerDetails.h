#pragma once
#include "AnimatorControllerEdits.h"
#include "ControllerDetailSelection.h"
#include "ControllerWidgetIdentity.h"

namespace Engine::Editor::ControllerDetails {
struct ParameterView {StableSelection selection;std::optional<size_t> replacement;uint64_t revision=0;};
AnimationSystem::Controller::Condition DefaultCondition(const AnimationSystem::Controller::Graph& graph,size_t parameter);
void Parameters(const AnimationSystem::Controller::Graph& graph,const AnimatorControllerAuthoringIds& ids,AnimatorControllerEdits& edit,bool enabled,uint64_t revision,ParameterView& view,std::string& status);
void Transitions(const AnimationSystem::Controller::Graph& graph,const AnimatorControllerAuthoringIds& ids,size_t layerIndex,AnimatorControllerEdits& edit,bool enabled,uint64_t revision,TransitionView& view,std::string& status);
}
