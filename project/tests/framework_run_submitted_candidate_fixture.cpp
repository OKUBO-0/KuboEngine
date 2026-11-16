#include "Framework.h"
#include "DirectXCommon.h"
#include "WinApp.h"
#include "SrvManager.h"
#include "framework_run_failure_candidate.h"
#include <Windows.h>
#include <d3d12sdklayers.h>
#include <atomic>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <thread>

struct OperationFault {const void* token;};
struct CoordinatedSignalFault {const void* token;unsigned attempt;std::exception_ptr original;};
bool coordinatedSubmitted=false;
bool holdGate=false;unsigned waitAttempts=0,waitTimeouts=0;
bool omitFinalization=false;
DWORD FixtureFenceWait(HANDLE event) noexcept {++waitAttempts;const auto result=WaitForSingleObject(event,1000);if(result==WAIT_TIMEOUT)++waitTimeouts;return result;}
std::string_view productionFaultMode;
bool removedAfterWake=false,staleWakeInjected=false;
unsigned completionReads=0;DWORD firstWaitBudget=0,secondWaitBudget=0;
ULONGLONG productionWaitElapsed=0;
UINT64 FixtureProductionFenceCompleted(ID3D12Fence* fence) noexcept {
    ++completionReads;
    if(productionFaultMode=="removed-before" || (productionFaultMode=="removed-after" && removedAfterWake))return UINT64_MAX;
    return fence->GetCompletedValue();
}
DWORD FixtureProductionFenceWait(HANDLE event,DWORD milliseconds) noexcept {
    ++waitAttempts;if(milliseconds>5000)std::_Exit(83);
    if(waitAttempts==1)firstWaitBudget=milliseconds;else if(waitAttempts==2)secondWaitBudget=milliseconds;
    if(productionFaultMode=="removed-after"){removedAfterWake=true;return WAIT_OBJECT_0;}
    if(productionFaultMode=="wait-failed"){SetLastError(ERROR_SUCCESS);return WAIT_FAILED;}
    if(productionFaultMode=="unexpected-wait")return 0x1234;
    if(productionFaultMode=="stale-wake" && !staleWakeInjected){staleWakeInjected=true;Sleep(50);return WAIT_OBJECT_0;}
    SetLastError(ERROR_ACCESS_DENIED); // Timeout must not use this unrelated error.
    const auto started=GetTickCount64();const auto result=WaitForSingleObject(event,milliseconds);
    productionWaitElapsed+=GetTickCount64()-started;if(result==WAIT_TIMEOUT)++waitTimeouts;return result;
}
const void* submittedHost=nullptr;
unsigned signalFailures=0,signalAttempts=0;
void FixtureBeforePresent(){if(submittedHost)throw OperationFault{submittedHost};}
HRESULT FixtureQueueSignal(ID3D12CommandQueue* queue,ID3D12Fence* fence,UINT64 value){
    ++signalAttempts;if(signalFailures){--signalFailures;return E_FAIL;}return queue->Signal(fence,value);
}
namespace {
void Require(bool value,const char* message){if(!value){std::cerr<<message<<'\n'<<std::flush;throw std::runtime_error(message);}}
struct Counts {unsigned finalized=0,destroyed=0;};
std::atomic<unsigned> gpuErrors{0};
void CALLBACK ObserveGpu(D3D12_MESSAGE_CATEGORY,D3D12_MESSAGE_SEVERITY severity,D3D12_MESSAGE_ID,LPCSTR message,void*){
    if(severity!=D3D12_MESSAGE_SEVERITY_ERROR && severity!=D3D12_MESSAGE_SEVERITY_CORRUPTION)return;
    ++gpuErrors;DWORD written=0;WriteFile(GetStdHandle(STD_ERROR_HANDLE),message,static_cast<DWORD>(std::strlen(message)),&written,nullptr);
}
class Host final:public Engine::Base::Framework {
public:
    explicit Host(Counts& counts):counts_(counts){}
    ~Host() override {if(release_.joinable())std::_Exit(82);if(queue_)queue_->UnregisterMessageCallback(cookie_);++counts_.destroyed;}
    void Initialize() override {
        Framework::Initialize();ShowWindow(winApp->GetHwnd(),SW_HIDE);
        Require(SUCCEEDED(dxCommon->GetDevice()->QueryInterface(IID_PPV_ARGS(&queue_))),"No owned InfoQueue1");
        Require(SUCCEEDED(queue_->RegisterMessageCallback(ObserveGpu,D3D12_MESSAGE_CALLBACK_IGNORE_FILTERS,nullptr,&cookie_)),"Owned GPU observer unavailable");
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=64;desc.Height=1;
        desc.DepthOrArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_UPLOAD;
        Require(SUCCEEDED(dxCommon->GetDevice()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&upload_))),"Upload creation failed");
        heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        Require(SUCCEEDED(dxCommon->GetDevice()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&destination_))),"Destination creation failed");
        heap.Type=D3D12_HEAP_TYPE_READBACK;
        Require(SUCCEEDED(dxCommon->GetDevice()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback_))),"Readback creation failed");
        Require(SUCCEEDED(dxCommon->GetDevice()->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate_))),"Gate fence creation failed");
        void* data=nullptr;D3D12_RANGE read{};Require(SUCCEEDED(upload_->Map(0,&read,&data)),"Upload Map failed");
        std::memset(data,0x5a,64);upload_->Unmap(0,nullptr);
    }
    void Update() override {Framework::Update();}
    bool IsEndRequest() const override {return false;}
    void Draw() override {
        Require(SUCCEEDED(dxCommon->GetCommandQueue()->Wait(gate_.Get(),1)),"Owned queue gate failed");
        release_=std::thread([this]{while(!drainEntered_.load())Sleep(1);if(holdGate){while(true)Sleep(1);}Sleep(250);earlyReturn_=drainReturned_.load();released_=gate_->Signal(1);});
        dxCommon->BeginGpuFrameTiming();dxCommon->Begin();
        auto* list=dxCommon->GetCommandList();list->CopyBufferRegion(destination_.Get(),0,upload_.Get(),0,64);
        D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource=destination_.Get();barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST;barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;
        list->ResourceBarrier(1,&barrier);list->CopyBufferRegion(readback_.Get(),0,destination_.Get(),0,64);
        submittedHost=this;dxCommon->End(); // Owned source hook throws after Execute, before Present/Signal.
    }
    void Finalize() override {++counts_.finalized;drainEntered_=true;if(omitFinalization){CheckProductionPersistent();return;}try{Framework::Finalize();}catch(...){
#ifdef KUBO_PRODUCTION_RUN_CHECKS
        if(!productionFaultMode.empty() && productionFaultMode!="stale-wake"){CheckProductionPersistent();throw;}
#endif
        if(coordinatedSubmitted)throw CoordinatedSignalFault{this,counts_.finalized,std::current_exception()};throw;
    }drainReturned_=true;if(release_.joinable())release_.join();}
    void CheckProductionPersistent(){
#ifdef KUBO_PRODUCTION_RUN_CHECKS
        if(counts_.finalized!=3)return;
        Require(!counts_.destroyed && Pending()==11 && IsWindow(Window()) && HeldBuffersAndThread() && Descriptors()->GetUsedCount()>0 && Graphics()->GetCommandList() && Graphics()->GetCommandState()==Engine::Base::DirectXCommon::CommandState::Submitted && !gpuErrors,"Production persistent failure lost submitted resource owners");
        const bool unfenced=productionFaultMode=="fatal" || productionFaultMode=="omitted-finalize";
        Require(Graphics()->HasUnfencedSubmission()==unfenced && signalAttempts==(productionFaultMode=="fatal" ? 3u : productionFaultMode=="omitted-finalize" ? 0u : 1u),"Production persistent completion ledger changed");
        if(productionFaultMode=="wait-timeout")Require(waitAttempts==3 && waitTimeouts==3 && productionWaitElapsed>=14900 && Graphics()->GetCompletedFenceValue()==0,"Actual finite event waits did not time out three times");
        if(productionFaultMode=="removed-before")Require(completionReads==3 && waitAttempts==0,"Removed fence sentinel authorized a wait or early completion");
        if(productionFaultMode=="removed-after")Require(completionReads==4 && waitAttempts==1,"Wake device sentinel authorized retirement");
        if(productionFaultMode=="wait-failed" || productionFaultMode=="unexpected-wait")Require(waitAttempts==3,"Invalid wait result authorized retirement");
        std::cout<<"PRODUCTION_FENCE_FATAL mode="<<productionFaultMode<<" attempts=3 pending=11 buffers_live=1 unfenced="<<unfenced<<" waits="<<waitAttempts<<" timeouts="<<waitTimeouts<<" complete_host_alive=1 unwind=0 gpu_errors=0\n"<<std::flush;
#endif
    }
    const Counts& HeldCounts() const{return counts_;}
    bool HeldBuffersAndThread() const{return upload_ && destination_ && readback_ && release_.joinable();}
    auto* Graphics() const{return dxCommon.get();}auto* Descriptors() const{return srvManager.get();}
    auto* Buffer() const{return destination_.Get();}HWND Window() const{return winApp->GetHwnd();}
    std::size_t Pending() const{return services_.PendingCount();}
    void CheckCompletedCopy(){
        Require(!earlyReturn_ && SUCCEEDED(released_),"Drain returned before owned queue release");
        void* data=nullptr;D3D12_RANGE range{0,64};Require(SUCCEEDED(readback_->Map(0,&range,&data)),"Readback Map failed");
        bool complete=true;for(unsigned index=0;index!=64;++index)complete=complete && static_cast<unsigned char*>(data)[index]==0x5a;
        D3D12_RANGE written{};readback_->Unmap(0,&written);Require(complete,"Submitted host buffer copy did not complete");
    }
