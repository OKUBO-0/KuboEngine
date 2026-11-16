#pragma once
#include "SceneDocument.h"
#include "Vector3.h"
#include "Vector4.h"
#include <cmath>
#include <initializer_list>
#include <string_view>
#include <type_traits>

namespace Engine::Scene {

enum class LightKind { Directional, Point, Spot };
inline constexpr float MaximumLightAngle=1.57079632679489661923f;
// Authored values only. Position/direction come from the Entity's composed
// transform; no graphics resource, live pointer or cached world pose is persisted.
struct LightComponent {
    LightKind kind=LightKind::Directional;
    bool enabled=true;
    Vector3 color{1,1,1};
    float intensity=1,range=10,decay=2;
    float innerAngle=0.35f,outerAngle=0.6f; // radians, half angles
    bool IsValid() const noexcept;
};
struct MaterialComponent {
    // Absent component and the default value retain generic legacy unlit output.
    bool lighting=false;
    Vector4 color{1,1,1,1};
    float shininess=60,roughness=0,reflectionStrength=0,textureInfluence=1;
    bool IsValid() const noexcept;
};

namespace RenderComponentDetail {
inline bool Range(float v,float lo,float hi) noexcept {return std::isfinite(v) && v>=lo && v<=hi;}
inline bool Number(const SceneValue& object,const char* key,float& value,float lo,float hi) {
    if(!object.contains(key))return true;
    const auto& input=object[key];if(!input.is_number())return false;
    const double number=input.get<crude_json::number>();
    // Check the double before narrowing: negative subnormal JSON must not become -0.
    if(!std::isfinite(number) || number<double(lo) || number>double(hi))return false;
    value=static_cast<float>(number);return Range(value,lo,hi);
}
inline bool Boolean(const SceneValue& object,const char* key,bool& value) {
    if(!object.contains(key))return true;
    if(!object[key].is_boolean())return false;
    value=object[key].get<crude_json::boolean>();return true;
}
inline bool Color(const SceneValue& object,size_t count,float* output,const char* key="color") {
    if(!object.contains(key))return true;
    const auto& value=object[key];
    if(!value.is_array() || value.get<crude_json::array>().size()!=count)return false;
    for(size_t i=0;i<count;++i){
        if(!value[i].is_number())return false;
        const auto n=value[i].get<crude_json::number>();
        if(!std::isfinite(n) || n<0 || n>1)return false;
        output[i]=static_cast<float>(n);
    }
    return true;
}
inline bool Keys(const SceneValue& value,std::initializer_list<std::string_view> allowed) {
    if(!value.is_object())return false;
    for(const auto& [key,child]:value.get<crude_json::object>()){
        (void)child;bool known=false;for(const auto field:allowed)if(key==field){known=true;break;}
        if(!known)return false;
    }
    return true;
}
}
inline bool LightComponent::IsValid() const noexcept {
    using RenderComponentDetail::Range;
    return (kind==LightKind::Directional || kind==LightKind::Point || kind==LightKind::Spot) &&
        Range(color.x,0,1) && Range(color.y,0,1) && Range(color.z,0,1) &&
        Range(intensity,0,1000000) && Range(range,0.0001f,1000000) && Range(decay,0,16) &&
        Range(innerAngle,0,MaximumLightAngle) && Range(outerAngle,0.0001f,MaximumLightAngle) && innerAngle<=outerAngle;
}
inline bool MaterialComponent::IsValid() const noexcept {
    using RenderComponentDetail::Range;
    return Range(color.x,0,1) && Range(color.y,0,1) && Range(color.z,0,1) && Range(color.w,0,1) &&
        Range(shininess,1,1024) && Range(roughness,0,1) && Range(reflectionStrength,0,1) && Range(textureInfluence,0,1);
}
inline bool DecodeLight(const SceneValue& value,LightComponent& output) noexcept {
    try {
        using namespace RenderComponentDetail;
        if(!Keys(value,{"type","enabled","color","intensity","range","decay","innerAngle","outerAngle"}))return false;
        LightComponent candidate;
        if(value.contains("type")){
            if(!value["type"].is_string())return false;
            const auto& type=value["type"].get<crude_json::string>();
            if(type=="directional")candidate.kind=LightKind::Directional;
            else if(type=="point")candidate.kind=LightKind::Point;
            else if(type=="spot")candidate.kind=LightKind::Spot;
            else return false;
        }
        float color[]{candidate.color.x,candidate.color.y,candidate.color.z};
        if(!Color(value,3,color) || !Boolean(value,"enabled",candidate.enabled) ||
            !Number(value,"intensity",candidate.intensity,0,1000000) ||
            !Number(value,"range",candidate.range,0.0001f,1000000) ||
            !Number(value,"decay",candidate.decay,0,16) ||
            !Number(value,"innerAngle",candidate.innerAngle,0,MaximumLightAngle) ||
            !Number(value,"outerAngle",candidate.outerAngle,0.0001f,MaximumLightAngle))return false;
        candidate.color={color[0],color[1],color[2]};
        if(!candidate.IsValid())return false;
        static_assert(std::is_nothrow_copy_assignable_v<LightComponent>);
        output=candidate;return true;
    }catch(...){return false;}
}
inline bool DecodeMaterial(const SceneValue& value,MaterialComponent& output) noexcept {
    try {
        using namespace RenderComponentDetail;
        if(!Keys(value,{"lighting","color","shininess","roughness","reflectionStrength","textureInfluence"}))return false;
        MaterialComponent candidate;
        float color[]{candidate.color.x,candidate.color.y,candidate.color.z,candidate.color.w};
        if(!Color(value,4,color) || !Boolean(value,"lighting",candidate.lighting) ||
            !Number(value,"shininess",candidate.shininess,1,1024) ||
            !Number(value,"roughness",candidate.roughness,0,1) ||
            !Number(value,"reflectionStrength",candidate.reflectionStrength,0,1) ||
            !Number(value,"textureInfluence",candidate.textureInfluence,0,1))return false;
        candidate.color={color[0],color[1],color[2],color[3]};
        if(!candidate.IsValid())return false;
        static_assert(std::is_nothrow_copy_assignable_v<MaterialComponent>);
        output=candidate;return true;
    }catch(...){return false;}
}
inline SceneValue EncodeLight(const LightComponent& light) {
    SceneValue value;value["type"]=light.kind==LightKind::Directional?"directional":light.kind==LightKind::Point?"point":light.kind==LightKind::Spot?"spot":"invalid";
    value["enabled"]=light.enabled;
    value["color"]=crude_json::array{double(light.color.x),double(light.color.y),double(light.color.z)};
    value["intensity"]=double(light.intensity);value["range"]=double(light.range);value["decay"]=double(light.decay);
    value["innerAngle"]=double(light.innerAngle);value["outerAngle"]=double(light.outerAngle);return value;
}
inline SceneValue EncodeMaterial(const MaterialComponent& material) {
    SceneValue value;value["lighting"]=material.lighting;
    value["color"]=crude_json::array{double(material.color.x),double(material.color.y),double(material.color.z),double(material.color.w)};
    value["shininess"]=double(material.shininess);value["roughness"]=double(material.roughness);
    value["reflectionStrength"]=double(material.reflectionStrength);value["textureInfluence"]=double(material.textureInfluence);return value;
}
}
