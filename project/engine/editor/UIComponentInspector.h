#pragma once
#include "RenderComponentInspector.h"
#include "../assets/AssetRegistry.h"
#include "../base/AssetPath.h"
#include <array>
#include <algorithm>
#include <cstring>

namespace Engine::Editor {
inline void ObserveUIInspector(const char* label) {
#if defined(KUBO_APPLICATION_CHECKS)
    ObserveRenderInspectorChecks(label);
#else
    (void)label;
#endif
}
// The Game caller owns document/history and all authoring lock scopes.
inline void DrawCanvasInspector(Scene::EntityWorld& world,Scene::EntityId id,std::string& status) {
    const auto* entity=world.Find(id);if(!entity)return;
    if(!ImGui::CollapsingHeader("Canvas",ImGuiTreeNodeFlags_DefaultOpen))return;
    if(!entity->hasCanvas){if(RenderInspectorButton("Add Canvas") && !world.SetCanvas(id,{}))status="Canvas addition rejected";return;}
    auto value=entity->canvas;bool changed=RenderInspectorCheckbox("Canvas Enabled",&value.enabled);
    changed|=ImGui::DragFloat2("Canvas Reference Size",&value.referenceSize.x,1,1,16384);
    int mode=static_cast<int>(value.scaleMode);
    if(ImGui::Combo("Canvas Scaling",&mode,"Constant Pixels\0Scale With Output (stretch)\0")){value.scaleMode=static_cast<Scene::CanvasScaleMode>(mode);changed=true;}
    changed|=ImGui::DragInt("Canvas Order",&value.sortOrder,1,-32768,32767);
    ImGui::TextWrapped("Screen overlay: +X right, +Y down. Nearest Canvas selects scaling; all parent Entity transforms apply. Disabling a Canvas hides its entire branch. Canvas order then Entity ID selects the draw group.");
    if(changed && !world.SetCanvas(id,value))status="Canvas rejected: finite reference dimensions 1..16384 and integer order -32768..32767 required";
    if(RenderInspectorButton("Remove Canvas"))world.RemoveCanvas(id);
}
inline void DrawSpriteInspector(Scene::EntityWorld& world,Scene::EntityId id,const Assets::AssetRegistry& registry,
    const std::filesystem::path& projectRoot,std::string& status) {
    const auto* entity=world.Find(id);if(!entity)return;
    if(!ImGui::CollapsingHeader("Sprite",ImGuiTreeNodeFlags_DefaultOpen))return;
    if(!entity->hasSprite){if(RenderInspectorButton("Add Sprite") && !world.SetSprite(id,{}))status="Sprite addition rejected";return;}
    auto value=entity->sprite;bool changed=RenderInspectorCheckbox("Sprite Enabled",&value.enabled);
    const bool opened=ImGui::BeginCombo("Registered Sprite Image",value.assetPath.empty()?"Unassigned":value.assetPath.c_str());
    ObserveUIInspector("Registered Sprite Image");
    if(opened){
        for(const auto& asset:registry.GetAssets()){
            if(asset.type!=Assets::AssetType::Texture || !Scene::ModelComponent::IsValidGuid(asset.guid))continue;
            const auto source=registry.ResolveGuid(asset.guid);std::error_code error;
            if(!std::filesystem::is_regular_file(Assets::AssetRegistry::MetadataPath(source),error) || error)continue;
            const auto hint=Base::PathToUtf8(source.lexically_relative(projectRoot));
            if(hint.empty() || !Scene::UIComponentDetail::AssetPath(hint))continue;
            const bool selected=ImGui::Selectable(asset.relativePath.c_str(),value.assetGuid==asset.guid);
            ObserveUIInspector("Sprite Texture Choice");
            if(selected){value.assetPath=hint;value.assetGuid=asset.guid;changed=true;}
        }
        ImGui::EndCombo();
    }
    ImGui::TextWrapped("Sprite GUID: %s",value.assetGuid.empty()?"none (path-only / unassigned)":value.assetGuid.c_str());
    std::array<char,4097> path{};
    const auto bytes=(std::min)(value.assetPath.size(),path.size()-1);std::memcpy(path.data(),value.assetPath.data(),bytes);
    if(ImGui::InputText("Sprite Image Path",path.data(),path.size())){value.assetPath=path.data();value.assetGuid.clear();changed=true;}
    if(RenderInspectorButton("Clear Sprite Image")){value.assetPath.clear();value.assetGuid.clear();changed=true;}
    ImGui::TextWrapped("Editing the path clears GUID ownership. Use project-relative UTF-8 with forward slashes. Empty image is a draft and does not draw; a bad GUID never falls back to its hint.");
    changed|=ImGui::DragFloat2("Sprite Size",&value.size.x,1,0,16384);
    changed|=ImGui::DragFloat2("Sprite Anchor",&value.anchor.x,0.01f,0,1,"%.2f",ImGuiSliderFlags_AlwaysClamp);
    if(ImGui::IsItemHovered())ImGui::SetTooltip("Output position from (0, 0) top left to (1, 1) bottom right.");
    changed|=ImGui::DragFloat2("Sprite Pivot",&value.pivot.x,0.01f,-16,16);
    changed|=ImGui::ColorEdit4("Sprite Tint",&value.color.x);
    changed|=RenderInspectorCheckbox("Sprite Flip X",&value.flipX);
    changed|=RenderInspectorCheckbox("Sprite Flip Y",&value.flipY);
    changed|=ImGui::DragInt("Sprite Order",&value.sortOrder,1,-32768,32767);
    ImGui::TextWrapped("Needs a Canvas on this Entity or an ancestor. Entity Transform sets the offset from the anchor, rotation and scale. Later Sprite order/ID draws on top within its Canvas group.");
    if(changed && !world.SetSprite(id,value))status="Sprite settings rejected: valid project-relative image/GUID and finite size/pivot/color/order required";
    if(RenderInspectorButton("Remove Sprite"))world.RemoveSprite(id);
}
}
