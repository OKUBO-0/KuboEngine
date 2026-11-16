#include "AnimatorControllerDetails.h"
#include <imgui.h>
#include <cstring>

namespace Engine::Editor {
namespace ControllerDetails {
using namespace AnimationSystem::Controller;
// Authored ### text is display data; complete 64-bit identities scope widgets.
template<class Entries> bool Choose(const char* label,const Entries& entries,const std::vector<uint64_t>& ids,size_t& selected){
    bool changed=false;
    if(ImGui::BeginCombo(label,entries.at(selected).name.c_str())){
        for(size_t i=0;i<entries.size();++i){IdentityScope identity(ids.at(i));
            if(ImGui::Selectable("##choice",selected==i)){selected=i;changed=true;}
            const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();const ImVec4 clip(a.x,a.y,b.x,b.y);
            const auto& text=entries[i].name;ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(),ImGui::GetFontSize(),a,ImGui::GetColorU32(ImGuiCol_Text),text.data(),text.data()+text.size(),0,&clip);
        }ImGui::EndCombo();
    }return changed;
}
Condition DefaultCondition(const Graph& graph,size_t parameter){
    Condition value;value.parameter=parameter;const auto kind=graph.parameters.at(parameter).kind;
    value.op=kind==Kind::Trigger?"set":"eq";value.flag=kind==Kind::Trigger;return value;
}
void Parameters(const Graph& graph,const AnimatorControllerAuthoringIds& ids,AnimatorControllerEdits& edit,bool enabled,uint64_t revision,ParameterView& view,std::string& status){
    if(view.revision!=revision){view.revision=revision;view.replacement.reset();}
    size_t selected=view.selection.Resolve(ids.parameters);
    if(!ImGui::CollapsingHeader("Parameters",ImGuiTreeNodeFlags_DefaultOpen))return;
    ImGui::PushID("ControllerParameters");ImGui::BeginDisabled(!enabled);
    if(!graph.parameters.empty()){
        if(Choose("Parameter",graph.parameters,ids.parameters,selected)){view.selection.Pick(ids.parameters,selected);view.replacement.reset();}
        IdentityScope identity(view.selection.Identity());IdentityScope epoch(revision);
        const auto& original=graph.parameters[selected];auto candidate=original;
        char name[129]{};std::memcpy(name,original.name.data(),original.name.size());
        bool changed=ImGui::InputText("Name (Enter)",name,sizeof(name),ImGuiInputTextFlags_EnterReturnsTrue);candidate.name=name;
        int kind=static_cast<int>(candidate.kind);
        if(ImGui::Combo("Type",&kind,"Float\0Bool\0Trigger\0")){
            candidate.kind=static_cast<Kind>(kind);candidate.number=0;candidate.flag=false;changed=true;
        }
        if(candidate.kind==Kind::Float)changed|=ImGui::InputDouble("Default (Enter)",&candidate.number,0,0,"%.6g",ImGuiInputTextFlags_EnterReturnsTrue);
        else if(candidate.kind==Kind::Bool)changed|=ImGui::Checkbox("Default",&candidate.flag);
        else ImGui::TextUnformatted("Trigger default is always false.");
        if(changed)edit.ReplaceParameter(selected,candidate,status);
        size_t references=0;for(const auto& layer:graph.layers)for(const auto& edge:layer.transitions)for(const auto& condition:edge.conditions)if(condition.parameter==selected)++references;
        ImGui::Text("Condition references: %zu",references);
        if(view.replacement && (*view.replacement>=graph.parameters.size() || *view.replacement==selected || graph.parameters[*view.replacement].kind!=original.kind))view.replacement.reset();
        if(ImGui::BeginCombo("Delete replacement",view.replacement?graph.parameters[*view.replacement].name.c_str():"None (unused only)")){
            if(ImGui::Selectable("None (unused only)",!view.replacement))view.replacement.reset();
            for(size_t i=0;i<graph.parameters.size();++i)if(i!=selected && graph.parameters[i].kind==original.kind){
                IdentityScope replacementIdentity(ids.parameters.at(i));if(ImGui::Selectable("##replacement",view.replacement==i))view.replacement=i;
                const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();const ImVec4 clip(a.x,a.y,b.x,b.y);const auto& text=graph.parameters[i].name;
                ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(),ImGui::GetFontSize(),a,ImGui::GetColorU32(ImGuiCol_Text),text.data(),text.data()+text.size(),0,&clip);
            }ImGui::EndCombo();
        }
        ImGui::BeginDisabled(references && !view.replacement);
        if(ImGui::Button("Delete parameter"))edit.RemoveParameter(selected,view.replacement,status);
        ImGui::EndDisabled();
        ImGui::TextWrapped("Referenced parameters cannot change type. Deletion retargets conditions only to an explicitly selected same-type replacement.");
    }else ImGui::TextUnformatted("No parameters.");
    ImGui::BeginDisabled(graph.parameters.size()>=32);
    if(ImGui::Button("Add parameter")){
        Parameter value;size_t number=1;do{value.name="Parameter "+std::to_string(number++);}while(std::any_of(graph.parameters.begin(),graph.parameters.end(),[&](const auto& p){return p.name==value.name;}));
        edit.AddParameter(value,status);
    }ImGui::EndDisabled();ImGui::EndDisabled();ImGui::PopID();
}
void Transitions(const Graph& graph,const AnimatorControllerAuthoringIds& ids,size_t layerIndex,AnimatorControllerEdits& edit,bool enabled,uint64_t revision,TransitionView& view,std::string& status){
    const auto& layerIds=ids.layers.at(layerIndex);auto& selection=view.For(layerIds);size_t selected=selection.Resolve(layerIds.transitions);
    if(!ImGui::CollapsingHeader("Transitions",ImGuiTreeNodeFlags_DefaultOpen))return;
    ImGui::PushID("ControllerTransitions");ImGui::BeginDisabled(!enabled);
    {
    IdentityScope layerIdentity(layerIds.id);IdentityScope epoch(revision);
    const auto& layer=graph.layers.at(layerIndex);
    if(!layer.transitions.empty()){
        if(ImGui::BeginCombo("Priority",std::to_string(selected).c_str())){
            for(size_t i=0;i<layer.transitions.size();++i){IdentityScope identity(layerIds.transitions.at(i));if(ImGui::Selectable(std::to_string(i).c_str(),selected==i)){selected=i;selection.Pick(layerIds.transitions,i);}}ImGui::EndCombo();
        }
        IdentityScope identity(selection.Identity());
        const auto& original=layer.transitions[selected];auto candidate=original;bool changed=false;
        bool any=!candidate.from;if(ImGui::Checkbox("Any State source",&any)){candidate.from=any?std::optional<size_t>{}:std::optional<size_t>{0};changed=true;}
        if(candidate.from){size_t from=*candidate.from;if(Choose("From",layer.states,layerIds.states,from)){candidate.from=from;changed=true;}}
        changed|=Choose("To",layer.states,layerIds.states,candidate.to);
        changed|=ImGui::InputFloat("Blend seconds (Enter)",&candidate.blendSeconds,0,0,"%.3f",ImGuiInputTextFlags_EnterReturnsTrue);
        bool exit=candidate.exitTime.has_value();if(ImGui::Checkbox("Use normalized exit time",&exit)){candidate.exitTime=exit?std::optional<float>{0.f}:std::optional<float>{};changed=true;}
        if(candidate.exitTime)changed|=ImGui::InputFloat("Exit time (Enter)",&*candidate.exitTime,0,0,"%.3f",ImGuiInputTextFlags_EnterReturnsTrue);
        changed|=ImGui::Checkbox("Explicit self reentry",&candidate.reenter);
        std::optional<size_t> remove;
        for(size_t i=0;i<candidate.conditions.size();++i){ImGui::PushID(static_cast<int>(i));auto& condition=candidate.conditions[i];
            size_t parameter=condition.parameter;if(Choose("Condition parameter",graph.parameters,ids.parameters,parameter)){condition=DefaultCondition(graph,parameter);changed=true;}
            const auto kind=graph.parameters[condition.parameter].kind;
            const char* const floatOps[]={"eq","ne","lt","le","gt","ge"};const char* const boolOps[]={"eq","ne"};
            if(kind==Kind::Trigger)ImGui::TextUnformatted("Trigger: set / true");
            else {
                const auto ops=kind==Kind::Float?floatOps:boolOps;const int count=kind==Kind::Float?6:2;int op=0;
                for(int j=0;j<count;++j)if(condition.op==ops[j])op=j;
                if(ImGui::Combo("Operator",&op,ops,count)){condition.op=ops[op];changed=true;}
                if(kind==Kind::Float)changed|=ImGui::InputDouble("Value (Enter)",&condition.number,0,0,"%.6g",ImGuiInputTextFlags_EnterReturnsTrue);
                else changed|=ImGui::Checkbox("Value",&condition.flag);
            }
            if(ImGui::Button("Remove condition"))remove=i;ImGui::PopID();
        }
        if(remove){candidate.conditions.erase(candidate.conditions.begin()+*remove);changed=true;}
        ImGui::BeginDisabled(graph.parameters.empty() || candidate.conditions.size()>=8);
        if(ImGui::Button("Add condition")){candidate.conditions.push_back(DefaultCondition(graph,0));changed=true;}ImGui::EndDisabled();
        if(changed)edit.ReplaceTransition(layerIndex,selected,candidate,status);
        ImGui::BeginDisabled(selected==0);if(ImGui::Button("Higher priority"))edit.MoveTransition(layerIndex,selected,selected-1,status);ImGui::EndDisabled();ImGui::SameLine();
        ImGui::BeginDisabled(selected+1==layer.transitions.size());if(ImGui::Button("Lower priority"))edit.MoveTransition(layerIndex,selected,selected+1,status);ImGui::EndDisabled();
        if(ImGui::Button("Delete transition"))edit.RemoveTransition(layerIndex,selected,status);
    }else ImGui::TextUnformatted("No transitions. Drag a node link or add an Any State transition.");
    ImGui::BeginDisabled(layer.transitions.size()>=256);
    if(ImGui::Button("Add Any State transition")){Transition value;value.to=0;value.blendSeconds=.2f;edit.AddTransition(layerIndex,value,status);}
    ImGui::EndDisabled();ImGui::TextWrapped("Lower index has higher priority. Blend seconds: 0..60; normalized exit time: 0..1; at most 8 conditions. Self transitions require explicit reentry.");
    }
    ImGui::EndDisabled();ImGui::PopID();
}
}
}
