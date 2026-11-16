#include "engine/base/core/WinApp.h"
#include "engine/base/directx/DirectXCommon.h"
#include "engine/base/directx/SrvManager.h"
#include "engine/3d/object/Object3DCommon.h"
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
#include <cstdlib>
#include <new>
#include <string_view>

namespace {bool observeAllocations=false,failNextAllocation=false;size_t allocationCalls=0;}
template<class T>uint32_t DiagnosticFailures(const T& descriptors){
 if constexpr(requires{descriptors.GetUsageLabelFailureCount();})return descriptors.GetUsageLabelFailureCount();
 else return 0; // Retain source compatibility with the immutable before SDK.
}
void* operator new(size_t bytes){
 if(observeAllocations)++allocationCalls;
 if(failNextAllocation){failNextAllocation=false;throw std::bad_alloc();}
 if(void* memory=std::malloc(bytes?bytes:1))return memory;
 throw std::bad_alloc();
}
void* operator new[](size_t bytes){return ::operator new(bytes);}
void operator delete(void* memory) noexcept{std::free(memory);}
void operator delete[](void* memory) noexcept{std::free(memory);}
void operator delete(void* memory,size_t) noexcept{std::free(memory);}
void operator delete[](void* memory,size_t) noexcept{std::free(memory);}

