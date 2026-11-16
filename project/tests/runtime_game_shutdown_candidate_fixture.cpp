#include "RuntimeGame.h"
#include "DirectXCommon.h"
#include "WinApp.h"
#include "SrvManager.h"
#include "SceneManager.h"
#include "framework_run_failure_candidate.h"
#include <Windows.h>
#include <d3d12sdklayers.h>
#include <array>
#include <atomic>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace Engine::App {
struct RuntimeCandidateAccess {
    static auto& Scripts(RuntimeGame& host){return host.scripts_;}
    static auto& World(RuntimeGame& host){return host.world_;}
};
}
namespace {
struct OperationFault {const void* token;};
struct DestroyFault {const void* token;Engine::Scene::EntityId id;};
struct WaitAdmissionFault {const void* token;};
bool refuseWait=false;unsigned waitRefusals=0;const int waitIdentity=0;
void Require(bool value,const char* why){if(!value){std::cerr<<why<<'\n'<<std::flush;throw std::runtime_error(why);}}
std::atomic<unsigned> gpuErrors{0};
void CALLBACK ObserveGpu(D3D12_MESSAGE_CATEGORY,D3D12_MESSAGE_SEVERITY severity,D3D12_MESSAGE_ID,LPCSTR,void*){
    if(severity==D3D12_MESSAGE_SEVERITY_ERROR || severity==D3D12_MESSAGE_SEVERITY_CORRUPTION)++gpuErrors;
}
class Host final:public Engine::App::RuntimeGame {
public:
    void Initialize() override {
        RuntimeGame::Initialize();ShowWindow(winApp->GetHwnd(),SW_HIDE);
        Require(SUCCEEDED(dxCommon->GetDevice()->QueryInterface(IID_PPV_ARGS(&queue_))),"No owned GPU observer");
        Require(SUCCEEDED(queue_->RegisterMessageCallback(ObserveGpu,D3D12_MESSAGE_CALLBACK_IGNORE_FILTERS,nullptr,&cookie_)),"GPU callback admission failed");
    }
    ~Host() override {if(queue_)queue_->UnregisterMessageCallback(cookie_);}
    void Update() override {
        Engine::Base::Framework::Update(); // Initializes the actual borrowed-world EntityScene.
        auto& scripts=Engine::App::RuntimeCandidateAccess::Scripts(*this);
        scripts.Update(Engine::App::RuntimeCandidateAccess::World(*this),0.1f);
        scripts.FixedUpdate(Engine::App::RuntimeCandidateAccess::World(*this),0.1f);
        throw OperationFault{this};
    }
    void Draw() override {}
    auto* Graphics() const{return dxCommon.get();}auto* Descriptors() const{return srvManager.get();}
    HWND Window() const{return winApp->GetHwnd();}std::size_t Pending() const{return services_.PendingCount();}
private:
    Microsoft::WRL::ComPtr<ID3D12InfoQueue1> queue_;
    DWORD cookie_=0;
};
struct ExitBeforeUnsafeUnwind {
    std::unique_ptr<Host>& host;
    ~ExitBeforeUnsafeUnwind(){if(host && host->Graphics() && host->Graphics()->GetCommandState()!=Engine::Base::DirectXCommon::CommandState::Retired){std::cerr<<"Refuse unsafe real Player unwind\n"<<std::flush;std::_Exit(81);}}
};
}
// Linked only by the private graphics source when the wait-admission gate is
// requested. This is not queue Signal failure or actual device loss evidence.
void FixturePlayerWaitAdmission(){
    if(refuseWait){refuseWait=false;++waitRefusals;throw WaitAdmissionFault{&waitIdentity};}
}
void FixtureRunFatal(Engine::Base::Framework*,const std::exception_ptr&,const std::exception_ptr&,unsigned) noexcept {
    std::cerr<<"Unexpected persistent actual Player shutdown refusal\n"<<std::flush;std::_Exit(74);
}
int main(int argc,char** argv){try{
    if(argc!=3 && argc!=4)return 2;SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    const bool candidate=std::string_view(argv[1])=="candidate";
    const bool production=std::string_view(argv[1])=="production",coordinated=std::string_view(argv[1])=="coordinated",guarded=candidate || production || coordinated;
    const bool waitAdmission=argc==4 && std::string_view(argv[3])=="wait";
    const bool dispatchAdmission=argc==4 && std::string_view(argv[3])=="dispatch";
    if(argc==4 && (!(waitAdmission || dispatchAdmission) || !production))return 2;
    if(!candidate && !production && !coordinated && std::string_view(argv[1])!="before")return 2;
    const auto root=std::filesystem::path(argv[2]);
    Engine::Scene::EntityWorld seed;
    for(unsigned index=0;index!=32;++index){const auto id=seed.CreateEntity("Owned native Player script");Require(seed.SetScript(id,"owned/native"),"Seed native script failed");}
    std::string error;const auto scene=root/"player.json";Require(seed.Save(scene,error),"Save private Player scene failed");
    auto host=std::make_unique<Host>();ExitBeforeUnsafeUnwind guard{host};host->SetProjectRoot(root);
    Require(host->ConfigureStartupScene(scene,error),"Configure actual Player generic scene failed");
    std::array<unsigned,32> destroys{};unsigned first=32,reentries=0,retiredGpu=0;
    auto& scripts=Engine::App::RuntimeCandidateAccess::Scripts(*host);
    unsigned dispatchRefusals=0;
    auto checkDispatch=[&]{
        if(!dispatchAdmission)return;
        const auto used=host->Descriptors()->GetUsedCount();bool refused=false;
        try{host->Finalize();}catch(const std::logic_error&){refused=true;}
        Require(refused && scripts.IsDispatchActive() && host->Pending()==11 && IsWindow(host->Window()) &&
            host->Descriptors()->GetUsedCount()==used && host->Graphics()->GetCommandState()==Engine::Base::DirectXCommon::CommandState::Recording,
            "Player Finalize changed owners during active script dispatch");
        ++dispatchRefusals;
    };
    Engine::Scripting::ScriptCallbacks hooks;hooks.onCreate=[&](auto&){checkDispatch();};
    hooks.onUpdate=[&](auto&,float){checkDispatch();};hooks.onFixedUpdate=[&](auto&,float){checkDispatch();};
    hooks.onDestroy=[&](auto& entity){
        const auto index=static_cast<unsigned>(entity.id-1);if(first==32)first=index;++destroys.at(index);
        if(host->Graphics()->GetCommandState()==Engine::Base::DirectXCommon::CommandState::Retired)++retiredGpu;
        if(guarded){
            host->Finalize();++reentries;
            Require(host->Pending()==11 && IsWindow(host->Window()) && Engine::Scene::SceneManager::GetInstance()->GetCurrentScene(),"Nested Player finalization released live borrowed-world services");
        }
        throw DestroyFault{host.get(),entity.id};
    };
    Require(scripts.Register("owned/native",std::move(hooks)),"Register actual Player native callbacks failed");
    bool operation=false,cleanup=false;
    try{host->Run();}
    catch(const FrameworkRunFailure& failure){
        try{std::rethrow_exception(failure.Operation());}catch(const OperationFault& fault){operation=fault.token==host.get();}
        try{std::rethrow_exception(failure.Cleanup());}catch(const DestroyFault& fault){cleanup=fault.token==host.get() && fault.id==first+1;}
    }catch(const OperationFault& fault){operation=fault.token==host.get();}
    Require(operation && cleanup==(candidate || coordinated),"Actual Player Run lost original operation/Destroy diagnostics");
    Require(!scripts.IsDispatchActive() && dispatchRefusals==(dispatchAdmission ? 96u : 0u),"Player dispatch admission scope/count mismatch");
    Require(host->Pending()==(coordinated ? 0u : 11u) && bool(IsWindow(host->Window()))==!coordinated,"Actual Player pending ownership not retained/completed at correct boundary");
    Require(scripts.GetRunningCount()==((candidate || coordinated) ? 0u : 32u),"Actual Player instance retirement boundary mismatch");
    if(waitAdmission){
        refuseWait=true;bool originalWait=false;
        try{host->Finalize();}catch(const WaitAdmissionFault& fault){originalWait=fault.token==&waitIdentity;}
        Require(originalWait && waitRefusals==1 && scripts.GetRunningCount()==32 &&
            host->Pending()==11 && IsWindow(host->Window()) &&
            host->Graphics()->GetCommandState()==Engine::Base::DirectXCommon::CommandState::Recording,
            "Player wait refusal lost original error or retired owners before admission");
        for(auto count:destroys)Require(count==0,"Player wait refusal invoked Destroy before GPU retirement");
        Require(retiredGpu==0 && reentries==0,"Player wait refusal dispatched a hook");
    }
    if(!candidate && !coordinated){
        bool original=false;try{host->Finalize();}catch(const DestroyFault& fault){original=fault.token==host.get() && fault.id==first+1;}
        Require(original && scripts.GetRunningCount()==0 && host->Pending()==11 && IsWindow(host->Window()),"Original Player script exception did not prevent base retirement");
    }
    Require(retiredGpu==(guarded ? 32u : 0u) && reentries==(guarded ? 32u : 0u),"Actual Player GPU pre-drain/reentrancy protocol mismatch");
    auto* graphics=host->Graphics();auto* descriptors=host->Descriptors();
    host->Finalize();host->Finalize();
    Require(host->Graphics()==graphics && host->Descriptors()==descriptors && host->Pending()==0 && descriptors->GetUsedCount()==0 &&
        !IsWindow(host->Window()) && graphics->GetCommandState()==Engine::Base::DirectXCommon::CommandState::Retired &&
        !Engine::Scene::SceneManager::GetInstance()->GetCurrentScene(),"Actual Player retry did not retire borrowed Scene/core owners");
    for(auto count:destroys)Require(count==1,"Actual Player Destroy callback skipped/repeated");
    Require(!gpuErrors,"Actual Player owned GPU Error/Corruption observed");host.reset();
    if(waitAdmission)std::cout<<"RUNTIME_GAME_WAIT_ADMISSION refused=1 hooks_before_retry=0 retry_destroyed=32 native_exit=0\n";
    if(dispatchAdmission)std::cout<<"RUNTIME_GAME_DISPATCH_ADMISSION refused=96 owners_preserved=1 native_exit=0\n";
    std::cout<<"RUNTIME_GAME_SHUTDOWN_CANDIDATE variant="<<argv[1]<<" destroyed=32 retained_world=1 gpu_errors=0 native_exit=0\n";return 0;
}catch(const std::exception& failure){std::cerr<<failure.what()<<'\n';return 1;}}
