#include "externals/imgui_node_editor/crude_json.h"
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>

namespace {bool trackJsonAllocations=false;size_t jsonAllocations=0,failJsonAllocation=0;}
void* operator new(size_t bytes){
    if(trackJsonAllocations && ++jsonAllocations==failJsonAllocation){trackJsonAllocations=false;failJsonAllocation=0;throw std::bad_alloc();}
    if(auto* memory=std::malloc(bytes?bytes:1))return memory;throw std::bad_alloc();
}
void* operator new[](size_t bytes){return ::operator new(bytes);}
void operator delete(void* memory) noexcept{std::free(memory);}
void operator delete[](void* memory) noexcept{std::free(memory);}
void operator delete(void* memory,size_t) noexcept{std::free(memory);}
void operator delete[](void* memory,size_t) noexcept{std::free(memory);}
namespace {
using crude_json::value;using crude_json::type_t;
void Require(bool condition,const char* why){if(!condition)throw std::runtime_error(why);}
void Arm(size_t failAt=0){jsonAllocations=0;failJsonAllocation=failAt;trackJsonAllocations=true;}
size_t Stop(){trackJsonAllocations=false;failJsonAllocation=0;return jsonAllocations;}
value Make(type_t type){
    value result(type);
    if(type==type_t::object){result["payload"]=std::string(4096,'x');result["nested"]["日本語"]=7.0;}
    else if(type==type_t::array){result.push_back(std::string(4096,'a'));result.push_back(true);}
    else if(type==type_t::string)result=std::string(4096,'s');
    else if(type==type_t::number)result=3.0;
    else if(type==type_t::boolean)result=true;
    return result;
}
void CheckAlive(value& item){
    const auto bytes=item.dump();Require(value::parse(bytes).dump()==bytes,"JSON exception left invalid value");
    item=42.0;Require(item.is_number() && item.get<crude_json::number>()==42.0,"JSON failed value cannot be reassigned");
}
}
int main(){try {
    size_t failures=0;const auto source=Make(type_t::object);const auto sourceBytes=source.dump();
    Arm();const auto dumped=source.dump();const auto dumpAllocations=Stop();Require(dumped==sourceBytes && dumpAllocations>0,"Dump allocation probe inactive");
    for(size_t failAt=1;failAt<=dumpAllocations;++failAt){
        bool threw=false;Arm(failAt);try{const auto incomplete=source.dump();(void)incomplete;}catch(const std::exception&){threw=true;}Stop();
        Require(threw && source.dump()==sourceBytes,"Dump swallowed allocation failure/returned partial JSON/changed source");++failures;
    }
    const type_t types[]={type_t::null,type_t::object,type_t::array,type_t::string,type_t::number,type_t::boolean};
    for(const auto leftType:types)for(const auto rightType:types){
        auto probe=Make(leftType);const auto right=Make(rightType);const auto expected=right.dump();
        Arm();probe=right;const auto assignmentAllocations=Stop();Require(probe.dump()==expected,"Assignment probe wrong value");
        for(size_t failAt=1;failAt<=assignmentAllocations;++failAt){
            auto left=Make(leftType);bool threw=false;Arm(failAt);try{left=right;}catch(const std::exception&){threw=true;}Stop();
            Require(threw && right.dump()==expected,"Assignment failure changed const source/did not throw");CheckAlive(left);++failures;
        }
        auto swapLeft=Make(leftType),swapRight=Make(rightType);const auto expectedLeft=swapRight.dump(),expectedRight=swapLeft.dump();
        Arm();swapLeft.swap(swapRight);const auto swapAllocations=Stop();Require(swapLeft.dump()==expectedLeft && swapRight.dump()==expectedRight,"Swap probe wrong values");
        for(size_t failAt=1;failAt<=swapAllocations;++failAt){
            auto left=Make(leftType),other=Make(rightType);bool threw=false;Arm(failAt);try{left.swap(other);}catch(const std::exception&){threw=true;}Stop();
            Require(threw,"Swap failure injection inactive");CheckAlive(left);CheckAlive(other);++failures;
        }
    }
    Require(failures>0,"JSON allocation failures not exercised");
    std::cout<<"PASS: "<<failures<<" JSON allocation failures across throwing complete dump, all six-type copy assignment and mixed swap; valid lifetimes/reassignment and const source preservation (mixed swap basic, not strong guarantee)\n";return 0;
}catch(const std::exception& exception){trackJsonAllocations=false;std::cerr<<"FAIL: "<<exception.what()<<"\n";return 1;}}
