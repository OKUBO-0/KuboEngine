#include "engine/base/core/Framework.h"
#include "engine/base/core/WinApp.h"
#include "engine/base/directx/DirectXCommon.h"
#include "engine/base/directx/SrvManager.h"
#include <Windows.h>
#include <d3d12sdklayers.h>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace {
void Require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
struct OperationFault {const void* token;};
std::atomic<unsigned> gpuErrors{0};
void CALLBACK Observe(D3D12_MESSAGE_CATEGORY,D3D12_MESSAGE_SEVERITY severity,D3D12_MESSAGE_ID,LPCSTR,void*){
    if(severity==D3D12_MESSAGE_SEVERITY_ERROR || severity==D3D12_MESSAGE_SEVERITY_CORRUPTION)++gpuErrors;
}
struct Counts {unsigned finalizations=0,hostDestroyed=0,memberDestroyed=0;};
struct Member {Counts& counts;~Member(){++counts.memberDestroyed;}};
class Host final:public Engine::Base::Framework {
public:
    Host(Counts& counts,bool persistent):counts_(counts),member_{counts},persistent_(persistent){}
    ~Host() override{if(release_.joinable())std::_Exit(82);if(observer_)observer_->UnregisterMessageCallback(cookie_);++counts_.hostDestroyed;}
    void Initialize() override{
        Framework::Initialize();ShowWindow(winApp->GetHwnd(),SW_HIDE);
        Require(SUCCEEDED(dxCommon->GetDevice()->QueryInterface(IID_PPV_ARGS(&observer_))) && SUCCEEDED(observer_->RegisterMessageCallback(Observe,D3D12_MESSAGE_CALLBACK_IGNORE_FILTERS,nullptr,&cookie_)),"SDK fence observer unavailable");
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=64;desc.Height=1;
        desc.DepthOrArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_UPLOAD;
        Require(SUCCEEDED(dxCommon->GetDevice()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&upload_))),"SDK upload allocation failed");
        heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        Require(SUCCEEDED(dxCommon->GetDevice()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&destination_))),"SDK destination allocation failed");
        heap.Type=D3D12_HEAP_TYPE_READBACK;
        Require(SUCCEEDED(dxCommon->GetDevice()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback_))),"SDK readback allocation failed");
        Require(SUCCEEDED(dxCommon->GetDevice()->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate_))),"SDK gate creation failed");
        void* data=nullptr;D3D12_RANGE range{};Require(SUCCEEDED(upload_->Map(0,&range,&data)),"SDK upload map failed");std::memset(data,0x5a,64);upload_->Unmap(0,nullptr);
    }
    void Update() override{Framework::Update();if(drawn_)throw OperationFault{this};}
    void Draw() override{
        Require(!drawn_,"SDK probe drew twice");drawn_=true;
        Require(SUCCEEDED(dxCommon->GetCommandQueue()->Wait(gate_.Get(),1)),"SDK independent queue wait failed");
        if(!persistent_)release_=std::thread([this]{Sleep(250);released_=gate_->Signal(1);});
        dxCommon->BeginGpuFrameTiming();dxCommon->Begin();auto* commands=dxCommon->GetCommandList();
        commands->CopyBufferRegion(destination_.Get(),0,upload_.Get(),0,64);
        D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource=destination_.Get();barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST;barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;
        commands->ResourceBarrier(1,&barrier);commands->CopyBufferRegion(readback_.Get(),0,destination_.Get(),0,64);
        dxCommon->End(); // Only the unmodified, prebuilt SDK implementation.
    }
    void Finalize() override{
        ++counts_.finalizations;const auto began=GetTickCount64();
        try{Framework::Finalize();}
        catch(const std::runtime_error& error){
            if(!persistent_)throw;
            Require(std::string_view(error.what()).starts_with("ID3D12Fence wait timeout failed with HRESULT 0x800705B4"),"SDK shutdown did not report real timeout");
            Require(GetTickCount64()-began>=4900,"SDK timeout was injected or early");
            Require(Pending()==11 && IsWindow(Window()) && dxCommon->GetCommandList() && dxCommon->GetCommandState()!=Engine::Base::DirectXCommon::CommandState::Retired && !dxCommon->HasUnfencedSubmission() && dxCommon->GetCompletedFenceValue()==0 && upload_ && destination_ && readback_ && gate_ && !counts_.hostDestroyed && !counts_.memberDestroyed && !gpuErrors,"Prebuilt SDK timeout retired incomplete GPU resource owners");
            if(counts_.finalizations==3)std::cout<<"SDK_FENCE_FATAL attempts=3 real_timeouts=3 pending=11 buffers_live=1 complete_host_alive=1 unwind=0 gpu_errors=0\n"<<std::flush;
            throw;
        }
        if(release_.joinable())release_.join();
    }
    std::size_t Pending() const{return services_.PendingCount();}HWND Window() const{return winApp ? winApp->GetHwnd() : nullptr;}
    bool Retired() const{return dxCommon && dxCommon->GetCommandState()==Engine::Base::DirectXCommon::CommandState::Retired;}
    void CheckRecovery(){
        Require(Retired() && Pending()==0 && !IsWindow(Window()) && srvManager->GetUsedCount()==0 && !dxCommon->GetCommandList() && dxCommon->GetCompletedFenceValue()>=1 && SUCCEEDED(released_) && !gpuErrors,"Prebuilt SDK recovery propagated before terminal completion");
        void* data=nullptr;D3D12_RANGE range{0,64};Require(SUCCEEDED(readback_->Map(0,&range,&data)),"SDK readback map failed");
        bool complete=true;for(unsigned index=0;index!=64;++index)complete=complete && static_cast<unsigned char*>(data)[index]==0x5a;
        D3D12_RANGE written{};readback_->Unmap(0,&written);Require(complete,"SDK retained copy bytes incomplete");
    }
