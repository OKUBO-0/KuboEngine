#include "ParticleComponents.h"
#include <algorithm>
#include <limits>
namespace Engine::Scene {
namespace {
bool Vec(Vector3 v,float lo,float hi){using RenderComponentDetail::Range;return Range(v.x,lo,hi)&&Range(v.y,lo,hi)&&Range(v.z,lo,hi);}
bool Color(Vector4 v){using RenderComponentDetail::Range;return Range(v.x,0,1)&&Range(v.y,0,1)&&Range(v.z,0,1)&&Range(v.w,0,1);}
template<class T> bool ReadArray(const SceneValue& value,const char* key,T& out,size_t count,float lo,float hi){
    if(!value.contains(key))return true;const auto& array=value[key];if(!array.is_array()||array.get<crude_json::array>().size()!=count)return false;
    float values[4]{};for(size_t i=0;i<count;++i){if(!array[i].is_number())return false;const auto n=array[i].get<double>();if(!std::isfinite(n)||n<double(lo)||n>double(hi))return false;values[i]=static_cast<float>(n);}
    if constexpr(std::is_same_v<T,Vector3>)out={values[0],values[1],values[2]};else out={values[0],values[1],values[2],values[3]};return true;
}
bool Integer(const SceneValue& value,const char* key,uint32_t& out,uint32_t lo,uint32_t hi){
    if(!value.contains(key))return true;if(!value[key].is_number())return false;const auto n=value[key].get<double>();
    if(!std::isfinite(n)||n<double(lo)||n>double(hi)||std::floor(n)!=n)return false;out=static_cast<uint32_t>(n);return true;
}
bool Same(Vector3 a,Vector3 b){return a.x==b.x&&a.y==b.y&&a.z==b.z;}
bool Same(Vector4 a,Vector4 b){return a.x==b.x&&a.y==b.y&&a.z==b.z&&a.w==b.w;}
SceneValue Array(Vector3 v){return crude_json::array{double(v.x),double(v.y),double(v.z)};}
SceneValue Array(Vector4 v){return crude_json::array{double(v.x),double(v.y),double(v.z),double(v.w)};}
template<class Keys,class Value,class Access,class Mix>
Value Sample(const Keys& keys,float age,Value first,Value last,Access access,Mix mix) noexcept {
    if(!(age>0))return first;
    if(age>=1)return last;
    float from=0;
    for(const auto& key:keys){
        if(age<=key.age)return mix(first,access(key),(age-from)/(key.age-from));
        first=access(key);from=key.age;
    }
    return mix(first,last,(age-from)/(1-from));
}
template<class Keys,class Validate>
bool ValidKeys(const Keys& keys,Validate validate) noexcept {
    if(keys.size()>ParticleComponent::MaximumCurveKeys)return false;
    float previous=0;
    for(const auto& key:keys){
        if(!(key.age>previous && key.age<1) || !validate(key))return false;
        previous=key.age;
    }
    return true;
}
template<size_t Dimension,class Add>
bool ReadKeys(const SceneValue& value,const char* key,double minimum,double maximum,Add add) {
    if(!value.contains(key))return true;
    const auto& entries=value[key];
    if(!entries.is_array() || entries.get<crude_json::array>().size()>ParticleComponent::MaximumCurveKeys)return false;
    double previous=0;
    for(const auto& entry:entries.get<crude_json::array>()){
        if(!entry.is_array() || entry.get<crude_json::array>().size()!=Dimension+1 || !entry[0].is_number())return false;
        const auto age=entry[0].get<double>();
        if(!(age>previous && age<1))return false;
        previous=age;
        float components[Dimension]{};
        for(size_t i=0;i<Dimension;++i){
            if(!entry[i+1].is_number())return false;
            const auto n=entry[i+1].get<double>();
            if(!std::isfinite(n) || n<minimum || n>maximum)return false;
            components[i]=static_cast<float>(n);
        }
        add(static_cast<float>(age),components);
    }
    return true;
}
}
float ParticleComponent::SizeAt(float age) const noexcept {
    return Sample(sizeKeys,age,size,endSize.value_or(size),[](const auto& key){return key.size;},
        [](float a,float b,float t){return std::lerp(a,b,t);});
}
Vector4 ParticleComponent::ColorAt(float age) const noexcept {
    return Sample(colorKeys,age,startColor,endColor,[](const auto& key){return key.color;},
        [](Vector4 a,Vector4 b,float t){return Vector4{std::lerp(a.x,b.x,t),std::lerp(a.y,b.y,t),
            std::lerp(a.z,b.z,t),std::lerp(a.w,b.w,t)};});
}
bool ParticleComponent::IsValid() const noexcept {
    using RenderComponentDetail::Range;
    return UIComponentDetail::AssetPath(assetPath)&&UIComponentDetail::Guid(assetGuid)&&
        (geometry==ParticleGeometry::Quad||geometry==ParticleGeometry::Ring||geometry==ParticleGeometry::Cylinder)&&
        capacity>=1&&capacity<=4096&&burst<=capacity&&Range(rate,0,4096)&&Range(lifetime,1.f/240,60)&&Range(size,0.0001f,1000)&&
        Vec(velocity,-1000,1000)&&Vec(spread,0,1000)&&Color(startColor)&&Color(endColor)&&
        (!endSize || Range(*endSize,0,1000))&&
        ValidKeys(sizeKeys,[](const auto& key){return Range(key.size,0,1000);})&&
        ValidKeys(colorKeys,[](const auto& key){return Color(key.color);});
}
bool ParticleComponent::operator==(const ParticleComponent& b) const noexcept {
    return enabled==b.enabled&&assetPath==b.assetPath&&assetGuid==b.assetGuid&&geometry==b.geometry&&capacity==b.capacity&&burst==b.burst&&seed==b.seed&&rate==b.rate&&lifetime==b.lifetime&&size==b.size&&Same(velocity,b.velocity)&&Same(spread,b.spread)&&Same(startColor,b.startColor)&&Same(endColor,b.endColor)&&
        endSize==b.endSize&&sizeKeys==b.sizeKeys&&std::equal(colorKeys.begin(),colorKeys.end(),b.colorKeys.begin(),b.colorKeys.end(),
            [](const auto& a,const auto& b){return a.age==b.age&&Same(a.color,b.color);});
}
bool DecodeParticle(const SceneValue& value,ParticleComponent& output) noexcept {
    try {
        using namespace RenderComponentDetail;
        if(!Keys(value,{"enabled","asset","guid","geometry","capacity","burst","seed","rate","lifetime","size","velocity","spread","startColor","endColor","endSize","sizeKeys","colorKeys"}))return false;
        ParticleComponent candidate;
        for(const auto* key:{"asset","guid"})if(value.contains(key)){if(!value[key].is_string())return false;(key[0]=='a'?candidate.assetPath:candidate.assetGuid)=value[key].get<crude_json::string>();}
        if(value.contains("geometry")){if(!value["geometry"].is_string())return false;const auto& v=value["geometry"].get<crude_json::string>();if(v=="quad")candidate.geometry=ParticleGeometry::Quad;else if(v=="ring")candidate.geometry=ParticleGeometry::Ring;else if(v=="cylinder")candidate.geometry=ParticleGeometry::Cylinder;else return false;}
        if(!Boolean(value,"enabled",candidate.enabled)||!Integer(value,"capacity",candidate.capacity,1,4096)||!Integer(value,"burst",candidate.burst,0,4096)||!Integer(value,"seed",candidate.seed,0,(std::numeric_limits<uint32_t>::max)())||
           !Number(value,"rate",candidate.rate,0,4096)||!Number(value,"lifetime",candidate.lifetime,1.f/240,60)||!Number(value,"size",candidate.size,0.0001f,1000)||
           !ReadArray(value,"velocity",candidate.velocity,3,-1000,1000)||!ReadArray(value,"spread",candidate.spread,3,0,1000)||!ReadArray(value,"startColor",candidate.startColor,4,0,1)||!ReadArray(value,"endColor",candidate.endColor,4,0,1))return false;
        if(value.contains("endSize")){
            float end=0;
            if(!Number(value,"endSize",end,0,1000))return false;
            candidate.endSize=end;
        }
        if(!ReadKeys<1>(value,"sizeKeys",0,1000,[&](float age,const float* n){candidate.sizeKeys.push_back({age,n[0]});}) ||
            !ReadKeys<4>(value,"colorKeys",0,1,[&](float age,const float* n){candidate.colorKeys.push_back({age,{n[0],n[1],n[2],n[3]}});}) ||
            !candidate.IsValid())return false;
        static_assert(std::is_nothrow_move_assignable_v<ParticleComponent>);output=std::move(candidate);return true;
    }catch(...){return false;}
}
SceneValue EncodeParticle(const ParticleComponent& value){
    SceneValue result;result["enabled"]=value.enabled;result["asset"]=value.assetPath;if(!value.assetGuid.empty())result["guid"]=value.assetGuid;
    result["geometry"]=value.geometry==ParticleGeometry::Quad?"quad":value.geometry==ParticleGeometry::Ring?"ring":value.geometry==ParticleGeometry::Cylinder?"cylinder":"invalid";
    result["capacity"]=double(value.capacity);result["burst"]=double(value.burst);result["seed"]=double(value.seed);result["rate"]=double(value.rate);result["lifetime"]=double(value.lifetime);result["size"]=double(value.size);
    result["velocity"]=Array(value.velocity);result["spread"]=Array(value.spread);result["startColor"]=Array(value.startColor);result["endColor"]=Array(value.endColor);
    if(value.endSize)result["endSize"]=double(*value.endSize);
    crude_json::array sizes,colors;
    for(const auto& key:value.sizeKeys)sizes.emplace_back(crude_json::array{double(key.age),double(key.size)});
    for(const auto& key:value.colorKeys)colors.emplace_back(crude_json::array{double(key.age),double(key.color.x),
        double(key.color.y),double(key.color.z),double(key.color.w)});
    if(!sizes.empty())result["sizeKeys"]=std::move(sizes);
    if(!colors.empty())result["colorKeys"]=std::move(colors);
    return result;
}
double RequiredEntityDocumentVersion(const SceneValue& world){
    if(world.is_object() && world.contains("environment"))return 3;
    if(world.is_object()&&world.contains("entities")&&world["entities"].is_array())for(const auto& node:world["entities"].get<crude_json::array>())if(node.is_object()&&node.contains("particle"))return 2;
    return 1;
}
void RetagEntityDocument(SceneValue& world){world["version"]=RequiredEntityDocumentVersion(world);}
}
