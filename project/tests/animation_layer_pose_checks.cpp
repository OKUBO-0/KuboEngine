#include "engine/animation/AnimationLayerPose.h"
#include <iostream>
#include <stdexcept>
#include <limits>
static void Need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
static bool Near(float a,float b){return std::abs(a-b)<0.0001f;}
int main(){try{
    using namespace Engine::AnimationSystem;
    Skeleton rig{};
    for(const auto* name:{"Rig::Root","Rig::Spine","Rig::Hand","Rig::Foot"}){
        Joint joint{};joint.name=name;joint.transform={{2,2,2},{0,0,0,1},{4,0,0}};rig.joints.push_back(joint);
    }
    rig.joints[1].parent=0;rig.joints[2].parent=1;rig.joints[3].parent=0;
    BoneMask mask{7};const std::vector<std::string> upper{"Spine"};
    Need(ResolveBoneMask(rig,upper,mask) && mask==BoneMask({0,1,1,0}),"Subtree mask did not include the hand/exclude foot");
    Need(ResolveBoneMask(rig,std::vector<std::string>{"Rig::Spine"},mask),"Full-name mask failed");
    const auto heldMask=mask;
    for(const auto& bad:std::vector<std::vector<std::string>>{{"missing"},{"Spine","Rig::Spine"},{""},{std::string(129,'x')}})
        Need(!ResolveBoneMask(rig,bad,mask) && mask==heldMask,"Invalid/duplicate resolved mask changed output");
    auto ambiguous=rig;ambiguous.joints[3].name="Other::Spine";
    Need(!ResolveBoneMask(ambiguous,upper,mask) && mask==heldMask,"Ambiguous short-name mask admitted");
    Need(ResolveBoneMask(ambiguous,std::vector<std::string>{"Rig::Spine"},mask) && mask==heldMask,"Exact name did not resolve ambiguity");
    auto cycle=rig;cycle.joints[0].parent=2;
    Need(!ResolveBoneMask(cycle,{},mask) && mask==heldMask,"Cyclic rig admitted");
    cycle=rig;cycle.joints[1].parent=-1;
    Need(!ResolveBoneMask(cycle,{},mask) && mask==heldMask,"Negative parent admitted");
    cycle=rig;cycle.joints[1].parent=9;
    Need(!ResolveBoneMask(cycle,{},mask) && mask==heldMask,"Out-of-range parent admitted");
    BoneMask all;Need(ResolveBoneMask(rig,{},all) && all==BoneMask({1,1,1,1}),"Empty mask did not select all bones");
    std::vector<QuaternionTransform> reference;for(const auto& j:rig.joints)reference.push_back(j.transform);
    auto base=reference,layer=reference;for(auto& p:base)p.translate.x=10;
    for(auto& p:layer){p.translate.x=8;p.scale={4,4,4};p.rotate={0,0,1,0};}
    std::vector<QuaternionTransform> result;
    Need(ComposeMaskedLayer(base,layer,reference,mask,PoseLayerMode::Override,0.5f,result),"Masked override failed");
    Need(Near(result[0].translate.x,10) && Near(result[1].translate.x,9) && Near(result[2].scale.x,3) && Near(result[3].translate.x,10),"Override leaked outside subtree/weight");
    Need(Near(result[1].rotate.z,std::sqrt(0.5f)),"Override quaternion slerp failed");
    Need(ComposeMaskedLayer(base,layer,reference,mask,PoseLayerMode::Additive,0.5f,result),"Masked additive failed");
    Need(Near(result[1].translate.x,12) && Near(result[2].scale.x,3) && Near(result[0].scale.x,2) && Near(result[3].translate.x,10),"Additive reference/scale ratio/mask failed");
    Need(Near(result[1].rotate.z,std::sqrt(0.5f)) && Near(result[1].rotate.w,std::sqrt(0.5f)),"Additive reference rotation failed");
    auto ordered=result;
    Need(ComposeMaskedLayer(ordered,layer,reference,mask,PoseLayerMode::Override,0.5f,ordered) && Near(ordered[1].translate.x,10),"Aliased output/authored layer order failed");
    // Noncommuting rotations prove reference-local delta/order rather than qLayer alone.
    const float h=std::sqrt(0.5f);reference[1].rotate={h,0,0,h};
    layer[1].rotate=reference[1].rotate*Quaternion{0,0,h,h};base[1].rotate={0,h,0,h};
    Need(ComposeMaskedLayer(base,layer,reference,mask,PoseLayerMode::Additive,1,result),"Local rotation delta failed");
    const auto expected=base[1].rotate*Quaternion{0,0,h,h};
    Need(Near(result[1].rotate.x,expected.x) && Near(result[1].rotate.y,expected.y) && Near(result[1].rotate.z,expected.z) && Near(result[1].rotate.w,expected.w),"Additive quaternion multiplication order ignored reference");
    Need(ComposeMaskedLayer(base,reference,reference,mask,PoseLayerMode::Additive,1,result) && Near(result[1].translate.x,base[1].translate.x) && Near(result[1].rotate.y,base[1].rotate.y),"Reference pose was not additive identity");
    Need(ComposeMaskedLayer(base,layer,reference,mask,PoseLayerMode::Additive,0,result) && result[1].rotate.y==base[1].rotate.y && result[1].translate.x==base[1].translate.x,"Weight zero changed base");
    const auto retained=result.data();const auto refuse=[&](const auto& a,const auto& b,const auto& ref,const auto& m,PoseLayerMode mode,float weight){Need(!ComposeMaskedLayer(a,b,ref,m,mode,weight,result) && result.data()==retained,"Malformed layer partially published");};
    auto bad=layer;bad[3].translate.x=std::numeric_limits<float>::quiet_NaN();refuse(base,bad,reference,mask,PoseLayerMode::Override,1);
    bad=reference;bad[1].scale.x=0;refuse(base,layer,bad,mask,PoseLayerMode::Additive,1);
    bad=layer;bad.pop_back();refuse(base,bad,reference,mask,PoseLayerMode::Override,1);
    auto badMask=mask;badMask[0]=2;refuse(base,layer,reference,badMask,PoseLayerMode::Override,1);
    refuse(base,layer,reference,mask,PoseLayerMode::Override,std::numeric_limits<float>::infinity());
    refuse(base,layer,reference,mask,static_cast<PoseLayerMode>(9),1);
    bad=layer;bad[1].rotate={0,0,0,0};refuse(base,bad,reference,mask,PoseLayerMode::Override,1);
    bad=layer;bad[1].translate.x=(std::numeric_limits<float>::max)();auto extreme=base;extreme[1].translate.x=(std::numeric_limits<float>::max)();
    auto low=reference;low[1].translate.x=-(std::numeric_limits<float>::max)();refuse(extreme,bad,low,mask,PoseLayerMode::Additive,1);
    bad=layer;bad[1].scale.x=(std::numeric_limits<float>::max)();extreme=base;extreme[1].scale.x=(std::numeric_limits<float>::max)();
    low=reference;low[1].scale.x=std::numeric_limits<float>::min();refuse(extreme,bad,low,mask,PoseLayerMode::Additive,1);
    auto signedReference=reference,signedLayer=reference,signedBase=base;
    signedReference[1].scale.x=-2;signedLayer[1].scale.x=-4;signedBase[1].scale.x=-3;
    Need(ComposeMaskedLayer(signedBase,signedLayer,signedReference,mask,PoseLayerMode::Additive,0.5f,result) && Near(result[1].scale.x,-4.5f),"Mirrored reference scale ratio failed");
    const std::vector<std::string> tooMany(257,"Spine");
    Need(!ResolveBoneMask(rig,tooMany,mask) && mask==heldMask,"Mask entry limit changed output");
    std::cout<<"PASS: production local-TRS layer override/additive/reference rotation+scale/subtree masks/exact-name+ambiguous alias/cycle guards/ordered alias-safe composition/fail-closed output (CPU composition; Object3D checks separate)\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
