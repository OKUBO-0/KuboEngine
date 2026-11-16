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
}
bool ParticleComponent::IsValid() const noexcept {
    using RenderComponentDetail::Range;
    return UIComponentDetail::AssetPath(assetPath)&&UIComponentDetail::Guid(assetGuid)&&
        (geometry==ParticleGeometry::Quad||geometry==ParticleGeometry::Ring||geometry==ParticleGeometry::Cylinder)&&
        capacity>=1&&capacity<=4096&&burst<=capacity&&Range(rate,0,4096)&&Range(lifetime,1.f/240,60)&&Range(size,0.0001f,1000)&&
        Vec(velocity,-1000,1000)&&Vec(spread,0,1000)&&Color(startColor)&&Color(endColor);
}
bool ParticleComponent::operator==(const ParticleComponent& b) const noexcept {
    return enabled==b.enabled&&assetPath==b.assetPath&&assetGuid==b.assetGuid&&geometry==b.geometry&&capacity==b.capacity&&burst==b.burst&&seed==b.seed&&rate==b.rate&&lifetime==b.lifetime&&size==b.size&&Same(velocity,b.velocity)&&Same(spread,b.spread)&&Same(startColor,b.startColor)&&Same(endColor,b.endColor);
}
bool DecodeParticle(const SceneValue& value,ParticleComponent& output) noexcept {
    try {
        using namespace RenderComponentDetail;
        if(!Keys(value,{"enabled","asset","guid","geometry","capacity","burst","seed","rate","lifetime","size","velocity","spread","startColor","endColor"}))return false;
        ParticleComponent candidate;
        for(const auto* key:{"asset","guid"})if(value.contains(key)){if(!value[key].is_string())return false;(key[0]=='a'?candidate.assetPath:candidate.assetGuid)=value[key].get<crude_json::string>();}
        if(value.contains("geometry")){if(!value["geometry"].is_string())return false;const auto& v=value["geometry"].get<crude_json::string>();if(v=="quad")candidate.geometry=ParticleGeometry::Quad;else if(v=="ring")candidate.geometry=ParticleGeometry::Ring;else if(v=="cylinder")candidate.geometry=ParticleGeometry::Cylinder;else return false;}
        if(!Boolean(value,"enabled",candidate.enabled)||!Integer(value,"capacity",candidate.capacity,1,4096)||!Integer(value,"burst",candidate.burst,0,4096)||!Integer(value,"seed",candidate.seed,0,(std::numeric_limits<uint32_t>::max)())||
           !Number(value,"rate",candidate.rate,0,4096)||!Number(value,"lifetime",candidate.lifetime,1.f/240,60)||!Number(value,"size",candidate.size,0.0001f,1000)||
           !ReadArray(value,"velocity",candidate.velocity,3,-1000,1000)||!ReadArray(value,"spread",candidate.spread,3,0,1000)||!ReadArray(value,"startColor",candidate.startColor,4,0,1)||!ReadArray(value,"endColor",candidate.endColor,4,0,1)||!candidate.IsValid())return false;
        static_assert(std::is_nothrow_move_assignable_v<ParticleComponent>);output=std::move(candidate);return true;
    }catch(...){return false;}
}
SceneValue EncodeParticle(const ParticleComponent& value){
    SceneValue result;result["enabled"]=value.enabled;result["asset"]=value.assetPath;if(!value.assetGuid.empty())result["guid"]=value.assetGuid;
    result["geometry"]=value.geometry==ParticleGeometry::Quad?"quad":value.geometry==ParticleGeometry::Ring?"ring":value.geometry==ParticleGeometry::Cylinder?"cylinder":"invalid";
    result["capacity"]=double(value.capacity);result["burst"]=double(value.burst);result["seed"]=double(value.seed);result["rate"]=double(value.rate);result["lifetime"]=double(value.lifetime);result["size"]=double(value.size);
    result["velocity"]=Array(value.velocity);result["spread"]=Array(value.spread);result["startColor"]=Array(value.startColor);result["endColor"]=Array(value.endColor);return result;
}
double RequiredEntityDocumentVersion(const SceneValue& world){
    if(world.is_object()&&world.contains("entities")&&world["entities"].is_array())for(const auto& node:world["entities"].get<crude_json::array>())if(node.is_object()&&node.contains("particle"))return 2;
    return 1;
}
void RetagEntityDocument(SceneValue& world){world["version"]=RequiredEntityDocumentVersion(world);}
}
