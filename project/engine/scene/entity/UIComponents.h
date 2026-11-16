#pragma once
#include "RenderComponents.h"
#include "Vector2.h"
#include "../../script/ScriptProperty.h"
#include <string>
#include <utility>

namespace Engine::Scene {
enum class CanvasScaleMode { ConstantPixels, ScaleWithOutput };
// Authored data only: no viewport, GPU handle or computed layout is persisted.
struct CanvasComponent {
    bool enabled=true;
    Vector2 referenceSize{1280,720};
    CanvasScaleMode scaleMode=CanvasScaleMode::ScaleWithOutput;
    int sortOrder=0;
    bool IsValid() const noexcept;
};
struct SpriteComponent {
    bool enabled=true;
    // GUID authoritative when assigned. Empty references are an unassigned draft.
    std::string assetPath,assetGuid;
    Vector2 size{100,100},pivot{0.5f,0.5f};
    Vector4 color{1,1,1,1};
    bool flipX=false,flipY=false;
    int sortOrder=0;
    bool IsValid() const noexcept;
};
namespace UIComponentDetail {
inline bool AssetPath(std::string_view path) noexcept {
    if(!Scripting::ValidScriptText(path,4096))return false;
    if(path.empty())return true;
    // Canonical project-relative UTF-8, forward slashes only. Lexical safety is
    // not asset existence/type validation or a filesystem containment check.
    size_t begin=0;
    while(begin<path.size()) {
        const auto end=path.find('/',begin);
        const auto part=path.substr(begin,end==path.npos?path.size()-begin:end-begin);
        if(part.empty() || part=="." || part==".." || part.back()=='.' || part.back()==' ')return false;
        for(const unsigned char c:part)if(c<32 || c==127 || c=='\\' || c==':' || c=='*' || c=='?' || c=='"' || c=='<' || c=='>' || c=='|')return false;
        const auto base=part.substr(0,part.find('.'));
        const auto equal=[&](std::string_view reserved) {
            if(base.size()!=reserved.size())return false;
            for(size_t i=0;i<base.size();++i){char c=base[i];if(c>='a' && c<='z')c-=32;if(c!=reserved[i])return false;}return true;
        };
        if(equal("CON") || equal("PRN") || equal("AUX") || equal("NUL"))return false;
        if(base.size()==4 && base[3]>='1' && base[3]<='9' &&
            ((base[0]=='C'||base[0]=='c') && (base[1]=='O'||base[1]=='o') && (base[2]=='M'||base[2]=='m') ||
             (base[0]=='L'||base[0]=='l') && (base[1]=='P'||base[1]=='p') && (base[2]=='T'||base[2]=='t')))return false;
        if(end==path.npos)return true;
        begin=end+1;
    }
    return false; // trailing slash
}
inline bool Guid(std::string_view guid) noexcept {
    if(guid.empty())return true;
    if(guid.size()!=16 && guid.size()!=32)return false;
    for(char c:guid)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;
    return true;
}
inline bool Pair(const SceneValue& object,const char* key,Vector2& out,float lo,float hi) {
    if(!object.contains(key))return true;
    const auto& v=object[key];if(!v.is_array() || v.get<crude_json::array>().size()!=2)return false;
    float result[2];for(size_t i=0;i<2;++i){
        if(!v[i].is_number())return false;
        const auto n=v[i].get<crude_json::number>();
        if(!std::isfinite(n) || n<double(lo) || n>double(hi))return false;
        result[i]=static_cast<float>(n);
    }
    out={result[0],result[1]};return true;
}
inline bool Order(const SceneValue& object,int& out) {
    if(!object.contains("sortOrder"))return true;
    const auto& v=object["sortOrder"];if(!v.is_number())return false;
    const auto n=v.get<crude_json::number>();
    if(!std::isfinite(n) || n<-32768 || n>32767 || std::floor(n)!=n)return false;
    out=static_cast<int>(n);return true;
}
inline SceneValue Pair(Vector2 v) {return crude_json::array{double(v.x),double(v.y)};}
}
inline bool CanvasComponent::IsValid() const noexcept {
    using RenderComponentDetail::Range;
    return Range(referenceSize.x,1,16384) && Range(referenceSize.y,1,16384) &&
        (scaleMode==CanvasScaleMode::ConstantPixels || scaleMode==CanvasScaleMode::ScaleWithOutput) && sortOrder>=-32768 && sortOrder<=32767;
}
inline bool SpriteComponent::IsValid() const noexcept {
    using RenderComponentDetail::Range;
    return UIComponentDetail::AssetPath(assetPath) && UIComponentDetail::Guid(assetGuid) &&
        Range(size.x,0,16384) && Range(size.y,0,16384) && Range(pivot.x,-16,16) && Range(pivot.y,-16,16) &&
        Range(color.x,0,1) && Range(color.y,0,1) && Range(color.z,0,1) && Range(color.w,0,1) && sortOrder>=-32768 && sortOrder<=32767;
}
inline bool DecodeCanvas(const SceneValue& value,CanvasComponent& output) noexcept {
    try {
        using namespace RenderComponentDetail;
        if(!Keys(value,{"enabled","referenceSize","scaleMode","sortOrder"}))return false;
        CanvasComponent candidate;
        if(!Boolean(value,"enabled",candidate.enabled) || !UIComponentDetail::Pair(value,"referenceSize",candidate.referenceSize,1,16384) || !UIComponentDetail::Order(value,candidate.sortOrder))return false;
        if(value.contains("scaleMode")){
            if(!value["scaleMode"].is_string())return false;
            const auto& mode=value["scaleMode"].get<crude_json::string>();
            if(mode=="constantPixels")candidate.scaleMode=CanvasScaleMode::ConstantPixels;
            else if(mode=="scaleWithOutput")candidate.scaleMode=CanvasScaleMode::ScaleWithOutput;
            else return false;
        }
        if(!candidate.IsValid())return false;
        static_assert(std::is_nothrow_copy_assignable_v<CanvasComponent>);
        output=candidate;return true;
    }catch(...){return false;}
}
inline bool DecodeSprite(const SceneValue& value,SpriteComponent& output) noexcept {
    try {
        using namespace RenderComponentDetail;
        if(!Keys(value,{"enabled","asset","guid","size","pivot","color","flipX","flipY","sortOrder"}))return false;
        SpriteComponent candidate;
        for(const auto* key:{"asset","guid"})if(value.contains(key)){
            if(!value[key].is_string())return false;
            (key[0]=='a'?candidate.assetPath:candidate.assetGuid)=value[key].get<crude_json::string>();
        }
        float color[]{1,1,1,1};
        if(!Boolean(value,"enabled",candidate.enabled) || !Boolean(value,"flipX",candidate.flipX) || !Boolean(value,"flipY",candidate.flipY) ||
            !UIComponentDetail::Pair(value,"size",candidate.size,0,16384) || !UIComponentDetail::Pair(value,"pivot",candidate.pivot,-16,16) ||
            !Color(value,4,color) || !UIComponentDetail::Order(value,candidate.sortOrder))return false;
        candidate.color={color[0],color[1],color[2],color[3]};
        if(!candidate.IsValid())return false;
        static_assert(std::is_nothrow_move_assignable_v<SpriteComponent>);
        output=std::move(candidate);return true;
    }catch(...){return false;}
}
inline SceneValue EncodeCanvas(const CanvasComponent& canvas) {
    SceneValue v;v["enabled"]=canvas.enabled;v["referenceSize"]=UIComponentDetail::Pair(canvas.referenceSize);
    v["scaleMode"]=canvas.scaleMode==CanvasScaleMode::ConstantPixels?"constantPixels":canvas.scaleMode==CanvasScaleMode::ScaleWithOutput?"scaleWithOutput":"invalid";
    v["sortOrder"]=double(canvas.sortOrder);return v;
}
inline SceneValue EncodeSprite(const SpriteComponent& sprite) {
    SceneValue v;v["enabled"]=sprite.enabled;v["asset"]=sprite.assetPath;if(!sprite.assetGuid.empty())v["guid"]=sprite.assetGuid;
    v["size"]=UIComponentDetail::Pair(sprite.size);v["pivot"]=UIComponentDetail::Pair(sprite.pivot);
    v["color"]=crude_json::array{double(sprite.color.x),double(sprite.color.y),double(sprite.color.z),double(sprite.color.w)};
    v["flipX"]=sprite.flipX;v["flipY"]=sprite.flipY;v["sortOrder"]=double(sprite.sortOrder);return v;
}
}
