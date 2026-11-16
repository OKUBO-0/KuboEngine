#include "animator_controller_candidate.h"
#include "animator_controller_evaluation_candidate.h"
#include "animator_controller_text_candidate.h"
#include <iostream>
#include <cstdlib>
#include <new>
namespace {long failAfter=-1;bool count=false;size_t allocations=0;
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}}
void* operator new(size_t n){if(count)++allocations;if(failAfter==0){failAfter=-1;throw std::bad_alloc();}if(failAfter>0)--failAfter;if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept {std::free(p);} void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,size_t) noexcept {std::free(p);} void operator delete[](void* p,size_t) noexcept {std::free(p);}
int main(){try {
    using namespace AnimatorControllerCandidate;using V=crude_json::value;
    const auto original=V::parse(R"({"format":"kubo-controller-candidate-1","parameters":[{"name":"speed","type":"float","default":0},{"name":"grounded","type":"bool","default":true},{"name":"jump","type":"trigger","default":false}],"layers":[{"name":"Base","mode":"override","weight":1,"mask":[],"entry":"Idle","states":[{"name":"Idle","clip":"idle","speed":1,"loop":true},{"name":"走る","clip":"walk","speed":2,"loop":false}],"transitions":[{"from":"Idle","to":"走る","blendSeconds":0.2,"exitTime":null,"reenter":false,"conditions":[{"parameter":"speed","op":"gt","value":0.5},{"parameter":"grounded","op":"eq","value":true},{"parameter":"jump","op":"set","value":true}]}]}]})");
    Graph output;std::string error;Require(Codec::Decode(original,output,error),error.c_str());
    Graph loaded;Require(TextCodec::Decode(original.dump(),loaded,error) && loaded.layers[0].states[1].name=="走る","Controller UTF-8 text decode failed");
    const auto textRetained=loaded.layers[0].states.data();size_t textRefusals=0;
    const auto refuseText=[&](const std::string& text){Require(!TextCodec::Decode(text,loaded,error) && loaded.layers[0].states.data()==textRetained,"Text refusal changed graph");++textRefusals;};
    const auto validText=original.dump();
    refuseText("{\"format\":\"kubo-controller-candidate-1\","+validText.substr(1));
    refuseText("{\"\\u0066ormat\":\"kubo-controller-candidate-1\","+validText.substr(1));
    refuseText("{\"format\":1,\"format\":2}");
    refuseText(R"({"format":1,"\u0066ormat":2})");
    refuseText(R"({"name":"\ud800"})");
    refuseText(R"({"name":"\u0000"})");
    refuseText(original.dump()+" true");
    refuseText("{\"number\":01}");refuseText("{\"number\":1.}");refuseText("{\"number\":1e}");
    refuseText("{\"items\":[1,]}");refuseText(std::string(1,static_cast<char>(0xff)));
    refuseText(std::string(TextCodec::MaxBytes+1,' '));
    refuseText(std::string(TextCodec::MaxDepth+2,'[')+"0"+std::string(TextCodec::MaxDepth+2,']'));
    auto many=std::string("[");for(size_t i=0;i<TextCodec::MaxNodes;++i){if(i)many+=',';many+='0';}many+=']';refuseText(many);
    Graph byteBoundary;Require(TextCodec::Decode(validText+std::string(TextCodec::MaxBytes-validText.size(),' '),byteBoundary,error),"Exact byte boundary refused valid graph");
    std::cout<<"PASS: private controller text gate UTF-8/bytes/depth/nodes/decoded duplicate keys/grammar/trailing-data, "<<textRefusals<<" refusals preserve graph (NOT file/asset loader)\n";
    Require(output.parameters.size()==3 && output.layers.size()==1 && output.layers[0].entry==0 && output.layers[0].transitions[0].to==1 && output.layers[0].states[1].name=="走る" && output.layers[0].transitions[0].conditions[2].parameter==2,"Typed reference resolution failed");
    const auto retained=output.layers[0].states.data();size_t negatives=0;
    const auto refuse=[&](const V& value){Require(!Codec::Decode(value,output,error) && output.layers[0].states.data()==retained && output.layers[0].states[0].clip=="idle","Malformed controller changed output");++negatives;};
    auto v=original;v["format"]="future";refuse(v);
    v=original;v["runtimeState"]="Idle";refuse(v);
    v=original;v["parameters"][1]["name"]="speed";refuse(v);
    v=original;v["parameters"][2]["default"]=true;refuse(v);
    v=original;v["parameters"][0]["type"]="integer";refuse(v);
    v=original;v["parameters"][0]["default"]=std::numeric_limits<double>::infinity();refuse(v);
    v=original;v["layers"][0]["states"][1]["name"]="Idle";refuse(v);
    v=original;v["layers"][0]["states"][0]["clip"]=std::string(257,'x');refuse(v);
    v=original;v["layers"][0]["states"][0]["clip"]=std::string(1,static_cast<char>(0xff));refuse(v);
    v=original;v["layers"][0]["entry"]="Missing";refuse(v);
    v=original;v["layers"][0]["mode"]="unknown";refuse(v);
    v=original;v["layers"][0]["weight"]=1.1;refuse(v);
    v=original;v["layers"][0]["transitions"][0]["to"]="Missing";refuse(v);
    v=original;v["layers"][0]["transitions"][0]["to"]="Idle";refuse(v);
    v=original;v["layers"][0]["transitions"][0]["exitTime"]=1.1;refuse(v);
    v=original;v["layers"][0]["transitions"][0]["blendSeconds"]=-1.0;refuse(v);
    v=original;v["layers"][0]["transitions"][0]["conditions"][0]["parameter"]="Missing";refuse(v);
    v=original;v["layers"][0]["transitions"][0]["conditions"][1]["value"]=1.0;refuse(v);
    v=original;v["layers"][0]["transitions"][0]["conditions"][2]["value"]=false;refuse(v);
    v=original;v["layers"][0]["transitions"][0]["conditions"][0]["op"]="set";refuse(v);
    v=original;v["layers"][0]["mask"]=crude_json::array{"Root","Root"};refuse(v);
    v=original;auto layer=v["layers"][0];for(int i=1;i<5;++i){layer["name"]=std::to_string(i);v["layers"].push_back(layer);}refuse(v);
    v=original;auto parameter=v["parameters"][0];for(int i=3;i<33;++i){parameter["name"]=std::to_string(i);v["parameters"].push_back(parameter);}refuse(v);
    Graph changed;v=original;v["layers"][0]["transitions"][0]["from"]=crude_json::null{};Require(Codec::Decode(v,changed,error) && !changed.layers[0].transitions[0].from,"Any-state reference failed");
    v=original;v["layers"][0]["transitions"][0]["to"]="Idle";v["layers"][0]["transitions"][0]["reenter"]=true;Require(Codec::Decode(v,changed,error),"Explicit self reentry failed");
    auto boundary=original;
    auto p=boundary["parameters"][0];for(int i=3;i<32;++i){p["name"]="p"+std::to_string(i);boundary["parameters"].push_back(p);}
    auto s=boundary["layers"][0]["states"][0];for(int i=2;i<32;++i){s["name"]="s"+std::to_string(i);boundary["layers"][0]["states"].push_back(s);}
    auto transition=boundary["layers"][0]["transitions"][0];auto condition=transition["conditions"][0];while(transition["conditions"].get<crude_json::array>().size()<8)transition["conditions"].push_back(condition);
    boundary["layers"][0]["transitions"]=crude_json::array{};for(int i=0;i<256;++i)boundary["layers"][0]["transitions"].push_back(transition);
    auto upper=boundary["layers"][0];upper["mode"]="additive";upper["weight"]=0.5;upper["mask"]=crude_json::array{};for(int i=0;i<256;++i)upper["mask"].push_back("joint"+std::to_string(i));
    for(int i=1;i<4;++i){upper["name"]="upper"+std::to_string(i);if(i==2)upper["transitions"]=crude_json::array{};boundary["layers"].push_back(upper);}
    Require(Codec::Decode(boundary,changed,error) && changed.parameters.size()==32 && changed.layers.size()==4 && changed.layers[1].mask.size()==256,"Exact controller bounds/additive layer rejected");
    v=boundary;v["layers"][2]["transitions"].push_back(transition);refuse(v);
    v=boundary;s["name"]="extra";v["layers"][2]["states"].push_back(s);refuse(v);
    v=boundary;v["layers"][0]["transitions"].push_back(transition);refuse(v);
    v=boundary;v["layers"][1]["mask"].push_back("extra");refuse(v);
    v=original;while(v["layers"][0]["states"].get<crude_json::array>().size()<65){s["name"]=std::to_string(v["layers"][0]["states"].get<crude_json::array>().size());v["layers"][0]["states"].push_back(s);}refuse(v);
    v=original;for(int i=3;i<9;++i)v["layers"][0]["transitions"][0]["conditions"].push_back(condition);refuse(v);
    allocations=0;count=true;const bool measured=Codec::Decode(original,changed,error);count=false;Require(measured,"Allocation baseline failed");const auto positions=allocations;
    for(size_t i=0;i<positions;++i){failAfter=static_cast<long>(i);const bool decoded=Codec::Decode(original,output,error);failAfter=-1;Require(!decoded && output.layers[0].states.data()==retained,"Allocation failure published controller");}
    auto owned=original;owned["layers"][0]["states"][0]["clip"]=std::string(256,'c');allocations=0;count=true;const bool measuredOwned=Codec::Decode(owned,changed,error);count=false;Require(measuredOwned,"Owned-string allocation baseline failed");const auto ownedPositions=allocations;
    for(size_t i=0;i<ownedPositions;++i){failAfter=static_cast<long>(i);const bool decoded=Codec::Decode(owned,output,error);failAfter=-1;Require(!decoded && output.layers[0].states.data()==retained,"Owned-string failure published controller");}
    Graph decision;Require(Codec::Decode(original,decision,error),"Decision graph decode failed");
    Instance first(decision),second(decision);
    first.values[0].number=1;first.values[2].flag=true;
    Require(Evaluation::Select(decision,first,0,0,0.1)==0,"Matching typed conditions refused");
    Require(!Evaluation::Select(decision,second,0,0,0.1),"Independent instance inherited parameters");
    Require(!Evaluation::Commit(decision,first,0,0,0,0.1,false) && first.active[0]==0 && first.values[2].flag,"Refusal consumed trigger/state");
    first.values[1].flag=false;
    Require(!Evaluation::Commit(decision,first,0,0,0,0.1,true) && first.values[2].flag,"Stale condition committed");
    first.values[1].flag=true;
    Require(Evaluation::Commit(decision,first,0,0,0,0.1,true) && first.active[0]==1 && !first.values[2].flag && second.active[0]==0,"Commit/trigger/instance isolation failed");
    decision.layers[0].transitions[0].conditions.clear();decision.layers[0].transitions[0].exitTime=0.75f;
    Instance timed(decision);
    Require(Evaluation::Select(decision,timed,0,0.6,3.2)==0,"Large delta skipped exit threshold");
    Require(!Evaluation::Select(decision,timed,0,0.8,1.2),"Loop threshold fired before next crossing");
    Require(Evaluation::Select(decision,timed,0,1.6,1.8)==0,"Unwrapped loop threshold missed");
    Require(!Evaluation::Select(decision,timed,0,1.8,1.8) && !Evaluation::Select(decision,timed,0,1.8,0.1) && !Evaluation::Select(decision,timed,0,0,std::numeric_limits<double>::infinity()),"Paused/backwards/nonfinite interval admitted");
    decision.layers[0].states[0].loop=false;
    Require(!Evaluation::Select(decision,timed,0,1.6,1.8),"Non-loop exit repeated");
    auto any=decision.layers[0].transitions[0];any.from.reset();any.exitTime.reset();any.to=0;
    decision.layers[0].transitions.insert(decision.layers[0].transitions.begin(),any);
    Require(Evaluation::Select(decision,timed,0,0.6,0.8)==1,"Any-state self transition bypassed reentry guard");
    decision.layers[0].transitions[0].reenter=true;
    Require(Evaluation::Select(decision,timed,0,0.6,0.8)==0,"Authored priority/reentry failed");
    timed.values.pop_back();Require(!Evaluation::Select(decision,timed,0,0,1),"Schema mismatch admitted");
    std::cout<<"PASS: private controller decision selection/admission/trigger retention/instance isolation/threshold crossing/priority/bounded one-step evaluation (NOT real clock/pose/asset/runtime integration)\n";
    std::cout<<"PASS: private controller parsed-value codec typed parameters/layers/states/conditions/references/exact bounds, "<<negatives<<" invalid refusals and "<<positions<<'+'<<ownedPositions<<" allocation failures preserve output (NOT asset loader/runtime/controller/blend/IK/production integration)\n";
    return 0;
}catch(const std::exception& e){failAfter=-1;count=false;std::cerr<<e.what()<<'\n';return 1;}}
