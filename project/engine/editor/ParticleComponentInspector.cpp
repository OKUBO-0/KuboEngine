#include "ParticleComponentInspector.h"
#include "RenderComponentInspector.h"
#include "UIComponentInspector.h"
#include "../base/AssetPath.h"
namespace Engine::Editor {
namespace {
template<class Keys,class Edit,class Insert>
bool DrawLifetimeKeys(const char* addLabel,Keys& keys,Edit edit,Insert insert) {
    bool changed=false;
    for(size_t i=0;i<keys.size();++i){
        ImGui::PushID(static_cast<int>(i));
        const float previous=i?keys[i-1].age:0;
        const float next=i+1<keys.size()?keys[i+1].age:1;
        changed|=ImGui::SliderFloat("Age",&keys[i].age,std::nextafter(previous,1.f),
            std::nextafter(next,0.f),"%.3f",ImGuiSliderFlags_AlwaysClamp);
        changed|=edit(keys[i]);
        const bool remove=ImGui::SmallButton("Remove Key");
        ImGui::PopID();
        if(remove){keys.erase(keys.begin()+i);changed=true;break;}
    }
    ImGui::BeginDisabled(keys.size()>=Scene::ParticleComponent::MaximumCurveKeys);
    if(RenderInspectorButton(addLabel)){
        size_t index=0;
        float widest=0,start=0;
        for(size_t i=0;i<=keys.size();++i){
            const float left=i?keys[i-1].age:0,right=i<keys.size()?keys[i].age:1;
            if(right-left>widest){widest=right-left;start=left;index=i;}
        }
        insert(keys.begin()+index,start+widest*0.5f);
        changed=true;
    }
    ImGui::EndDisabled();
    return changed;
}
bool DrawLifetimeCurves(Scene::ParticleComponent& settings) {
    const bool expanded=ImGui::TreeNode("Lifetime Curves");
    ObserveUIInspector("Lifetime Curves");
    if(!expanded)return false;
    bool changed=false;
    const bool sizeExpanded=ImGui::TreeNode("Size Curve");
    ObserveUIInspector("Size Curve");
    if(sizeExpanded){
        float end=settings.endSize.value_or(settings.size);
        if(ImGui::DragFloat("End Size",&end,.01f,0,1000,"%.3f",ImGuiSliderFlags_AlwaysClamp)){
            settings.endSize=end;changed=true;
        }
        if(settings.endSize && ImGui::SmallButton("Match Start Size")){settings.endSize.reset();changed=true;}
        std::array<float,64> samples{};
        for(size_t i=0;i<samples.size();++i)samples[i]=settings.SizeAt(float(i)/(samples.size()-1));
        const auto maximum=(std::max)(.01f,*std::max_element(samples.begin(),samples.end()));
        ImGui::PlotLines("##sizeCurve",samples.data(),static_cast<int>(samples.size()),0,nullptr,0,maximum,ImVec2(-1,52));
        changed|=DrawLifetimeKeys("Add Size Key",settings.sizeKeys,[](auto& key){
            return ImGui::DragFloat("Size",&key.size,.01f,0,1000,"%.3f",ImGuiSliderFlags_AlwaysClamp);
        },[&](auto next,float age){settings.sizeKeys.insert(next,{age,settings.SizeAt(age)});});
        ImGui::TreePop();
    }
    const bool colorExpanded=ImGui::TreeNode("Color Curve");
    ObserveUIInspector("Color Curve");
    if(colorExpanded){
        const auto min=ImGui::GetCursorScreenPos();
        const float width=(std::max)(1.f,ImGui::GetContentRegionAvail().x);
        auto* draw=ImGui::GetWindowDrawList();
        draw->AddRectFilled(min,ImVec2(min.x+width,min.y+20),IM_COL32(40,40,40,255));
        const auto colorAt=[&](float age){
            const auto color=settings.ColorAt(age);
            return ImGui::ColorConvertFloat4ToU32({color.x,color.y,color.z,color.w});
        };
        float previous=0;
        for(size_t i=0;i<=settings.colorKeys.size();++i){
            const float next=i<settings.colorKeys.size()?settings.colorKeys[i].age:1;
            const auto first=colorAt(previous),last=colorAt(next);
            draw->AddRectFilledMultiColor({min.x+width*previous,min.y},{min.x+width*next,min.y+20},first,last,last,first);
            previous=next;
        }
        ImGui::Dummy({width,20});
        changed|=DrawLifetimeKeys("Add Color Key",settings.colorKeys,[](auto& key){return ImGui::ColorEdit4("Color",&key.color.x);},
            [&](auto next,float age){settings.colorKeys.insert(next,{age,settings.ColorAt(age)});});
        ImGui::TreePop();
    }
    ImGui::TreePop();
    return changed;
}
}
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
    changed|=DrawLifetimeCurves(value);
    ImGui::TextWrapped("Local-space particles follow all parent transforms. Play emits, Pause freezes, Stop resets. Burst runs once on start; set Rate to zero for a one-shot effect. Empty image does not emit. Re-enabling or changing settings restarts the effect from its seed.");
    if(changed&&!world.SetParticle(id,value))status="Particle rejected: valid image/GUID, finite ranges and Burst <= Capacity required";
    if(RenderInspectorButton("Remove Particle"))world.RemoveParticle(id);
}
}
