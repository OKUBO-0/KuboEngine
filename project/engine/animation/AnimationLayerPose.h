#pragma once
#include "AnimationPose.h"
#include <limits>
#include <span>
#include <string>

namespace Engine::AnimationSystem {
enum class PoseLayerMode { Override, Additive };
using BoneMask = std::vector<unsigned char>;

inline std::string_view ShortJointName(std::string_view name) noexcept {
    const auto separator=name.find("::");
    return separator==std::string_view::npos?name:name.substr(separator+2);
}

// Named roots include descendants through the parent links. Empty means all.
// Full names take precedence; short names must resolve uniquely. No use of a
// potentially stale jointMap/children cache. Failure leaves the output intact.
inline bool ResolveBoneMask(const Skeleton& bind,std::span<const std::string> names,BoneMask& output) {
    const auto count=bind.joints.size();if(!count || names.size()>256)return false;
    for(size_t i=0;i<count;++i) {
        size_t cursor=i,steps=0;
        while(bind.joints[cursor].parent) {
            const auto parent=*bind.joints[cursor].parent;
            if(parent<0 || size_t(parent)>=count || ++steps>=count)return false;
            cursor=size_t(parent);
        }
    }
    BoneMask roots(count,0),candidate(count,names.empty()?1:0);
    for(const auto& name:names) {
        if(name.empty() || name.size()>128 || name.find('\0')!=std::string::npos)return false;
        size_t chosen=count,matches=0;
        for(size_t i=0;i<count;++i)if(bind.joints[i].name==name){chosen=i;++matches;}
        if(!matches)for(size_t i=0;i<count;++i)if(ShortJointName(bind.joints[i].name)==name){chosen=i;++matches;}
        if(matches!=1 || roots[chosen])return false;
        roots[chosen]=1;
    }
    if(!names.empty())for(size_t i=0;i<count;++i) {
        size_t cursor=i;
        for(;;) {
            if(roots[cursor]){candidate[i]=1;break;}
            if(!bind.joints[cursor].parent)break;
            cursor=size_t(*bind.joints[cursor].parent);
        }
    }
    output.swap(candidate);return true;
}

inline bool LayerFloat(double value,float& output) noexcept {
    if(!std::isfinite(value) || std::abs(value)>(std::numeric_limits<float>::max)())return false;
    output=static_cast<float>(value);return true;
}

// Ordered local-TRS composition, before the owner's hierarchy/palette update.
// Additive uses a bind reference: translation difference, scale ratio and
// right-multiplied local rotation delta. Weight zero preserves base bytes.
inline bool ComposeMaskedLayer(const std::vector<QuaternionTransform>& base,
    const std::vector<QuaternionTransform>& layer,const std::vector<QuaternionTransform>& reference,
    const BoneMask& mask,PoseLayerMode mode,float weight,std::vector<QuaternionTransform>& output) {
    if(base.empty() || layer.size()!=base.size() || reference.size()!=base.size() || mask.size()!=base.size() ||
        (mode!=PoseLayerMode::Override && mode!=PoseLayerMode::Additive) ||
        !std::isfinite(weight) || weight<0 || weight>1)return false;
    auto candidate=base;
    for(size_t i=0;i<base.size();++i) {
        Quaternion from,to,ref;
        if(mask[i]>1 || !Finite(base[i].translate) || !Finite(base[i].scale) ||
            !Finite(layer[i].translate) || !Finite(layer[i].scale) ||
            !Finite(reference[i].translate) || !Finite(reference[i].scale) ||
            !NormalizeRotation(base[i].rotate,from) || !NormalizeRotation(layer[i].rotate,to) ||
            !NormalizeRotation(reference[i].rotate,ref))return false;
        if(!mask[i] || weight==0)continue;
        auto& pose=candidate[i];
        if(mode==PoseLayerMode::Override) {
            pose.translate=Interpolate(base[i].translate,layer[i].translate,weight);
            pose.scale=Interpolate(base[i].scale,layer[i].scale,weight);
            if(!InterpolateRotation(from,to,weight,pose.rotate))return false;
        } else {
            const auto additive=[weight](float b,float l,float r,float& t){return LayerFloat(double(b)+double(weight)*(double(l)-r),t);};
            const auto scale=[weight](float b,float l,float r,float& s){return r!=0 && LayerFloat(double(b)*(1+double(weight)*(double(l)/r-1)),s);};
            if(!additive(base[i].translate.x,layer[i].translate.x,reference[i].translate.x,pose.translate.x) ||
                !additive(base[i].translate.y,layer[i].translate.y,reference[i].translate.y,pose.translate.y) ||
                !additive(base[i].translate.z,layer[i].translate.z,reference[i].translate.z,pose.translate.z) ||
                !scale(base[i].scale.x,layer[i].scale.x,reference[i].scale.x,pose.scale.x) ||
                !scale(base[i].scale.y,layer[i].scale.y,reference[i].scale.y,pose.scale.y) ||
                !scale(base[i].scale.z,layer[i].scale.z,reference[i].scale.z,pose.scale.z))return false;
            const Quaternion inverse{-ref.x,-ref.y,-ref.z,ref.w};
            Quaternion weightedDelta;
            if(!InterpolateRotation({0,0,0,1},inverse*to,weight,weightedDelta) ||
                !NormalizeRotation(from*weightedDelta,pose.rotate))return false;
        }
        if(!Finite(pose.translate) || !Finite(pose.scale))return false;
    }
    output.swap(candidate);return true;
}
}
