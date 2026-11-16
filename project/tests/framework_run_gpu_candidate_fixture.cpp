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

namespace {
enum class Stage {None,Initialize,Update,EndQuery,Draw};
struct OperationFault {const void* token;};
struct CleanupFault {const void* token;};
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Counts {unsigned finalized=0,destroyed=0;};
std::atomic<unsigned> gpuErrors{0};
void CALLBACK ObserveGpu(D3D12_MESSAGE_CATEGORY,D3D12_MESSAGE_SEVERITY severity,D3D12_MESSAGE_ID,LPCSTR message,void*){
    if(severity!=D3D12_MESSAGE_SEVERITY_ERROR && severity!=D3D12_MESSAGE_SEVERITY_CORRUPTION)return;
    ++gpuErrors;DWORD written=0;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE),message,static_cast<DWORD>(std::strlen(message)),&written,nullptr);
}
class Host final : public Engine::Base::Framework {
public:
    Host(Counts& counts,Stage stage,unsigned failures):counts_(counts),stage_(stage),failures_(failures){}
    ~Host() override {if(queue_)queue_->UnregisterMessageCallback(cookie_);++counts_.destroyed;}
    void Initialize() override {
        Framework::Initialize();ShowWindow(winApp->GetHwnd(),SW_HIDE);
        Require(SUCCEEDED(dxCommon->GetDevice()->QueryInterface(IID_PPV_ARGS(&queue_))),"No owned InfoQueue1");
        Require(SUCCEEDED(queue_->RegisterMessageCallback(ObserveGpu,D3D12_MESSAGE_CALLBACK_IGNORE_FILTERS,nullptr,&cookie_)),"GPU observer registration failed");
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=64;
        desc.Height=1;desc.DepthOrArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_UPLOAD;
        Require(SUCCEEDED(dxCommon->GetDevice()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&upload_))),"Owned upload creation failed");
        heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        Require(SUCCEEDED(dxCommon->GetDevice()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&destination_))),"Owned destination creation failed");
        void* data=nullptr;D3D12_RANGE read{};Require(SUCCEEDED(upload_->Map(0,&read,&data)),"Owned upload map failed");
        std::memset(data,0x34,64);upload_->Unmap(0,nullptr);
        Fail(Stage::Initialize);
    }
    void Update() override {Framework::Update();++updates_;Fail(Stage::Update);}
    bool IsEndRequest() const override {Fail(Stage::EndQuery);return updates_>1;}
    void Draw() override {
        dxCommon->BeginGpuFrameTiming();dxCommon->Begin();
        dxCommon->GetCommandList()->CopyBufferRegion(destination_.Get(),0,upload_.Get(),0,64);
        Fail(Stage::Draw); // Command list references complete-host-owned buffers.
        dxCommon->End();
    }
    void Finalize() override {++counts_.finalized;if(failures_){--failures_;throw CleanupFault{this};}Framework::Finalize();}
    auto* Graphics() const{return dxCommon.get();}
    auto* Descriptors() const{return srvManager.get();}
    auto* Buffer() const{return destination_.Get();}
    HWND Window() const{return winApp->GetHwnd();}
    std::size_t Pending() const{return services_.PendingCount();}