private:
    Counts& counts_;DWORD cookie_=0;std::thread release_;
    std::atomic<bool> drainEntered_{false},drainReturned_{false},earlyReturn_{false};std::atomic<HRESULT> released_{E_FAIL};
    Microsoft::WRL::ComPtr<ID3D12InfoQueue1> queue_;Microsoft::WRL::ComPtr<ID3D12Fence> gate_;
    Microsoft::WRL::ComPtr<ID3D12Resource> upload_,destination_,readback_;
};
struct ExitBeforeUnsafeUnwind {
    std::unique_ptr<Host>& host;
    ~ExitBeforeUnsafeUnwind(){if(host && host->Graphics() && host->Graphics()->GetCommandState()!=Engine::Base::DirectXCommon::CommandState::Retired){std::cerr<<"Refuse unsafe submitted-host unwind\n"<<std::flush;std::_Exit(81);}}
};
bool IsOperation(const std::exception_ptr& failure,const void* token){if(!failure)return false;try{std::rethrow_exception(failure);}catch(const OperationFault& fault){return fault.token==token;}catch(...){return false;}}
bool IsSignalFailure(const std::exception_ptr& failure){if(!failure)return false;try{std::rethrow_exception(failure);}catch(const CoordinatedSignalFault& fault){return IsSignalFailure(fault.original);}catch(const std::runtime_error& fault){return std::string_view(fault.what()).starts_with("ID3D12CommandQueue::Signal failed with HRESULT 0x80004005");}catch(...){return false;}}
bool IsWaitFailure(const std::exception_ptr& failure){if(!failure)return false;try{std::rethrow_exception(failure);}catch(const std::runtime_error& fault){return std::string_view(fault.what()).starts_with("ID3D12Fence wait timeout failed with HRESULT 0x800705B4");}catch(...){return false;}}
}
void FixtureRunFatal(Engine::Base::Framework* base,const std::exception_ptr& operation,const std::exception_ptr& cleanup,unsigned attempts) noexcept{
    const auto* host=static_cast<const Host*>(base);bool first=false;
    if(omitFinalization){
        try{std::rethrow_exception(cleanup);}catch(const std::logic_error& fault){first=std::string_view(fault.what())=="Framework Finalize returned without terminal base retirement";}catch(...){}
        if(!first || attempts!=3 || host->HeldCounts().finalized!=3 || host->HeldCounts().destroyed || !IsOperation(operation,host) ||
            host->Pending()!=11 || !IsWindow(host->Window()) || !host->HeldBuffersAndThread() || !host->Descriptors()->GetUsedCount() ||
            host->Graphics()->GetCommandState()!=Engine::Base::DirectXCommon::CommandState::Submitted || !host->Graphics()->HasUnfencedSubmission() || signalAttempts || waitAttempts || gpuErrors)std::_Exit(74);
        std::cout<<"FRAMEWORK_RUN_OMITTED_FATAL returned_without_retirement=1 attempts=3 pending=11 buffers_live=1 signals=0 gpu_errors=0 native_exit=73\n"<<std::flush;std::_Exit(73);
    }
    try{std::rethrow_exception(cleanup);}catch(const CoordinatedSignalFault& fault){first=fault.token==host && fault.attempt==1 && (holdGate ? IsWaitFailure(fault.original) : IsSignalFailure(fault.original));}catch(...){}
    if(attempts!=3 || host->HeldCounts().finalized!=3 || host->HeldCounts().destroyed || !IsOperation(operation,host) || !first ||
        host->Pending()!=11 || !IsWindow(host->Window()) || !host->HeldBuffersAndThread() ||
        host->Graphics()->GetCommandState()!=Engine::Base::DirectXCommon::CommandState::Submitted || host->Graphics()->HasUnfencedSubmission()==holdGate ||
        !host->Graphics()->GetCommandList() || !host->Descriptors()->GetUsedCount() || signalAttempts!=(holdGate ? 1u : 3u) || gpuErrors ||
        (holdGate && (waitAttempts!=3 || waitTimeouts!=3 || host->Graphics()->GetCompletedFenceValue()!=0)))std::_Exit(74);
    if(holdGate){std::cout<<"FRAMEWORK_RUN_WAIT_FATAL submitted=1 unfenced=0 pending=11 buffers_live=1 waits=3 timeouts=3 signals=1 gpu_errors=0 native_exit=73\n"<<std::flush;std::_Exit(73);}
    std::cout<<"FRAMEWORK_RUN_SUBMITTED_FATAL submitted=1 unfenced=1 pending=11 buffers_live=1 gpu_errors=0 native_exit=73\n";
    std::cout<<"RUN_COORDINATOR_FATAL stage=3 attempts=3 original_identity=1 complete_host_alive=1 unwind=0 native_exit=73\n"<<std::flush;std::_Exit(73);
}
int main(int argc,char** argv){try{
    const bool fatalWait=argc==4 && std::string_view(argv[1])=="wait-timeout" && std::string_view(argv[2])=="3";
    const bool fatalOmitted=argc==4 && std::string_view(argv[1])=="omitted-finalize" && std::string_view(argv[2])=="3";
    const bool fatalRemoved=argc==4 && (std::string_view(argv[1])=="removed-before" || std::string_view(argv[1])=="removed-after" || std::string_view(argv[1])=="wait-failed" || std::string_view(argv[1])=="unexpected-wait") && std::string_view(argv[2])=="3";
    const bool staleWake=argc==3 && std::string_view(argv[1])=="stale-wake";
    const bool fatal=fatalWait || fatalOmitted || fatalRemoved || (argc==4 && std::string_view(argv[1])=="fatal" && std::string_view(argv[2])=="3");holdGate=fatalWait || fatalRemoved;omitFinalization=fatalOmitted;
#ifdef KUBO_PRODUCTION_RUN_CHECKS
    if(fatal || staleWake)productionFaultMode=argv[1];
#endif
    if(!fatal && argc!=3)return 2;SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    const bool candidate=std::string_view(argv[1])=="candidate",coordinated=fatal || staleWake || std::string_view(argv[1])=="coordinated";
    if(!candidate && !coordinated && std::string_view(argv[1])!="before")return 2;coordinatedSubmitted=coordinated;
    unsigned cases=0;
    for(unsigned failures:{0u,1u,2u}) {
        if(fatal)failures=(fatalWait || fatalOmitted || fatalRemoved) ? 0u : 3u;
        Counts counts;auto host=std::make_unique<Host>(counts);ExitBeforeUnsafeUnwind guard{host};const auto* token=host.get();
        const auto project=std::filesystem::path(argv[fatal ? 3 : 2])/std::to_string(failures);std::filesystem::create_directories(project);host->SetProjectRoot(project);
        submittedHost=nullptr;signalAttempts=0;waitAttempts=0;waitTimeouts=0;signalFailures=failures;completionReads=0;removedAfterWake=false;staleWakeInjected=false;firstWaitBudget=secondWaitBudget=0;productionWaitElapsed=0;std::exception_ptr operation,cleanup;bool paired=false;
        try{host->Run();}catch(const FrameworkRunFailure& fault){operation=fault.Operation();cleanup=fault.Cleanup();paired=true;}
        catch(const OperationFault&){operation=std::current_exception();}
        if(fatal)std::_Exit(76);
        if(coordinated)Require(waitAttempts==(staleWake ? 2u : 1u) && !waitTimeouts,"Coordinated submission wait count/timeout mismatch");
        if(staleWake)Require(staleWakeInjected && firstWaitBudget<=5000 && secondWaitBudget<firstWaitBudget && productionWaitElapsed>=150,"Stale wake restarted budget or authorized early resource reuse");
        Require(IsOperation(operation,token),"Original submitted-operation fault lost");
        Require(paired==((candidate || coordinated) && failures>0) && IsSignalFailure(cleanup)==((candidate || coordinated) && failures>0),"Original Signal cleanup failure lost");
        if(coordinated && failures){bool first=false;try{std::rethrow_exception(cleanup);}catch(const CoordinatedSignalFault& fault){first=fault.token==token && fault.attempt==1;}Require(first,"Coordinator replaced first Signal fault");}
        Require(counts.finalized==(coordinated ? failures+1 : candidate ? 1u : 0u) && !counts.destroyed,"Run did not retain complete submitted host");
        auto* graphics=host->Graphics();auto* descriptors=host->Descriptors();auto* buffer=host->Buffer();const auto window=host->Window();
        const bool pending=!coordinated && (!candidate || failures>0);
        Require(host->Pending()==(pending ? 11u : 0u) && (IsWindow(window)!=FALSE)==pending,"Submitted ownership boundary mismatch");
        if(pending)Require(graphics->GetCommandState()==Engine::Base::DirectXCommon::CommandState::Submitted && graphics->HasUnfencedSubmission() && graphics->GetCommandList() && descriptors->GetUsedCount()>0 && buffer,"Lost submitted/unfenced host resources");
        const auto originalCleanup=cleanup;unsigned retryFailures=0;bool retired=false;
        for(unsigned retry=0;retry!=3;++retry){try{host->Finalize();retired=true;break;}catch(...){Require(IsSignalFailure(std::current_exception()),"Retry replaced Signal failure type/diagnostic");++retryFailures;}}
        if(!retired)std::_Exit(80);
        Require(retryFailures==(coordinated ? 0u : failures-(candidate && failures ? 1u : 0u)) && cleanup==originalCleanup,"Retry overwrote original cleanup diagnostic");
        Require(host->Graphics()==graphics && host->Descriptors()==descriptors && host->Buffer()==buffer,"Retry changed complete core/buffer owner");
        Require(host->Pending()==0 && descriptors->GetUsedCount()==0 && !IsWindow(window) && !graphics->HasUnfencedSubmission() &&
            graphics->GetCommandState()==Engine::Base::DirectXCommon::CommandState::Retired && !graphics->GetCommandList() && graphics->GetCompletedFenceValue()==1 && signalAttempts==failures+1,"Submitted terminal completion incomplete");
        host->CheckCompletedCopy();Require(!gpuErrors,"Owned GPU Error/Corruption observed");host.reset();Require(counts.destroyed==1,"Host destruction not exactly once");++cases;
    }
    if(staleWake)std::cout<<"PRODUCTION_FENCE_STALE cases=3 remaining_budget=1 completed_copy=1 gpu_errors=0 native_exit=0\n";
    std::cout<<"FRAMEWORK_RUN_SUBMITTED_CANDIDATE variant="<<(coordinated ? "coordinated" : candidate ? "candidate" : "before")<<" cases="<<cases<<" retained_host=1 completed_copy=1 waited_for_gate=1 gpu_errors=0 native_exit=0\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
