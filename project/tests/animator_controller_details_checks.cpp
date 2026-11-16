#include "engine/editor/AnimatorControllerDetails.h"
#include <imgui_internal.h>
#include <map>
#include <iostream>

static std::map<ImGuiID,ImRect> boxes;
static std::map<std::string,ImRect> items;
void ImGuiTestEngineHook_ItemAdd(ImGuiContext* context,ImGuiID id,const ImRect& bb,const ImGuiLastItemData*){
    boxes.insert_or_assign(id,bb);
    // This ImGui revision does not emit ItemInfo for BeginCombo. Resolve its
    // actual ItemAdd ID in the current scope, not by guessing screen positions.
    if(context->CurrentWindow && context->CurrentWindow->IDStack.Size>0 && id==context->CurrentWindow->GetID("Delete replacement"))items.insert_or_assign("Delete replacement",bb);
}
void ImGuiTestEngineHook_ItemInfo(ImGuiContext*,ImGuiID id,const char* label,ImGuiItemStatusFlags){if(auto found=boxes.find(id);found!=boxes.end())items.insert_or_assign(label,found->second);}
void ImGuiTestEngineHook_Log(ImGuiContext*,const char*,...){}
const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*,ImGuiID){return nullptr;}
static void Need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
int main(){ImGui::CreateContext();try{
    using namespace Engine::Editor;using namespace Engine::AnimationSystem::Controller;
    AnimatorControllerDocument document;std::string error;ControllerDetails::ParameterView parameters;ControllerDetails::TransitionView transition;
    Need(AnimatorControllerEdits(document).New(error),"New failed");
    auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;io.DisplaySize={1400,1200};io.DeltaTime=1.f/60;
    unsigned char* pixels;int width,height;io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    ImGui::GetCurrentContext()->TestEngineHookItems=true;
    bool enabled=true;
    const auto frame=[&]{boxes.clear();items.clear();ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({1300,1100});
        ImGui::Begin("Details fixture",nullptr,ImGuiWindowFlags_NoSavedSettings);
        const auto graph=document.Definition();const auto ids=document.AuthoringIds();const auto revision=document.Revision();AnimatorControllerEdits edit(document);
        ControllerDetails::Parameters(graph,ids,edit,enabled,revision,parameters,error);
        ControllerDetails::Transitions(graph,ids,0,edit,enabled && document.Revision()==revision,revision,transition,error);
        ImGui::End();ImGui::Render();};
    const auto click=[&](const char* label){frame();frame();auto found=items.find(label);if(found==items.end())throw std::runtime_error(std::string("Expected widget not emitted: ")+label);
        const auto center=found->second.GetCenter();io.AddMousePosEvent(center.x,center.y);frame();
        io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();
    };
    frame();frame();const auto original=document;frame();Need(document.HasSameState(original),"Passive draw mutated graph");
    click("Add parameter");Need(document.Definition().parameters.size()==1,"Add parameter input failed");
    click("Default (Enter)");io.AddKeyEvent(ImGuiMod_Ctrl,true);io.AddKeyEvent(ImGuiKey_A,true);frame();
    io.AddKeyEvent(ImGuiKey_A,false);io.AddKeyEvent(ImGuiMod_Ctrl,false);frame();
    io.AddInputCharactersUTF8("3.5");frame();io.AddKeyEvent(ImGuiKey_Enter,true);frame();io.AddKeyEvent(ImGuiKey_Enter,false);frame();
    Need(document.Definition().parameters[0].number==3.5,"Float input Enter failed");
    click("Add parameter");Need(document.Definition().parameters.size()==2,"Second parameter input failed");
    auto flag=document.Definition().parameters[0];flag.name="Flag###literal";flag.kind=Kind::Bool;
    Need(AnimatorControllerEdits(document).ReplaceParameter(0,flag,error),"Bool setup failed");
    click("Default");Need(document.Definition().parameters[0].flag,"Bool checkbox input failed");
    click("Add Any State transition");Need(document.Definition().layers[0].transitions.size()==1,"Add edge input failed");
    click("Add condition");Need(document.Definition().layers[0].transitions[0].conditions.size()==1,"Add condition input failed");
    click("Value");Need(document.Definition().layers[0].transitions[0].conditions[0].flag,"Condition checkbox input failed");
    click("Use normalized exit time");Need(document.Definition().layers[0].transitions[0].exitTime.has_value(),"Exit checkbox input failed");
    click("Explicit self reentry");Need(document.Definition().layers[0].transitions[0].reenter,"Reentry input failed");
    const auto configured=document;Need(document.Undo(error) && !document.Definition().layers[0].transitions[0].reenter,"UI Undo failed");
    Need(document.Redo(error) && document.Text()==configured.Text(),"UI Redo failed");
    enabled=false;const auto disabled=document;click("Add parameter");click("Remove condition");Need(document.HasSameState(disabled),"Disabled input changed graph");enabled=true;
    const auto referenced=document;flag.kind=Kind::Float;
    Need(!AnimatorControllerEdits(document).ReplaceParameter(0,flag,error) && document.HasSameState(referenced),"Referenced bool eq changed type silently");
    click("Delete parameter");Need(document.HasSameState(referenced),"Referenced deletion without replacement changed graph");
    click("Remove condition");Need(document.Definition().layers[0].transitions[0].conditions.empty(),"Remove condition input failed");
    click("Delete parameter");Need(document.Definition().parameters.size()==1,"Unused parameter delete input failed");
    click("Add Any State transition");transition.For(document.AuthoringIds().layers[0]).Pick(document.AuthoringIds().layers[0].transitions,1);
    auto edge=document.Definition().layers[0].transitions[1];edge.blendSeconds=.75f;
    Need(AnimatorControllerEdits(document).ReplaceTransition(0,1,edge,error),"Priority setup failed");
    const auto movedId=document.AuthoringIds().layers[0].transitions[1];
    click("Higher priority");Need(document.Definition().layers[0].transitions[0].blendSeconds==.75f && transition.For(document.AuthoringIds().layers[0]).Identity()==movedId,"Priority input/selection failed");
    click("Delete transition");Need(document.Definition().layers[0].transitions.size()==1 && document.Definition().layers[0].transitions[0].blendSeconds!=.75f,"Delete removed the old priority index instead of the selected edge");
    auto retarget=document.Definition();auto replacement=retarget.parameters[0];replacement.name="Replacement###literal";
    retarget.parameters.push_back(replacement);retarget.layers[0].transitions[0].conditions={ControllerDetails::DefaultCondition(retarget,0)};
    retarget.layers[0].transitions[0].conditions[0].number=9;
    Need(document.Apply(retarget,error),"Retarget setup failed");
    click("Delete replacement");click("##replacement");click("Delete parameter");
    Need(document.Definition().parameters.size()==1 && document.Definition().parameters[0].name==replacement.name &&
        document.Definition().layers[0].transitions[0].conditions[0].parameter==0 && document.Definition().layers[0].transitions[0].conditions[0].number==9,"Explicit GUI replacement/compaction failed");
    // A float eq condition would be format-valid after coercion to bool; the
    // structural command must nevertheless refuse to reinterpret its value.
    auto graph=document.Definition();graph.parameters[0].kind=Kind::Float;graph.parameters[0].number=3;
    graph.layers[0].transitions[0].conditions={ControllerDetails::DefaultCondition(graph,0)};
    graph.layers[0].transitions[0].conditions[0].number=2;
    Need(document.Apply(graph,error),"Float eq fixture failed");const auto numeric=document;
    auto converted=graph.parameters[0];converted.kind=Kind::Bool;
    Need(!AnimatorControllerEdits(document).ReplaceParameter(0,converted,error) && document.HasSameState(numeric),"Referenced float eq was coerced");
    graph.layers[0].transitions[0].conditions.clear();Need(document.Apply(graph,error),"Reference removal failed");
    Need(AnimatorControllerEdits(document).ReplaceParameter(0,converted,error),"Unused type change refused");
    for(size_t i=document.Definition().parameters.size();i<32;++i)Need(AnimatorControllerEdits(document).AddParameter({"Bound "+std::to_string(i),Kind::Trigger,0,false},error),"Parameter bound setup failed");
    const auto maximum=document;click("Add parameter");Need(document.HasSameState(maximum),"Parameter cap UI changed document");
    auto compact=document.Definition();compact.parameters={{"Unused",Kind::Float,0,false},{"Middle",Kind::Float,1,false},{"Last",Kind::Float,2,false}};
    for(auto& layer:compact.layers)for(auto& edge:layer.transitions)edge.conditions.clear();Need(document.Apply(compact,error),"Compaction fixture failed");
    parameters.selection.Pick(document.AuthoringIds().parameters,1);const auto middleId=parameters.selection.Identity();frame();
    Need(AnimatorControllerEdits(document).RemoveParameter(0,std::nullopt,error),"Compaction delete failed");frame();
    Need(parameters.selection.Identity()==middleId && parameters.selection.Resolve(document.AuthoringIds().parameters)==0,"Actual detail view selected compacted successor");
    click("Default (Enter)");io.AddKeyEvent(ImGuiMod_Ctrl,true);io.AddKeyEvent(ImGuiKey_A,true);frame();io.AddKeyEvent(ImGuiKey_A,false);io.AddKeyEvent(ImGuiMod_Ctrl,false);frame();io.AddInputCharactersUTF8("7");frame();io.AddKeyEvent(ImGuiKey_Enter,true);frame();io.AddKeyEvent(ImGuiKey_Enter,false);frame();
    Need(document.Definition().parameters[0].number==7 && document.Definition().parameters[1].number==2,"Post-compaction widget edited a different parameter");
    click("Name (Enter)");io.AddKeyEvent(ImGuiMod_Ctrl,true);io.AddKeyEvent(ImGuiKey_A,true);frame();io.AddKeyEvent(ImGuiKey_A,false);io.AddKeyEvent(ImGuiMod_Ctrl,false);frame();io.AddInputCharactersUTF8("Uncommitted old element");frame();
    Need(AnimatorControllerEdits(document).RemoveParameter(0,std::nullopt,error),"Focused element deletion failed");frame();frame();io.AddKeyEvent(ImGuiKey_Enter,true);frame();io.AddKeyEvent(ImGuiKey_Enter,false);frame();
    Need(document.Definition().parameters.size()==1 && document.Definition().parameters[0].name=="Last" && document.Definition().parameters[0].number==2,"Retired input buffer committed into another element");
    click("Name (Enter)");io.AddKeyEvent(ImGuiMod_Ctrl,true);io.AddKeyEvent(ImGuiKey_A,true);frame();io.AddKeyEvent(ImGuiKey_A,false);io.AddKeyEvent(ImGuiMod_Ctrl,false);frame();io.AddInputCharactersUTF8("Old revision draft");frame();
    auto external=document.Definition().parameters[0];external.name="New authoritative revision";Need(AnimatorControllerEdits(document).ReplaceParameter(0,external,error),"External revision setup failed");frame();frame();io.AddKeyEvent(ImGuiKey_Enter,true);frame();io.AddKeyEvent(ImGuiKey_Enter,false);frame();
    Need(document.Definition().parameters[0].name==external.name,"Old revision text overwrote same-ID external edit");
    ImGui::DestroyContext();std::cout<<"PASS: actual ImGui injected events for parameter/condition/edge/default/exit/reentry/priority/deletion; stable selected edge deletion and parameter compaction target; retired-element and old-revision input buffer isolation; disabled/reference/cap guards; Undo/Redo and float-eq coercion refusal (not physical input QA)\n";return 0;
}catch(const std::exception& exception){ImGui::DestroyContext();std::cerr<<exception.what()<<'\n';return 1;}}