private:
    void Fail(Stage stage) const {if(stage_==stage)throw OperationFault{this};}
    Counts& counts_;Stage stage_;unsigned failures_,updates_=0;DWORD cookie_=0;
    Microsoft::WRL::ComPtr<ID3D12InfoQueue1> queue_;
    Microsoft::WRL::ComPtr<ID3D12Resource> upload_,destination_;
};
struct ExitBeforeUnsafeUnwind {
    std::unique_ptr<Host>& host;
    ~ExitBeforeUnsafeUnwind(){
        if(host && host->Graphics() && host->Graphics()->GetCommandState()!=Engine::Base::DirectXCommon::CommandState::Retired){
            std::cerr<<"Owned GPU fixture refuses unsafe host unwind before terminal retirement\n"<<std::flush;
            std::_Exit(81);
        }
    }
};
bool IsOperation(const std::exception_ptr& error,const void* token){if(!error)return false;try{std::rethrow_exception(error);}catch(const OperationFault& fault){return fault.token==token;}catch(...){return false;}}
bool IsCleanup(const std::exception_ptr& error,const void* token){if(!error)return false;try{std::rethrow_exception(error);}catch(const CleanupFault& fault){return fault.token==token;}catch(...){return false;}}
}
int main(int argc,char** argv){try{
    if(argc!=3)return 2;SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    const bool candidate=std::string_view(argv[1])=="candidate";
    if(!candidate && std::string_view(argv[1])!="before")return 2;
    unsigned cases=0;
    for(auto stage:{Stage::None,Stage::Initialize,Stage::Update,Stage::EndQuery,Stage::Draw})for(unsigned failures:{0u,1u}){
        Counts counts;auto host=std::make_unique<Host>(counts,stage,failures);const auto* token=host.get();
        ExitBeforeUnsafeUnwind retirementGuard{host};
        const auto project=std::filesystem::path(argv[2])/std::to_string(cases);
        std::filesystem::create_directories(project);host->SetProjectRoot(project);
        std::exception_ptr operation,cleanup;bool paired=false;
        try{host->Run();}catch(const FrameworkRunFailure& fault){operation=fault.Operation();cleanup=fault.Cleanup();paired=true;}
        catch(const OperationFault&){operation=std::current_exception();}catch(const CleanupFault&){cleanup=std::current_exception();}
        const bool hasOperation=stage!=Stage::None,attempted=candidate || !hasOperation;
        Require(IsOperation(operation,token)==hasOperation && IsCleanup(cleanup,token)==(attempted && failures),"GPU Run lost original fault identity");
        Require(paired==(candidate && hasOperation && failures),"GPU Run paired diagnostics mismatch");
        Require(counts.finalized==(attempted ? 1u : 0u) && !counts.destroyed,"GPU Run lost complete caller-owned host");
        auto* graphics=host->Graphics();auto* descriptors=host->Descriptors();auto* buffer=host->Buffer();const auto window=host->Window();
        const bool pending=!attempted || failures;
        Require(host->Pending()==(pending ? 11u : 0u) && IsWindow(window)==pending,"GPU Run unexpected retirement boundary");
        if(pending){
            Require(graphics->GetCommandState()==Engine::Base::DirectXCommon::CommandState::Recording && graphics->GetCommandList() && descriptors->GetUsedCount()>0 && buffer,"Pending host lost graphics/descriptor/buffer ownership");
        }
        // Guard unexpected exceptions by retaining the complete host until process
        // teardown rather than unwinding resource owners with a recording list.
        bool retired=false;
        for(unsigned retry=0;retry!=2;++retry){try{host->Finalize();retired=true;break;}catch(const CleanupFault& fault){Require(fault.token==token,"GPU retry lost diagnostic identity");}}
        if(!retired)std::_Exit(80);
        Require(host->Graphics()==graphics && host->Descriptors()==descriptors && host->Buffer()==buffer,"Retry changed complete host/core ownership");
        Require(host->Pending()==0 && descriptors->GetUsedCount()==0 && !IsWindow(window) &&
            graphics->GetCommandState()==Engine::Base::DirectXCommon::CommandState::Retired && !graphics->GetCommandList(),"GPU terminal retirement incomplete");
        Require(!gpuErrors,"Owned GPU Error/Corruption observed");host.reset();Require(counts.destroyed==1,"Complete host not destroyed once after retirement");
        ++cases;
    }
    Require(cases==10,"GPU Run case coverage mismatch");
    std::cout<<"FRAMEWORK_RUN_GPU_CANDIDATE variant="<<(candidate ? "candidate" : "before")<<" cases=10 retained_host=1 srv_zero=1 discarded_commands=1 gpu_errors=0 native_exit=0\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
