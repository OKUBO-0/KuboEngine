#include "engine/animation/AnimationPose.h"
#include <iostream>
#include <stdexcept>
#include <limits>
static void Require(bool v,const char* message){if(!v)throw std::runtime_error(message);}
static bool Near(float a,float b){return std::abs(a-b)<0.0001f;}
int main(){try {
    using namespace Engine::AnimationSystem;
    Skeleton bind{};Joint joint{};joint.name="Rig::Root";joint.transform={{2,2,2},{0,0,0,1},{4,0,0}};bind.joints.push_back(joint);
    Joint child=joint;child.name="Child";child.transform.translate={0,3,0};bind.joints.push_back(child);
    Animation source{},target{};source.duration=target.duration=2;
    source.nodeAnimations["Root"].translate={ {0,{0,0,0}},{2,{8,0,0}} };
    target.nodeAnimations["Root"].rotate={ {0,{0,0,1,0}} };
    std::vector<QuaternionTransform> result,independent;
    Require(SampleBlendedLocalPose(bind,source,0,target,0,0.5f,result),"Pose sampling failed");
    Require(Near(result[0].translate.x,2) && Near(result[0].scale.x,2) && Near(result[1].translate.y,3),"TRS/bind-channel fallback failed");
    Require(Near(result[0].rotate.z,std::sqrt(0.5f)) && Near(result[0].rotate.w,std::sqrt(0.5f)),"Quaternion slerp failed");
    Require(SampleBlendedLocalPose(bind,source,2,target,0,0,independent) && Near(independent[0].translate.x,8) && Near(result[0].translate.x,2) && Near(bind.joints[0].transform.translate.x,4),"Instance/immutable bind isolation failed");
    Require(SampleBlendedLocalPose(bind,source,0,target,0,1,independent) && Near(independent[0].translate.x,4),"Target endpoint inherited source channel");
    Quaternion rotation;Require(InterpolateRotation({0,0,0,2},{0,0,0,-2},0.5f,rotation) && Near(rotation.w,1),"Antipodal/nonunit rotation failed");
    const auto retained=result.data();auto malformed=target;
    malformed.nodeAnimations["Root"].rotate[0].value={0,0,0,0};
    Require(!SampleBlendedLocalPose(bind,source,0,malformed,0,0.5f,result) && result.data()==retained,"Invalid quaternion partially published");
    malformed=source;malformed.nodeAnimations["Root"].translate[1].time=0;
    Require(!SampleLocalPose(bind,malformed,0,result) && result.data()==retained,"Duplicate key time admitted");
    Require(!SampleBlendedLocalPose(bind,source,0,target,0,std::numeric_limits<float>::quiet_NaN(),result) && result.data()==retained,"Nonfinite weight admitted");
    Require(!SampleLocalPose(bind,source,-1,result) && result.data()==retained,"Negative sample time admitted");
    const auto snapshot=result;std::vector<QuaternionTransform> resumed;
    Require(BlendLocalPoses(snapshot,independent,0,resumed) && Near(resumed[0].translate.x,snapshot[0].translate.x),"Snapshot continuation snapped");
    Require(BlendLocalPoses(snapshot,independent,1,resumed) && Near(resumed[0].translate.x,independent[0].translate.x),"Snapshot target endpoint failed");
    auto invalidPose=independent;invalidPose.pop_back();const auto saved=resumed.data();
    Require(!BlendLocalPoses(snapshot,invalidPose,0.5f,resumed) && resumed.data()==saved,"Rig size mismatch partially published");
    std::cout<<"PASS: actual engine TRS pose sampling/slerp/bind fallback/instance isolation/fail-closed output (not Object3D transition playback)\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
