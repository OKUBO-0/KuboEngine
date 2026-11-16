#include "engine/animation/LayeredControllerPlayer.h"
#include <iostream>
#include <stdexcept>
static void Need(bool v,const char* why){if(!v)throw std::runtime_error(why);}
static bool Near(float a,float b){return std::abs(a-b)<0.00001f;}
int main(){try{
    using namespace Engine::AnimationSystem;using namespace Controller;
    const char* text=R"({"format":"kubo-animator-controller-1","parameters":[{"name":"go","type":"trigger","default":false}],"layers":[{"name":"Base","mode":"override","weight":1,"mask":[],"entry":"Idle","states":[{"name":"Idle","clip":"move","speed":1,"loop":true},{"name":"Run","clip":"move","speed":3,"loop":true}],"transitions":[{"from":"Idle","to":"Run","blendSeconds":0.2,"exitTime":null,"reenter":false,"conditions":[{"parameter":"go","op":"set","value":true}]}]},{"name":"Upper","mode":"additive","weight":0.5,"mask":["Hand"],"entry":"UpperIdle","states":[{"name":"UpperIdle","clip":"overlay","speed":2,"loop":true},{"name":"UpperRun","clip":"overlay","speed":4,"loop":true}],"transitions":[{"from":"UpperIdle","to":"UpperRun","blendSeconds":0.1,"exitTime":null,"reenter":false,"conditions":[{"parameter":"go","op":"set","value":true}]}]}]})";
    auto runtime=Runtime::FromText(text);Need(bool(runtime),"Layered runtime decode failed");
    Skeleton bind{};Joint root{},hand{};root.name="Rig::Root";root.transform={{1,1,1},{0,0,0,1},{0,0,0}};root.index=0;
    hand=root;hand.name="Rig::Hand";hand.index=1;hand.parent=0;bind.joints={root,hand};
    Animation move{},overlay{};move.duration=overlay.duration=2;
    move.nodeAnimations["Root"].translate={{0,{0,0,0}},{2,{2,0,0}}};move.nodeAnimations["Hand"].translate={{0,{0,0,0}},{2,{4,0,0}}};
    overlay.nodeAnimations["Hand"].translate={{0,{1,0,0}},{2,{3,0,0}}};
    bool missing=false;const auto lookup=[&](const std::string& name)->const Animation*{if(name=="move")return &move;if(name=="overlay" && !missing)return &overlay;return nullptr;};
    auto player=LayeredPlayer::Prepare(*runtime,bind,lookup);Need(bool(player),"Layered clip/rig admission failed");
    std::unique_ptr<Runtime> nextRuntime;std::unique_ptr<LayeredPlayer> nextPlayer;std::vector<QuaternionTransform> pose;
    const auto advance=[&](float dt,float rate){Need(player->PrepareFrame(*runtime,dt,rate,lookup,nextRuntime,nextPlayer,pose),"Layered prepare failed");runtime.swap(nextRuntime);player.swap(nextPlayer);};
    advance(0.1f,1);Need(Near(player->GetClock(0)->time,0.1f) && Near(player->GetClock(1)->time,0.2f) && Near(pose[1].translate.x,0.5f),"Independent actual clocks/additive entry composition failed");
    advance(0.1f,1);Need(Near(pose[0].translate.x,0.1f) && Near(pose[1].translate.x,0.8f),"Ordered actual sampled pose/mask failed");
    const auto frozen=pose;advance(0.5f,0);
    Need(Near(player->GetClock(0)->time,0.2f) && Near(player->GetClock(1)->time,0.4f) && pose[1].translate.x==frozen[1].translate.x,"Pause advanced clocks or raw layer pose");
    Need(runtime->SetTrigger("go"),"Layer trigger set failed");missing=true;
    auto* heldRuntime=nextRuntime.get();auto* heldPlayer=nextPlayer.get();const auto* heldPose=pose.data();
    Need(!player->PrepareFrame(*runtime,0.1f,1,lookup,nextRuntime,nextPlayer,pose) && nextRuntime.get()==heldRuntime && nextPlayer.get()==heldPlayer && pose.data()==heldPose && runtime->ActiveState()->name=="Idle","Late layer failure partially published outputs/state");
    missing=false;advance(0.1f,1);
    Need(runtime->ActiveState()->name=="Run" && runtime->ActiveState(1)->name=="UpperIdle" && player->GetClock(0)->blending && Near(player->GetClock(0)->time,0.3f) && Near(player->GetClock(1)->time,0.6f),"Layer transition/trigger precedence/failure trigger retention failed");
    const auto blendingPose=pose;advance(0,1);Need(pose[1].translate.x==blendingPose[1].translate.x && player->IsBlending(),"Zero delta changed active blend");
    Need(player->SetWeight(1,0) && !player->SetWeight(4,1) && !player->SetWeight(1,2),"Runtime layer weight bounds failed");
    advance(0.1f,1);Need(!player->GetClock(0)->blending && Near(player->GetClock(0)->time,0.6f) && Near(player->GetClock(1)->time,0.8f),"Layer blend completion or zero-weight clock failed");
    Need(Near(pose[1].translate.x,1.2f),"Zero weight did not remove overlay");
    player->Reset();advance(0,1);Need(runtime->ActiveState()->name=="Run" && player->GetClock(0)->time==0 && player->GetClock(1)->time==0 && !player->IsBlending(),"Reset changed runtime state or retained clocks/blend");
    auto badGraph=crude_json::value::parse(text);badGraph["layers"][1]["mask"]=crude_json::array{"missing"};auto badRuntime=Runtime::FromText(badGraph.dump());
    Need(badRuntime && !LayeredPlayer::Prepare(*badRuntime,bind,lookup),"Unknown model mask was admitted");
    runtime=Runtime::FromText(text);player=LayeredPlayer::Prepare(*runtime,bind,lookup);advance(0.1f,1);advance(0.1f,1);
    const auto force=[&](size_t layer,const char* name,float duration,float rate){
        Need(player->PrepareForcedFrame(*runtime,layer,name,duration,rate,lookup,nextRuntime,nextPlayer,pose),"Forced layer candidate failed");runtime.swap(nextRuntime);player.swap(nextPlayer);
    };
    force(1,"UpperRun",0.2f,1);
    Need(runtime->ActiveState()->name=="Idle" && runtime->ActiveState(1)->name=="UpperRun" && Near(player->GetClock(0)->time,0.2f) && player->GetClock(1)->time==0 && player->IsBlending(),"Force affected peer layer clock/state or failed target reset");
    advance(0.1f,1);const auto forceBlendPose=pose;
    force(1,"UpperIdle",0.2f,0);Need(pose[1].translate.x==forceBlendPose[1].translate.x,"Paused force interruption changed source snapshot");
    const auto heldBase=player->GetClock(0)->time;advance(0.1f,0);
    Need(pose[1].translate.x==forceBlendPose[1].translate.x && player->GetClock(1)->time==0 && player->GetClock(0)->time==heldBase,"Forced paused blend advanced");
    advance(0.1f,1);Need(player->IsBlending() && Near(player->GetClock(1)->time,0.2f),"Forced blend resume failed");
    Need(runtime->SetTrigger("go"),"Force trigger retention setup failed");force(1,"UpperIdle",0,1);
    Need(!player->IsBlending() && player->GetClock(1)->time==0 && player->GetClock(0)->time>0,"Forced same-state instant reentry failed");
    advance(0.1f,1);Need(runtime->ActiveState()->name=="Run","Forced transition consumed a pending trigger");
    heldRuntime=nextRuntime.get();heldPlayer=nextPlayer.get();heldPose=pose.data();missing=true;
    Need(!player->PrepareForcedFrame(*runtime,1,"UpperRun",0.2f,1,lookup,nextRuntime,nextPlayer,pose) && nextRuntime.get()==heldRuntime && nextPlayer.get()==heldPlayer && pose.data()==heldPose && runtime->ActiveState(1)->name=="UpperIdle","Failed force changed live/outputs");missing=false;
    Need(!player->PrepareForcedFrame(*runtime,4,"Idle",0,1,lookup,nextRuntime,nextPlayer,pose) && !player->PrepareForcedFrame(*runtime,0,"missing",0,1,lookup,nextRuntime,nextPlayer,pose),"Force invalid layer/name admitted");
    missing=true;heldRuntime=nextRuntime.get();heldPlayer=nextPlayer.get();heldPose=pose.data();const auto stateBeforeLateForce=runtime->ActiveState()->name;
    Need(!player->PrepareForcedFrame(*runtime,0,"Idle",0,1,lookup,nextRuntime,nextPlayer,pose) && nextRuntime.get()==heldRuntime && nextPlayer.get()==heldPlayer && pose.data()==heldPose && runtime->ActiveState()->name==stateBeforeLateForce,"Late other-layer force failure published candidate outputs/state");missing=false;
    Need(!player->PrepareForcedFrame(*runtime,0,"Idle",0,std::numeric_limits<float>::quiet_NaN(),lookup,nextRuntime,nextPlayer,pose),"Force invalid rate admitted");
    std::cout<<"PASS: layered force-state private pose candidate/target reset/peer clocks/interruption/Pause+resume/instant reentry/trigger retention/refusal isolation\n";
    std::cout<<"PASS: production layered player actual clip clocks/ordered masked additive/atomic frame candidate/late failure+trigger retention/authored trigger precedence/Pause/blend/weight0/reset/rig admission (Object3D probe separate)\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
