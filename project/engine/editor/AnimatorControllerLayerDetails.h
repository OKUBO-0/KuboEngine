#pragma once
#include "AnimatorControllerEdits.h"
#include "ControllerWidgetIdentity.h"
#include <imgui.h>
#include <cstring>

namespace Engine::Editor {
struct ControllerLayerDetailsView {
    uint64_t layer=0;std::string bone;
    void Bind(uint64_t id) noexcept{if(layer!=id){bone.clear();layer=id;}}
};
// All controls use one immutable UI snapshot/captured edit capability. A real
// mutation invalidates later commands; the next frame reconstructs the view.
inline void DrawControllerLayerDetails(const AnimationSystem::Controller::Graph& graph,const AnimatorControllerAuthoringIds& ids,size_t layerIndex,
    AnimatorControllerEdits& edit,bool enabled,uint64_t revision,std::string& status,ControllerLayerDetailsView& view) {
    view.Bind(ids.layers.at(layerIndex).id);auto& boneDraft=view.bone;
    if(!ImGui::CollapsingHeader("Layer details",ImGuiTreeNodeFlags_DefaultOpen))return;
    ImGui::BeginDisabled(!enabled);
    {
    ControllerDetails::IdentityScope identity(ids.layers.at(layerIndex).id);ControllerDetails::IdentityScope epoch(revision);
    const auto& original=graph.layers.at(layerIndex);auto candidate=original;
    char name[129]{};std::memcpy(name,original.name.data(),original.name.size());
    bool changed=ImGui::InputText("Layer name (Enter)",name,sizeof(name),ImGuiInputTextFlags_EnterReturnsTrue);candidate.name=name;
    int mode=original.mode=="additive"?1:0;
    if(ImGui::Combo("Composition",&mode,"Override\0Additive\0")){candidate.mode=mode?"additive":"override";changed=true;}
    changed|=ImGui::InputFloat("Layer weight (Enter)",&candidate.weight,0,0,"%.3f",ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::TextWrapped("Weight must be 0..1. Empty mask affects the whole rig. Bone names are format-validated here; actual rig compatibility is checked when binding a Model.");
    std::optional<size_t> remove;
    for(size_t i=0;i<original.mask.size();++i){
        ImGui::PushID(static_cast<int>(i));char bone[129]{};std::memcpy(bone,original.mask[i].data(),original.mask[i].size());
        if(ImGui::InputText("Bone (Enter)",bone,sizeof(bone),ImGuiInputTextFlags_EnterReturnsTrue)){candidate.mask[i]=bone;changed=true;}
        ImGui::SameLine();if(ImGui::Button("Remove bone"))remove=i;ImGui::PopID();
    }
    char bone[129]{};std::memcpy(bone,boneDraft.data(),boneDraft.size());if(ImGui::InputText("New mask bone",bone,sizeof(bone)))boneDraft=bone;
    ImGui::BeginDisabled(original.mask.size()>=256 || boneDraft.empty());
    const bool add=ImGui::Button("Add mask bone");ImGui::EndDisabled();
    if(remove){candidate.mask.erase(candidate.mask.begin()+*remove);changed=true;}
    if(add){candidate.mask.push_back(boneDraft);changed=true;}
    if(changed)edit.ReplaceLayer(layerIndex,candidate,status);
    ImGui::BeginDisabled(graph.layers.size()>=4);
    if(ImGui::Button("Add layer")){
        AnimationSystem::Controller::Layer layer;size_t number=1;
        do{layer.name="Layer "+std::to_string(number++);}while(std::any_of(graph.layers.begin(),graph.layers.end(),[&](const auto& value){return value.name==layer.name;}));
        layer.mode="override";layer.states.push_back({"Idle","default",1,true});edit.AddLayer(layer,status);
    }
    ImGui::EndDisabled();ImGui::SameLine();ImGui::BeginDisabled(graph.layers.size()==1);
    if(ImGui::Button("Delete layer"))edit.RemoveLayer(layerIndex,status);
    ImGui::EndDisabled();
    }
    ImGui::EndDisabled();
}
}