private:
    Counts& counts_;Member member_;bool persistent_,drawn_=false;DWORD cookie_=0;std::thread release_;
    std::atomic<HRESULT> released_{E_FAIL};Microsoft::WRL::ComPtr<ID3D12InfoQueue1> observer_;
    Microsoft::WRL::ComPtr<ID3D12Fence> gate_;Microsoft::WRL::ComPtr<ID3D12Resource> upload_,destination_,readback_;
};
void Check(bool persistent,const std::filesystem::path& root){
    std::filesystem::create_directories(root);Counts counts;auto host=std::make_unique<Host>(counts,persistent);host->SetProjectRoot(root);
    struct Hold {std::unique_ptr<Host>& host;~Hold(){if(host && (host->Pending()!=0 || !host->Retired())){std::cerr<<"Refuse unsafe SDK fixture unwind\n"<<std::flush;std::_Exit(81);}}} hold{host};
    bool original=false;try{host->Run();}catch(const OperationFault& error){original=error.token==host.get();}
    if(persistent)std::_Exit(76);
    Require(original && counts.finalizations==1 && !counts.hostDestroyed && !counts.memberDestroyed,"Prebuilt SDK lost operation identity/full-host lifetime");
    host->CheckRecovery();host.reset();Require(counts.hostDestroyed==1 && counts.memberDestroyed==1,"SDK host/member destruction not exact once");
    std::cout<<"SDK_FENCE_RECOVERY original_identity=1 terminal_before_throw=1 copied_bytes=64 exact_once=1 gpu_errors=0 native_exit=0\n";
}
}
int main(int argc,char** argv){std::cout<<std::unitbuf;std::cerr<<std::unitbuf;SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);try{if(argc!=3)return 2;const auto mode=std::string_view(argv[1]);if(mode!="recovery" && mode!="persistent")return 2;Check(mode=="persistent",std::filesystem::path(argv[2]));return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