int main(int argc,char** argv){try {
 bool expectFixed=false;for(int argument=1;argument<argc;++argument)
  if(std::string_view(argv[argument])=="--expect-fixed")expectFixed=true;
 Engine::Base::WinApp window;window.Initialize();ShowWindow(window.GetHwnd(),SW_HIDE);
 auto graphics=std::make_shared<Engine::Base::DirectXCommon>();graphics->Initialize(&window);
 Engine::Base::SrvManager descriptors;descriptors.Initialize(graphics);
 if(argc>1 && std::string_view(argv[1])=="--reclaim-allocation"){
  const auto retired=descriptors.Allocate();descriptors.Free(retired);
  graphics->PrepareForFrame();graphics->BeginGpuFrameTiming();graphics->Begin();graphics->End();graphics->WaitForAllFrames();
  bool faulted=false;failNextAllocation=true;
  try{descriptors.GetRemainingCount();}catch(const std::bad_alloc&){faulted=true;}
  const bool injected=!failNextAllocation;
  failNextAllocation=false;
  const auto remaining=descriptors.GetRemainingCount();
  const auto first=descriptors.Allocate(),second=descriptors.Allocate();
  const auto used=descriptors.GetUsedCount();
  std::cout<<"SRV_RECLAIM_FAULT exception="<<faulted<<" remaining="<<remaining
   <<" max="<<descriptors.GetMaxCount()<<" first="<<first<<" second="<<second<<" used="<<used
   <<" fault_consumed="<<injected<<" diagnostic_failures="<<DiagnosticFailures(descriptors)<<'\n';
  const bool duplicate=faulted && remaining==descriptors.GetMaxCount()+1 && first==retired && second==retired && used==2;
  const bool isolated=!faulted && remaining==descriptors.GetMaxCount() && first==retired && second!=first && used==2;
  // No resource/SRV view was ever created in this private heap. GPU submission
  // is complete; recreate only this fixture heap to discard corrupted inventory.
  descriptors.Initialize(graphics);
  if(!(expectFixed?isolated:duplicate) || descriptors.GetUsedCount()!=0 || descriptors.GetRemainingCount()!=descriptors.GetMaxCount())
   throw std::runtime_error("Reclaim expected inventory invariant was not satisfied");
  window.Finalize();
  if(expectFixed)std::cout<<"PASS: reclaim with armed allocation fault does not throw, duplicate slots or exceed inventory; consumed status reported\n";
  else std::cout<<"REPRODUCED: reclaim diagnostic bad_alloc duplicates returned slot on retry; unused private heap recreated after GPU completion. This is before evidence, not engine PASS.\n";
  return 0;
 }
 if(argc>1 && std::string_view(argv[1])=="--label-allocation"){
  allocationCalls=0;observeAllocations=true;const auto index=descriptors.Allocate();observeAllocations=false;
  const auto acquireCalls=allocationCalls;
  const std::string longLabel(512,'x');bool faulted=false;
  allocationCalls=0;observeAllocations=true;failNextAllocation=true;
  try{descriptors.LabelUsage(index,longLabel);}catch(const std::bad_alloc&){faulted=true;}
  observeAllocations=false;failNextAllocation=false;const auto labelCalls=allocationCalls;
  const bool coherent=(faulted!=expectFixed) && (!expectFixed || DiagnosticFailures(descriptors)==1)
   && descriptors.IsAllocated(index) && descriptors.GetUsedCount()==1
   && descriptors.GetUsageRecords().size()==1 && descriptors.GetUsageRecords()[0].usage=="Allocated";
  allocationCalls=0;observeAllocations=true;const bool freed=descriptors.Free(index);observeAllocations=false;
  const auto releaseCalls=allocationCalls;
  graphics->WaitForAllFrames();window.Finalize();
  std::cout<<"SRV_LABEL_FAULT coherent="<<coherent<<" acquire_allocations="<<acquireCalls
   <<" label_allocations="<<labelCalls<<" release_allocations="<<releaseCalls
   <<" used_after_release="<<descriptors.GetUsedCount()<<'\n';
  if(!coherent || !freed || (!expectFixed && (!acquireCalls || !releaseCalls)) || labelCalls!=1 || descriptors.GetUsedCount()!=0)
   throw std::runtime_error("Label fault/allocator isolation assertion failed");
  // Debug stdlib instrumentation also allocates for apparently short strings.
  // Probe the real API, not an inference about SSO/reserved vector capacity.
  Engine::Base::SrvManager acquiring;acquiring.Initialize(graphics);
  uint32_t returned=UINT32_MAX;bool acquireFault=false;failNextAllocation=true;
  try{returned=acquiring.Allocate();}catch(const std::bad_alloc&){acquireFault=true;}
  const bool acquireInjected=!failNextAllocation;
  failNextAllocation=false;
  const bool orphan=acquireFault && returned==UINT32_MAX && acquiring.GetUsedCount()==1 && acquiring.IsAllocated(0);
  const bool acquired=!acquireFault && returned!=UINT32_MAX && acquiring.GetUsedCount()==1 && acquiring.IsAllocated(returned);
  std::cout<<"SRV_ACQUIRE_FAULT exception="<<acquireFault<<" returned="<<returned<<" orphan="<<orphan
   <<" used="<<acquiring.GetUsedCount()<<" fault_consumed="<<acquireInjected
   <<" diagnostic_failures="<<DiagnosticFailures(acquiring)<<'\n';
  if(expectFixed && (!acquireInjected || DiagnosticFailures(acquiring)!=1))throw std::runtime_error("Acquire diagnostic fault was not counted");
  for(uint32_t slot=0;slot<acquiring.GetMaxCount();++slot)if(acquiring.IsAllocated(slot))acquiring.Free(slot);
  Engine::Base::SrvManager releasing;releasing.Initialize(graphics);const auto releasingIndex=releasing.Allocate();
  bool releaseFault=false;failNextAllocation=true;
  try{releasing.Free(releasingIndex);}catch(const std::bad_alloc&){releaseFault=true;}
  const bool releaseInjected=!failNextAllocation;
  failNextAllocation=false;
  const bool committed=releaseFault && !releasing.IsAllocated(releasingIndex) && releasing.GetUsedCount()==0;
  const bool released=!releaseFault && !releasing.IsAllocated(releasingIndex) && releasing.GetUsedCount()==0;
  std::cout<<"SRV_RELEASE_FAULT exception="<<releaseFault<<" committed_before_exception="<<committed
   <<" used="<<releasing.GetUsedCount()<<" fault_consumed="<<releaseInjected
   <<" diagnostic_failures="<<DiagnosticFailures(releasing)<<'\n';
  if(expectFixed && releaseInjected && DiagnosticFailures(releasing)!=1)throw std::runtime_error("Release diagnostic fault was not counted");
  graphics->WaitForAllFrames();
  if(!(expectFixed?(acquired && released):(orphan && committed)) || acquiring.GetUsedCount()!=0)
   throw std::runtime_error("Expected current Debug allocator ownership failures were not reproduced");
  if(expectFixed)std::cout<<"PASS: Debug diagnostic allocation failures preserve returned allocation, known label and completed release without throwing\n";
  else std::cout<<"REPRODUCED: Debug Allocate can orphan unreturned slot and Free can throw after ownership commit; fixtures recovered. Label fault alone preserves label/known ownership. This is before evidence, not engine PASS.\n";
  return 0;
 }
 std::vector<uint32_t> held;held.reserve(descriptors.GetMaxCount());
 while(descriptors.GetRemainingCount()>16)held.push_back(descriptors.Allocate());
 const auto baseline=descriptors.GetUsedCount();bool refused=false;std::string failure;
 auto* objects=Engine::Graphics3D::Object3DCommon::GetInstance();
 try{objects->Initialize(graphics,&descriptors);}catch(const std::exception& error){refused=true;failure=error.what();}
 graphics->WaitForAllFrames();objects->Finalize();
 const auto retained=descriptors.GetUsedCount();
 std::cout<<"OBJECT_PARTIAL_SRV baseline="<<baseline<<" after_finalize="<<retained
  <<" unreturned="<<(retained>=baseline?retained-baseline:0)<<" refused="<<refused<<" error="<<failure<<'\n';
 // Retained pre-fix evidence is expected. Explicitly recover only this private
 // process's descriptor inventory; no production/package/source files are edited.
 for(uint32_t index=0;index<descriptors.GetMaxCount();++index)
  if(descriptors.IsAllocated(index))descriptors.Free(index);
 graphics->WaitForAllFrames();objects->Finalize();
 if(descriptors.GetUsedCount()!=0)throw std::runtime_error("Private fixture cleanup did not release all descriptors");
 window.Finalize();
 if(!refused || failure.find("descriptor heap exhausted")==std::string::npos ||
   (expectFixed?retained!=baseline:retained<=baseline))
  throw std::runtime_error("Object partial-initialize expected ownership invariant was not satisfied");
 if(expectFixed)std::cout<<"PASS: partial Object3D initialization Finalize returned every owned SRV to baseline\n";
 else std::cout<<"REPRODUCED: SDK partial Object3D SRV acquisition remains after Finalize; fixture explicitly recovered to zero. This is failing-before evidence, not engine correctness PASS.\n";
 return 0;
}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
