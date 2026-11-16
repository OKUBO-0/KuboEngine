#include "ParticleComponentInspector.h"
#include "RenderComponentInspector.h"
#include "UIComponentInspector.h"
#include "../base/AssetPath.h"
namespace Engine::Editor {
void DrawParticleInspector(Scene::EntityWorld& world,Scene::EntityId id,const Assets::AssetRegistry& assets,
    const std::filesystem::path& root,std::string& status){
    const auto* entity=world.Find(id);if(!entity||!ImGui::CollapsingHeader("Particle",ImGuiTreeNodeFlags_DefaultOpen))return;
    if(!entity->hasParticle){if(RenderInspectorButton("Add Particle")&&!world.SetParticle(id,{}))status="Particle addition rejected";return;}
    auto value=entity->particle;bool changed=RenderInspectorCheckbox("Particle Enabled",&value.enabled);
    const auto opened=ImGui::BeginCombo("Registered Particle Image",value.assetPath.empty()?"Unassigned":value.assetPath.c_str());ObserveUIInspector("Registered Particle Image");
    if(opened){for(const auto& asset:assets.GetAssets()){
        if(asset.type!=Assets::AssetType::Texture||!Scene::ModelComponent::IsValidGuid(asset.guid))continue;
        const auto path=assets.ResolveGuid(asset.guid);std::error_code error;if(!std::filesystem::is_regular_file(Assets::AssetRegistry::MetadataPath(path),error)||error)continue;
        const auto hint=Base::PathToUtf8(path.lexically_relative(root));if(hint.empty()||!Scene::UIComponentDetail::AssetPath(hint))continue;
        const bool selected=ImGui::Selectable(asset.relativePath.c_str(),value.assetGuid==asset.guid);ObserveUIInspector("Particle Texture Choice");if(selected){value.assetPath=hint;value.assetGuid=asset.guid;changed=true;}
    }ImGui::EndCombo();}
    std::array<char,4097> path{};std::memcpy(path.data(),value.assetPath.data(),(std::min)(value.assetPath.size(),path.size()-1));
    if(ImGui::InputText("Particle Image Path",path.data(),path.size())){value.assetPath=path.data();value.assetGuid.clear();changed=true;}
    ImGui::TextWrapped("Particle GUID: %s",value.assetGuid.empty()?"none":value.assetGuid.c_str());
    if(RenderInspectorButton("Clear Particle Image")){value.assetPath.clear();value.assetGuid.clear();changed=true;}
    int geometry=static_cast<int>(value.geometry);if(ImGui::Combo("Particle Geometry",&geometry,"Quad\0Ring\0Cylinder\0")){value.geometry=static_cast<Scene::ParticleGeometry>(geometry);changed=true;}
    int capacity=static_cast<int>(value.capacity),burst=static_cast<int>(value.burst);
    if(ImGui::DragInt("Particle Capacity",&capacity,1,1,4096)){value.capacity=static_cast<uint32_t>(capacity);changed=true;}
    if(ImGui::DragInt("Particle Burst",&burst,1,0,4096)){value.burst=static_cast<uint32_t>(burst);changed=true;}
    changed|=ImGui::InputScalar("Particle Seed",ImGuiDataType_U32,&value.seed);
    changed|=ImGui::DragFloat("Particle Rate",&value.rate,1,0,4096);changed|=ImGui::DragFloat("Particle Lifetime",&value.lifetime,.01f,1.f/240,60);changed|=ImGui::DragFloat("Particle Size",&value.size,.01f,.0001f,1000);
    changed|=ImGui::DragFloat3("Particle Velocity",&value.velocity.x,.1f,-1000,1000);changed|=ImGui::DragFloat3("Particle Velocity Spread",&value.spread.x,.1f,0,1000);
    changed|=ImGui::ColorEdit4("Particle Start Color",&value.startColor.x);changed|=ImGui::ColorEdit4("Particle End Color",&value.endColor.x);
    ImGui::TextWrapped("Local-space particles follow all parent transforms. Play emits, Pause freezes, Stop resets. Burst runs once on start; set Rate to zero for a one-shot effect. Empty image does not emit. Re-enabling or changing settings restarts the effect from its seed.");
    if(changed&&!world.SetParticle(id,value))status="Particle rejected: valid image/GUID, finite ranges and Burst <= Capacity required";
    if(RenderInspectorButton("Remove Particle"))world.RemoveParticle(id);
}
}
