#pragma once
#include "Object3D.h"
#include "engine/animation/AnimationLayerPose.h"
#include <stdexcept>
#include <iostream>

inline void VerifyLayeredObject(Engine::Graphics3D::Object3DCommon* common,Engine::Graphics3D::Model* model,
    const std::string& root,float step) {
    using namespace Engine::AnimationSystem;
    const auto check=[](bool value,const char* why){if(!value)throw std::runtime_error(why);};
    auto first=Engine::Graphics3D::Object3D::Create(common,"walk.gltf",root);
    auto peer=Engine::Graphics3D::Object3D::Create(common,"walk.gltf",root);
    const auto& bind=model->GetSkeleton();const auto* clip=model->FindAnimationClip("default");
    size_t selected=bind.joints.size();for(size_t i=0;i<bind.joints.size();++i)if(bind.joints[i].parent){selected=i;break;}
    check(clip && selected<bind.joints.size(),"Layered actual rig/clip unavailable");
    auto graph=crude_json::value::parse(R"({"format":"kubo-animator-controller-1","parameters":[{"name":"aim","type":"bool","default":false}],"layers":[{"name":"Base","mode":"override","weight":1,"mask":[],"entry":"Idle","states":[{"name":"Idle","clip":"default","speed":1,"loop":true}],"transitions":[]},{"name":"Upper","mode":"additive","weight":0.5,"mask":[],"entry":"Idle","states":[{"name":"Idle","clip":"default","speed":2,"loop":true},{"name":"Aim","clip":"default","speed":3,"loop":true}],"transitions":[{"from":"Idle","to":"Aim","blendSeconds":0.2,"exitTime":null,"reenter":false,"conditions":[{"parameter":"aim","op":"eq","value":true}]},{"from":"Aim","to":"Idle","blendSeconds":0.2,"exitTime":null,"reenter":false,"conditions":[{"parameter":"aim","op":"eq","value":false}]}]}]})");
    graph["layers"][1]["mask"]=crude_json::array{bind.joints[selected].name};
    for(auto& transition:graph["layers"][1]["transitions"].get<crude_json::array>())transition["blendSeconds"]=double(2*step);
    check(first->BindAnimationController(graph.dump()) && peer->BindAnimationController(graph.dump()),"Actual multi-layer/mask/additive bind failed");
    check(first->SetControllerPlaybackRate(0.5f),"Layered multiplier failed");first->Update(step);first->Update(step);
    check(std::abs(*first->GetControllerLayerTime(0)-step)<1e-5f && std::abs(*first->GetControllerLayerTime(1)-2*step)<1e-5f && *peer->GetControllerLayerTime(1)==0,"Layer clocks/multiplier/peer isolation failed");
    Animation identity{};std::vector<QuaternionTransform> reference,base,upper,expected;
    BoneMask mask;check(SampleLocalPose(bind,identity,0,reference) && ResolveBoneMask(bind,std::vector<std::string>{bind.joints[selected].name},mask),"Expected layered bind/mask failed");
    const auto compare=[&](const std::vector<QuaternionTransform>& pose){for(size_t i=0;i<pose.size();++i){QuaternionTransform actual;
        check(first->GetAnimationJointTransform(i,actual),"Layered joint unavailable");
        check(std::abs(actual.translate.x-pose[i].translate.x)<1e-4f && std::abs(actual.translate.y-pose[i].translate.y)<1e-4f && std::abs(actual.translate.z-pose[i].translate.z)<1e-4f && std::abs(actual.scale.x-pose[i].scale.x)<1e-4f,"Actual layered local TRS/mask differs from composer");
        const double dot=double(actual.rotate.x)*pose[i].rotate.x+double(actual.rotate.y)*pose[i].rotate.y+double(actual.rotate.z)*pose[i].rotate.z+double(actual.rotate.w)*pose[i].rotate.w;
        check(std::abs(std::abs(dot)-1)<1e-4,"Actual layered rotation differs from composer");
    }};
    check(SampleLocalPose(bind,*clip,0.5f*step,base) && SampleLocalPose(bind,*clip,step,upper) && ComposeMaskedLayer(base,upper,reference,mask,PoseLayerMode::Additive,0.5f,expected),"Expected layered actual pose failed");compare(expected);
    check(first->SetControllerPlaybackRate(0) && first->SetControllerBool("aim",true),"Layered pause/parameter failed");first->Update(step);compare(expected);
    check(first->GetControllerState(1)=="Idle" && *first->GetControllerLayerTime(1)==2*step,"Paused layer transitioned/advanced");
    check(first->SetControllerPlaybackRate(0.5f),"Layered resume failed");first->Update(step);
    check(first->GetControllerState(0)=="Idle" && first->GetControllerState(1)=="Aim" && first->IsAnimationBlending() && std::abs(*first->GetControllerLayerTime(1)-1.5f*step)<1e-5f && peer->GetControllerState(1)=="Idle","Overlay transition affected base/peer or lost target speed");
    check(SampleLocalPose(bind,*clip,step,base) && SampleBlendedLocalPose(bind,*clip,3*step,*clip,1.5f*step,0.5f,upper) && ComposeMaskedLayer(base,upper,reference,mask,PoseLayerMode::Additive,0.5f,expected),"Expected overlay blend failed");compare(expected);
    const auto snapshot=upper;
    check(first->SetControllerBool("aim",false),"Overlay interruption parameter failed");first->Update(step);
    std::vector<QuaternionTransform> target;
    check(SampleLocalPose(bind,*clip,1.5f*step,base) && SampleLocalPose(bind,*clip,step,target) && BlendLocalPoses(snapshot,target,0.5f,upper) && ComposeMaskedLayer(base,upper,reference,mask,PoseLayerMode::Additive,0.5f,expected),"Expected interrupted overlay snapshot failed");compare(expected);
    check(first->GetControllerState(1)=="Idle" && first->IsAnimationBlending(),"Interrupted layer did not admit new state");
    const auto held=*first->GetControllerLayerTime(1);auto bad=graph;bad["layers"][1]["mask"]=crude_json::array{"missing-real-model-joint"};
    check(!first->BindAnimationController(bad.dump()) && *first->GetControllerLayerTime(1)==held && first->GetControllerState(1)=="Idle","Bad mask rebind changed live layers");
    check(first->SetControllerLayerWeight(1,0) && !first->SetControllerLayerWeight(1,2) && !first->SetControllerLayerWeight(9,1) && !first->GetControllerLayerTime(9),"Layer weight/index bounds failed");first->Update(0);compare(base);
    first->ResetAnimationTime();check(!first->IsAnimationBlending() && *first->GetControllerLayerTime(0)==0 && *first->GetControllerLayerTime(1)==0 && *first->GetControllerLayerWeight(1)==0,"Layer reset changed weight or retained clocks/blends");
    first->SetModel(model);check(first->GetControllerState(1).empty() && !first->GetControllerLayerWeight(1),"Model change retained layered owner");
    auto four=graph;four["layers"]=crude_json::array{};
    for(size_t layer=0;layer<4;++layer){auto definition=graph["layers"][0];
        definition["name"]="Layer"+std::to_string(layer);definition["weight"]=double(layer==0?1:0.25);
        definition["mode"]=layer%2?"additive":"override";
        definition["states"][0]["speed"]=double(layer+1);
        if(layer)definition["mask"]=crude_json::array{bind.joints[selected].name};
        four["layers"].get<crude_json::array>().push_back(std::move(definition));
    }
    check(first->BindAnimationController(four.dump()),"Actual four-layer bind failed");
    first->Update(step);first->Update(step);expected=reference;
    BoneMask all;check(ResolveBoneMask(bind,std::vector<std::string>{},all),"All-bone mask failed");
    for(size_t layer=0;layer<4;++layer){
        check(first->GetControllerState(layer)=="Idle" && std::abs(*first->GetControllerLayerTime(layer)-2*step*float(layer+1))<1e-5f,"Four-layer clock/state failed");
        check(SampleLocalPose(bind,*clip,step*float(layer+1),upper) && ComposeMaskedLayer(expected,upper,reference,layer?mask:all,layer%2?PoseLayerMode::Additive:PoseLayerMode::Override,layer?0.25f:1,expected),"Four-layer expected composition failed");
    }
    compare(expected);check(!first->GetControllerLayerTime(4),"Fifth layer query accepted");
    auto single=graph;single["layers"]=crude_json::array{graph["layers"][0]};
    check(first->BindAnimationController(single.dump()),"Canonical single layer bind failed");
    first->Update(step);const auto singleTime=*first->GetControllerLayerTime(0);
    check(first->SetControllerLayerWeight(0,0.5f) && *first->GetControllerLayerTime(0)==singleTime,"Legacy weight conversion reset clock");
    first->Update(step);
    check(SampleLocalPose(bind,*clip,singleTime,base) && ComposeMaskedLayer(reference,base,reference,all,PoseLayerMode::Override,0.5f,expected),"Legacy converted pose expectation failed");compare(expected);
    single["layers"]=crude_json::array{graph["layers"][1]};single["layers"][0]["mode"]="override";
    single["layers"][0]["weight"]=1.0;single["layers"][0]["mask"]=crude_json::array{};
    check(first->BindAnimationController(single.dump()) && first->SetControllerBool("aim",true),"Legacy blend setup failed");
    first->Update(step);check(first->IsAnimationBlending(),"Legacy transition blend absent");
    check(SampleBlendedLocalPose(bind,*clip,2*step,*clip,3*step,0.5f,base),"Legacy blend expectation failed");
    const auto blendTime=*first->GetControllerLayerTime(0);
    check(first->SetControllerLayerWeight(0,0.5f) && first->IsAnimationBlending() && *first->GetControllerLayerTime(0)==blendTime,"Legacy weight conversion lost blend/clock");
    first->Update(0);check(ComposeMaskedLayer(reference,base,reference,all,PoseLayerMode::Override,0.5f,expected),"Converted blend composition failed");compare(expected);
    std::cout<<"PASS: actual Object3D four-layer ordered pose/independent clocks and legacy weight conversion clock/blend preservation\n";
    single["layers"][0]["transitions"]=crude_json::array{};
    check(first->BindAnimationController(single.dump()),"Canonical force-state setup failed");first->Update(step);first->Update(step);
    check(first->GetControllerStateNames(0)==std::vector<std::string>{"Idle","Aim"} && first->GetControllerStateNames(4).empty(),"Force-state authored catalog failed");
    const auto outgoingTime=*first->GetControllerLayerTime(0);
    check(first->ForceControllerState(0,"Aim",2*step) && first->GetControllerState()=="Aim" && *first->GetControllerLayerTime(0)==0 && first->IsAnimationBlending(),"Canonical force admission/reset failed");
    check(SampleLocalPose(bind,*clip,outgoingTime,expected),"Expected forced source failed");compare(expected);
    first->Update(step);
    check(SampleBlendedLocalPose(bind,*clip,outgoingTime+2*step,*clip,3*step,0.5f,expected),"Expected forced crossfade failed");compare(expected);
    const auto forcedSnapshot=expected;
    check(first->SetControllerPlaybackRate(0) && first->ForceControllerState(0,"Idle",2*step),"Paused force interruption refused");compare(forcedSnapshot);
    first->Update(step);compare(forcedSnapshot);
    check(first->SetControllerPlaybackRate(1),"Forced resume refused");first->Update(step);
    check(SampleLocalPose(bind,*clip,2*step,target) && BlendLocalPoses(forcedSnapshot,target,0.5f,expected),"Expected forced interrupted snapshot failed");compare(expected);
    const auto refusedTime=*first->GetControllerLayerTime(0);
    check(!first->ForceControllerState(0,"missing",0) && !first->ForceControllerState(9,"Idle",0) && !first->ForceControllerState(0,"Aim",-1) && *first->GetControllerLayerTime(0)==refusedTime,"Invalid forced admission changed live owner");compare(expected);
    check(first->ForceControllerState(0,"Idle",0) && !first->IsAnimationBlending() && *first->GetControllerLayerTime(0)==0 && SampleLocalPose(bind,*clip,0,expected),"Instant force reentry failed");compare(expected);
    std::cout<<"PASS: actual Object3D force-state canonical conversion/catalog/real crossfade/paused interrupted snapshot/resume/instant reentry/refusal pose retention\n";
    std::cout<<"PASS: actual Object3D layered Model/rig admission, masked additive TRS/palette, independent clocks/state/peer, multiplier/Pause, overlay blend/interruption, weights/bounds/reset/missing-mask preservation/model detach\n";
}
