#pragma once

#include "RenderComponents.h"
#include "../../base/render/PostEffectType.h"
#include <algorithm>
#include <array>

namespace Engine::Scene {

inline constexpr std::array<std::string_view,6> ScenePostEffectNames{
    "none","grayscale","vignette","boxBlur","luminanceOutline","radialBlur"
};

struct SceneEnvironment {
    inline static const Vector3 LegacyBackground{.55f,.68f,.92f};
    Vector3 background=LegacyBackground;
    float exposure=0;
    float reflectionIntensity=1;
    PostEffectType postEffect=PostEffectType::Fullscreen;
    bool showSkybox=false;

    bool IsValid() const noexcept {
        using RenderComponentDetail::Range;
        return Range(background.x,0,1) && Range(background.y,0,1) && Range(background.z,0,1) &&
            Range(exposure,-8,8) && Range(reflectionIntensity,0,1) &&
            size_t(postEffect)<ScenePostEffectNames.size();
    }

    bool IsDefault() const noexcept {
        return background.x==LegacyBackground.x && background.y==LegacyBackground.y && background.z==LegacyBackground.z &&
            exposure==0 && reflectionIntensity==1 && postEffect==PostEffectType::Fullscreen && !showSkybox;
    }

    static SceneEnvironment Day() noexcept {return {{.55f,.68f,.92f},0,1,PostEffectType::Fullscreen,true};}
    static SceneEnvironment Night() noexcept {return {{.015f,.027f,.075f},-1.8f,.25f,PostEffectType::Fullscreen,true};}
};

inline bool DecodeSceneEnvironment(const SceneValue& json,SceneEnvironment& output) {
    using namespace RenderComponentDetail;
    if(!Keys(json,{"background","exposure","reflectionIntensity","postEffect","showSkybox"}))return false;
    SceneEnvironment candidate;
    float color[]{candidate.background.x,candidate.background.y,candidate.background.z};
    if(!Color(json,3,color,"background") || !Number(json,"exposure",candidate.exposure,-8,8) ||
        !Number(json,"reflectionIntensity",candidate.reflectionIntensity,0,1) ||
        !Boolean(json,"showSkybox",candidate.showSkybox))return false;
    candidate.background={color[0],color[1],color[2]};
    if(json.contains("postEffect")){
        const auto& effect=json["postEffect"];
        if(!effect.is_string())return false;
        const auto& name=effect.get<crude_json::string>();
        const auto found=std::find(ScenePostEffectNames.begin(),ScenePostEffectNames.end(),name);
        if(found==ScenePostEffectNames.end())return false;
        candidate.postEffect=static_cast<PostEffectType>(found-ScenePostEffectNames.begin());
    }
    output=candidate;
    return true;
}

inline SceneValue EncodeSceneEnvironment(const SceneEnvironment& settings) {
    SceneValue json;
    json["background"]=crude_json::array{double(settings.background.x),double(settings.background.y),double(settings.background.z)};
    json["exposure"]=double(settings.exposure);
    json["reflectionIntensity"]=double(settings.reflectionIntensity);
    json["postEffect"]=std::string(ScenePostEffectNames.at(size_t(settings.postEffect)));
    json["showSkybox"]=settings.showSkybox;
    return json;
}

} // namespace Engine::Scene
