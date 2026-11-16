#pragma once
#include "engine/math/data/RenderingData.h"
#include <algorithm>
#include <cmath>
#include <string_view>
#include <type_traits>

namespace Engine::AnimationSystem {
// Local TRS only: hierarchy/palette updates belong to the owning Object3D.
inline bool Finite(const Vector3& v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
inline bool NormalizeRotation(const Quaternion& q,Quaternion& result) noexcept {
    const double length=std::sqrt(double(q.x)*q.x+double(q.y)*q.y+double(q.z)*q.z+double(q.w)*q.w);
    if(!std::isfinite(length) || length<=1e-12)return false;
    result={float(q.x/length),float(q.y/length),float(q.z/length),float(q.w/length)};return true;
}
inline Vector3 Interpolate(const Vector3& a,const Vector3& b,float weight) noexcept {
    return {float(double(a.x)*(1-weight)+double(b.x)*weight),float(double(a.y)*(1-weight)+double(b.y)*weight),float(double(a.z)*(1-weight)+double(b.z)*weight)};
}
inline bool InterpolateRotation(const Quaternion& a,const Quaternion& b,float weight,Quaternion& output) noexcept {
    Quaternion from,to;if(!NormalizeRotation(a,from) || !NormalizeRotation(b,to))return false;
    double dot=double(from.x)*to.x+double(from.y)*to.y+double(from.z)*to.z+double(from.w)*to.w;
    if(dot<0){to=to*-1;dot=-dot;}
    dot=(std::clamp)(dot,0.0,1.0);
    if(dot>0.9995)return NormalizeRotation(from*(1-weight)+to*weight,output);
    const double angle=std::acos(dot),denominator=std::sin(angle);
    return NormalizeRotation(from*float(std::sin((1-weight)*angle)/denominator)+to*float(std::sin(weight*angle)/denominator),output);
}
inline bool ValidValue(const Vector3& v) noexcept {return Finite(v);}
inline bool ValidValue(const Quaternion& q) noexcept {Quaternion unit;return NormalizeRotation(q,unit);}
template<class T> inline bool SampleKeys(const std::vector<Keyframe<T>>& keys,float time,T& output) {
    if(keys.empty())return true; // caller supplied bind-pose channel
    for(size_t i=0;i<keys.size();++i)
        if(!std::isfinite(keys[i].time) || keys[i].time<0 || !ValidValue(keys[i].value) || (i && keys[i].time<=keys[i-1].time))return false;
    if(time<=keys.front().time){output=keys.front().value;return true;}
    if(time>=keys.back().time){output=keys.back().value;return true;}
    const auto next=std::upper_bound(keys.begin(),keys.end(),time,[](float t,const auto& key){return t<key.time;});
    const auto& before=*(next-1);const float weight=(time-before.time)/(next->time-before.time);
    if constexpr(std::is_same_v<T,Quaternion>)return InterpolateRotation(before.value,next->value,weight,output);
    else {output=Interpolate(before.value,next->value,weight);return Finite(output);}
}
inline bool SampleLocalPose(const Skeleton& bind,const Animation& clip,float time,
    std::vector<QuaternionTransform>& output) {
    if(!std::isfinite(time) || time<0 || !std::isfinite(clip.duration) || clip.duration<0)return false;
    std::vector<QuaternionTransform> candidate;candidate.reserve(bind.joints.size());
    for(const auto& joint:bind.joints) {
        auto pose=joint.transform;
        if(!Finite(pose.translate) || !Finite(pose.scale) || !NormalizeRotation(pose.rotate,pose.rotate))return false;
        std::string_view name=joint.name;const auto separator=name.find("::");
        if(separator!=std::string_view::npos)name.remove_prefix(separator+2);
        const auto channel=clip.nodeAnimations.find(std::string(name));
        if(channel!=clip.nodeAnimations.end()) {
            const auto& track=channel->second;
            if(!SampleKeys(track.translate,time,pose.translate) || !SampleKeys(track.scale,time,pose.scale) ||
               !SampleKeys(track.rotate,time,pose.rotate) || !NormalizeRotation(pose.rotate,pose.rotate))return false;
        }
        candidate.push_back(pose);
    }
    output.swap(candidate);return true;
}
// Both clips are sampled against the SAME immutable bind skeleton. Missing
// channels never inherit the previously played clip. Output commits at the end.
inline bool BlendLocalPoses(const std::vector<QuaternionTransform>& source,
    const std::vector<QuaternionTransform>& target,float weight,std::vector<QuaternionTransform>& output) {
    if(source.size()!=target.size() || !std::isfinite(weight) || weight<0 || weight>1)return false;
    auto candidate=source;
    for(size_t i=0;i<candidate.size();++i) {
        if(!Finite(source[i].translate) || !Finite(source[i].scale) || !Finite(target[i].translate) || !Finite(target[i].scale))return false;
        candidate[i].translate=Interpolate(source[i].translate,target[i].translate,weight);
        candidate[i].scale=Interpolate(source[i].scale,target[i].scale,weight);
        if(!InterpolateRotation(source[i].rotate,target[i].rotate,weight,candidate[i].rotate))return false;
    }
    output.swap(candidate);return true;
}
inline bool SampleBlendedLocalPose(const Skeleton& bind,const Animation& source,float sourceTime,
    const Animation& target,float targetTime,float weight,std::vector<QuaternionTransform>& output) {
    if(!std::isfinite(weight) || weight<0 || weight>1)return false;
    std::vector<QuaternionTransform> from,to;
    if(!SampleLocalPose(bind,source,sourceTime,from) || !SampleLocalPose(bind,target,targetTime,to))return false;
    return BlendLocalPoses(from,to,weight,output);
}
}
