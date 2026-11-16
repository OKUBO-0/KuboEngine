#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include "engine/script/ScriptSceneCommands.h"
#include <cstdlib>
#include <new>
#include <iostream>
#include <string>
#include <vector>
#include <stdexcept>
namespace {
bool tracking=false;size_t allocations=0;
void Require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
}
void* operator new(size_t n){if(tracking)++allocations;if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,size_t) noexcept{std::free(p);}
void operator delete[](void* p,size_t) noexcept{std::free(p);}
int main(){try{
 using namespace Engine::Scene;using namespace Engine::Scripting;
 const std::vector<std::string> invalid={"",std::string("a\0b",3),std::string(257,'a'),"\xff","\x80","\xc0\xaf","\xc2","\xe0\x80\x80","\xed\xa0\x80","\xf4\x90\x80\x80","\xf5\x80\x80\x80","\xe2\x28\xa1",std::string(255,'a')+"\xc2"};
 std::string twoByteBoundary;for(int i=0;i<128;++i)twoByteBoundary+="\xdf\xbf";
 const std::vector<std::string> valid={"ASCII","日本語","\xc2\x80","\xdf\xbf","\xe0\xa0\x80","\xed\x9f\xbf","\xee\x80\x80","\xf0\x90\x80\x80","\xf4\x8f\xbf\xbf",std::string(256,'a'),twoByteBoundary};
 EntityWorld world;world.CreateEntity("Owner");auto self=*world.Find(1);ScriptSceneView view;
 ScriptSceneView::WriteScope scope(view,world);const auto snapshot=world.Capture().dump();const auto allocator=world.AllocationHighWatermark();
 size_t failures=0;
 for(size_t i=0;i<invalid.size();++i){
  ScriptSceneCommands commands;commands.Begin(&view,self);EntityId commandId=777;
  allocations=0;tracking=true;const auto command=commands.Spawn(invalid[i],0,commandId);tracking=false;const auto commandAllocations=allocations;
  ScriptSceneStructure core;Require(core.Begin(&view)==ScriptSceneStructure::Result::Success,"Core fixture setup failed");EntityId coreId=777;
  allocations=0;tracking=true;const auto result=core.Spawn(invalid[i],0,coreId);tracking=false;const auto coreAllocations=allocations;
  const bool accepted=command==ScriptSceneCommands::Result::Invalid && result==ScriptSceneStructure::Result::Invalid &&
   commandId==777 && coreId==777 && commands.OperationCount()==0 && core.OperationCount()==0 && commandAllocations==0 && coreAllocations==0;
  if(!accepted){++failures;std::cerr<<"UTF8_ADMISSION_FAILURE case="<<i<<" command="<<static_cast<int>(command)<<" core="<<static_cast<int>(result)<<" command_allocations="<<commandAllocations<<" core_allocations="<<coreAllocations<<'\n';}
  Require(world.Capture().dump()==snapshot && world.AllocationHighWatermark()==allocator,"Invalid admission modified live World/allocator");
 }
 ScriptSceneCommands commands;commands.Begin(&view,self);ScriptSceneStructure core;Require(core.Begin(&view)==ScriptSceneStructure::Result::Success,"Valid core fixture setup failed");
 for(const auto& name:valid){EntityId commandId=0,coreId=0;
  Require(commands.Spawn(name,0,commandId)==ScriptSceneCommands::Result::Success && commands.ReadWorld()->Find(commandId)->name==name,"Valid UTF8 combined admission failed");
  Require(core.Spawn(name,0,coreId)==ScriptSceneStructure::Result::Success && core.Candidate()->Find(coreId)->name==name,"Valid UTF8 core admission failed");
 }
 Require(commands.OperationCount()==valid.size() && core.OperationCount()==valid.size(),"Valid UTF8 operation count failed");
 Require(commands.Prepare() && commands.PublishPrepared() && world.GetEntities().size()==1+valid.size(),"Valid UTF8 publication failed");
 EntityWorld roundtrip;std::string error;Require(roundtrip.Restore(world.Capture(),error),error.c_str());
 Require(roundtrip.Capture().dump()==world.Capture().dump(),"Valid UTF8 Scene value roundtrip failed");
 const auto fixture=std::filesystem::temp_directory_path()/(L"kubo-scene-name-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
 Require(!std::filesystem::exists(fixture),"Scene name fixture already exists");std::filesystem::create_directory(fixture);
 const auto scenePath=fixture/L"Unicode名前.scene";
 Require(world.Save(scenePath,error,false),error.c_str());EntityWorld loaded;
 Require(loaded.Load(scenePath,error) && loaded.Capture().dump()==world.Capture().dump(),"Valid Unicode scalar/name-byte boundaries failed actual Scene file save/load");
 if(failures){std::cerr<<"FAIL: "<<failures<<" native invalid-name admission cases\n";return 1;}
 std::cout<<"PASS: allocation-free invalid UTF8/empty/NUL/name-length refusal without candidate operation/output/live identity mutation; valid Unicode scalar boundaries and 256-byte names through combined/core admission, publication and actual Scene file save/load\nRetained fixture: "<<fixture.string()<<'\n';return 0;
}catch(const std::exception& e){tracking=false;std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
