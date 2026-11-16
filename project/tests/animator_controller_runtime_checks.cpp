#include "engine/animation/AnimatorControllerRuntime.h"
#include "engine/3d/model/AnimationTime.h"
#include <iostream>
#include <stdexcept>
static void Require(bool v,const char* m){if(!v)throw std::runtime_error(m);}
int main(){try {
    using namespace Engine::AnimationSystem::Controller;
    const std::string text=R"({"format":"kubo-animator-controller-1","parameters":[{"name":"speed","type":"float","default":0},{"name":"ready","type":"bool","default":true},{"name":"jump","type":"trigger","default":false}],"layers":[{"name":"Base","mode":"override","weight":1,"mask":[],"entry":"Idle","states":[{"name":"Idle","clip":"idle","speed":1,"loop":true},{"name":"Run","clip":"run","speed":2,"loop":true}],"transitions":[{"from":"Idle","to":"Run","blendSeconds":0.25,"exitTime":null,"reenter":false,"conditions":[{"parameter":"speed","op":"gt","value":0.5},{"parameter":"ready","op":"eq","value":true},{"parameter":"jump","op":"set","value":true}]},{"from":"Run","to":"Idle","blendSeconds":0.25,"exitTime":0.75,"reenter":false,"conditions":[]}]}]})";
    auto first=Runtime::FromText(text),second=Runtime::FromText(text);
    Require(first && second && !Runtime::FromText("{}"),"Runtime factory validation failed");
    Require(!first->SetFloat("ready",1) && !first->SetBool("jump",true) && !first->SetTrigger("missing") && !first->SetFloat("speed",std::numeric_limits<double>::infinity()),"Typed setters accepted invalid write");
    Require(first->SetFloat("speed",1) && first->SetTrigger("jump"),"Typed writes failed");
    Require(!first->Advance(0,0,0.1,[](const auto&,const auto&){return false;}) && first->ActiveState()->name=="Idle","Rejected admission changed state");
    bool threw=false;try {first->Advance(0,0,0.1,[](const auto&,const auto&)->bool {throw std::runtime_error("owner refusal");});}catch(const std::runtime_error&){threw=true;}
    Require(threw && first->ActiveState()->name=="Idle","Admission exception committed state");
    Require(first->Advance(0,0,0.1,[&](const State& state,const Transition&){Require(state.name=="Run" && !first->SetFloat("speed",0),"Reentrant parameter write admitted");return true;}) && first->ActiveState()->name=="Run" && second->ActiveState()->name=="Idle","Admitted trigger transition/instance isolation failed");
    Require(!first->Advance(0,0.8,1.2,[](const auto&,const auto&){return true;}),"Loop exit fired before next threshold");
    Require(first->Advance(0,1.6,3.2,[](const auto&,const auto&){return true;}) && first->ActiveState()->name=="Idle","Large unwrapped interval missed threshold");
    Require(!first->Advance(0,0,0.1,[](const auto&,const auto&){return true;}),"Committed trigger was not consumed");
    Require(first->SetTrigger("jump") && first->SetBool("ready",false) && !first->Advance(0,0,0.1,[](const auto&,const auto&){return true;}),"Bool mismatch ignored");
    Require(first->SetBool("ready",true) && first->Advance(0,0,0.1,[](const auto&,const auto&){return true;}),"Condition mismatch consumed trigger");
    Require(!first->Advance(0,1,1,[](const auto&,const auto&){return true;}) && !first->Advance(0,1,0,[](const auto&,const auto&){return true;}) && !first->Advance(4,0,1,[](const auto&,const auto&){return true;}),"Invalid interval/layer admitted");
    auto cycle=crude_json::value::parse(text);for(auto& t:cycle["layers"][0]["transitions"].get<crude_json::array>()){t["conditions"]=crude_json::array{};t["exitTime"]=crude_json::null{};}
    cycle["layers"][0]["transitions"][1]["from"]=crude_json::null{};cycle["layers"][0]["transitions"][1]["reenter"]=true;
    auto bounded=Runtime::FromText(cycle.dump());Require(bounded && bounded->Advance(0,0,1,[](const auto&,const auto&){return true;}) && bounded->ActiveState()->name=="Run","Cycle evaluated more than one transition");
    auto stationaryGraph=crude_json::value::parse(text);
    auto& stationaryTransition=stationaryGraph["layers"][0]["transitions"][0];
    stationaryTransition["exitTime"]=0.75;
    stationaryTransition["conditions"]=crude_json::array{};
    for(bool loop : {false,true}) {
        stationaryGraph["layers"][0]["states"][0]["loop"]=loop;
        auto stationary=Runtime::FromText(stationaryGraph.dump());
        const auto paused=Engine::Graphics3D::AdvanceControllerNormalizedTime(0,0,0,1,0,loop);
        Require(stationary && !stationary->Advance(0,0,paused,[](const auto&,const auto&){return true;}),"Paused stationary exit fired");
        const auto completed=Engine::Graphics3D::AdvanceControllerNormalizedTime(0,0,0,0.01f,1,loop);
        Require(!stationary->Advance(0,0,completed,[](const auto&,const auto&){return false;}) && stationary->ActiveState()->name=="Idle","Refused stationary transition changed state");
        Require(stationary->Advance(0,0,completed,[](const auto&,const auto&){return true;}) && stationary->ActiveState()->name=="Run","Stationary completion missed exit threshold");
        Require(!stationary->Advance(0,completed,completed,[](const auto&,const auto&){return true;}),"Stationary completion repeated without a new interval");
    }
    std::cout<<"CONTROLLER_ZERO_DURATION_COMPLETION=1 PAUSE_REFUSAL=1 NO_SYNTHETIC_CYCLES=1\n";
    auto forced=Runtime::FromText(text);
    const auto admit=[](const auto&,const auto&){return true;};
    Require(!forced->ForceState(0,std::string(129,'x'),0,admit) && !forced->ForceState(0,std::string("Run\0",4),0,admit) && !forced->ForceState(0,std::string("\xff",1),0,admit),"Force-state invalid text admitted");
    Require(!forced->ForceState(4,"Run",0,admit) && !forced->ForceState(0,"missing",0,admit) && !forced->ForceState(0,"Run",-1,admit) && !forced->ForceState(0,"Run",61,admit) && !forced->ForceState(0,"Run",std::numeric_limits<float>::quiet_NaN(),admit),"Force-state bounds refused incorrectly");
    Require(forced->SetFloat("speed",1) && forced->SetTrigger("jump"),"Force fixture params failed");
    Require(!forced->ForceState(0,"Run",0.2f,[](const auto&,const auto&){return false;}) && forced->ActiveState()->name=="Idle","Refused force-state changed active state");
    threw=false;try{forced->ForceState(0,"Run",0.2f,[](const auto&,const auto&)->bool{throw std::runtime_error("forced refusal");});}catch(const std::runtime_error&){threw=true;}
    Require(threw && forced->ActiveState()->name=="Idle","Force exception changed active state");
    Require(forced->ForceState(0,"Run",0.2f,[&](const auto&,const Transition& t){
        Require(t.reenter && t.conditions.empty() && !t.exitTime && t.blendSeconds==0.2f && !forced->SetBool("ready",false) && !forced->Clone() && !forced->ForceState(0,"Idle",0,admit),"Force-state reentrancy/synthetic transition failed");return true;
    }),"Explicit force-state refused");
    Require(forced->ForceState(0,"Run",0,admit) && forced->ForceState(0,"Idle",0,admit) && forced->Advance(0,0,0.1,admit),"Force reentry/parameter+trigger retention failed");
    std::cout<<"PASS: explicit Controller force-state bounds/reentry/condition bypass/parameter-trigger preservation/admission refusal+exception/reentrancy\n";
    std::cout<<"PASS: production Controller runtime typed writes/immutable graph/instance state/priority/threshold crossing/admission refusal+exception/trigger consumption/reentrancy (CPU clock supplied by caller)\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
