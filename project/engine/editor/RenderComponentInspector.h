#pragma once
#include "../scene/entity/EntityWorld.h"
#include "imgui.h"

namespace Engine::Editor {
#if defined(KUBO_APPLICATION_CHECKS)
// Optional runner observes the actual last widget, never supplies its outcome.
inline void (*renderInspectorChecksObserver)(const char*,ImVec2,ImVec2,bool)=nullptr;
inline void ObserveRenderInspectorChecks(const char* label){
    if(renderInspectorChecksObserver)renderInspectorChecksObserver(label,ImGui::GetItemRectMin(),ImGui::GetItemRectMax(),(ImGui::GetItemFlags()&ImGuiItemFlags_Disabled)!=0);
}
#endif
inline bool RenderInspectorButton(const char* label){
    const bool pressed=ImGui::Button(label);
#if defined(KUBO_APPLICATION_CHECKS)
    ObserveRenderInspectorChecks(label);
#endif
    return pressed;
}
inline bool RenderInspectorCheckbox(const char* label,bool* value){
    const bool changed=ImGui::Checkbox(label,value);
#if defined(KUBO_APPLICATION_CHECKS)
    ObserveRenderInspectorChecks(label);
#endif
    return changed;
}
// The caller supplies the existing document/history and authoring-lock scope.
// These widgets only use validated native component setters, never file writes.
inline void DrawLightInspector(Scene::EntityWorld& world,Scene::EntityId id,std::string& status){
    auto* entity=world.Find(id);if(!entity)return;
    if(!ImGui::CollapsingHeader("Light",ImGuiTreeNodeFlags_DefaultOpen))return;
    if(!entity->hasLight){if(RenderInspectorButton("Add Light") && !world.SetLight(id,{}))status="Light addition rejected";return;}
    auto value=entity->light;int kind=static_cast<int>(value.kind);
    bool changed=ImGui::Combo("Light Type",&kind,"Directional\0Point\0Spot\0");value.kind=static_cast<Scene::LightKind>(kind);
    changed|=RenderInspectorCheckbox("Light Enabled",&value.enabled);
    changed|=ImGui::ColorEdit3("Light Color",&value.color.x);
    changed|=ImGui::DragFloat("Light Intensity",&value.intensity,0.05f,0,1000000);
    if(value.kind!=Scene::LightKind::Directional){
        changed|=ImGui::DragFloat("Light Range (world)",&value.range,0.1f,0.0001f,1000000);
        changed|=ImGui::DragFloat("Light Decay",&value.decay,0.05f,0,16);
    }
    if(value.kind==Scene::LightKind::Spot){
        changed|=ImGui::SliderAngle("Inner Cone",&value.innerAngle,0,90);
        changed|=ImGui::SliderAngle("Outer Cone",&value.outerAngle,0.01f,90);
    }
    ImGui::TextWrapped("Emits along local +Z; parent Transform/active applies. Range is in world units. Inner cone must not exceed outer cone.");
    if(changed && !world.SetLight(id,value))status="Light settings rejected: finite values/ranges/cone required";
    if(RenderInspectorButton("Remove Light"))world.RemoveLight(id);
}
inline void DrawMaterialInspector(Scene::EntityWorld& world,Scene::EntityId id,std::string& status){
    auto* entity=world.Find(id);if(!entity)return;
    if(!ImGui::CollapsingHeader("Material",ImGuiTreeNodeFlags_DefaultOpen))return;
    if(!entity->hasMaterial){if(RenderInspectorButton("Add Material") && !world.SetMaterial(id,{}))status="Material addition rejected";return;}
    auto value=entity->material;bool changed=RenderInspectorCheckbox("Material Lighting",&value.lighting);
    changed|=ImGui::ColorEdit4("Material Tint",&value.color.x);
    changed|=ImGui::DragFloat("Material Shininess",&value.shininess,1,1,1024);
    changed|=ImGui::SliderFloat("Material Roughness",&value.roughness,0,1);
    changed|=ImGui::SliderFloat("Environment Reflection",&value.reflectionStrength,0,1);
    changed|=ImGui::SliderFloat("Texture Influence",&value.textureInfluence,0,1);
    ImGui::TextWrapped("Applies to this Entity's Model. Lit material needs enabled Lights. Absent material retains legacy unlit output.");
    if(changed && !world.SetMaterial(id,value))status="Material settings rejected: finite values/ranges required";
    if(RenderInspectorButton("Remove Material"))world.RemoveMaterial(id);
}
}
