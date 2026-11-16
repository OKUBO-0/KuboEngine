#include "RuntimeGame.h"
#include "DirectXCommon.h"
#include "WinApp.h"
#include "SrvManager.h"
#include "SceneManager.h"
#include "BaseScene.h"
#include "Camera.h"
#include "CameraManager.h"
#include "framework_run_failure_candidate.h"
#include <Windows.h>
#include <d3d12sdklayers.h>
#include <atomic>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
struct InitFault {int stage;};
int target=-1;std::atomic<unsigned> gpuErrors{0};
Microsoft::WRL::ComPtr<ID3D12InfoQueue1> observed;DWORD cookie=0;
void Require(bool value,const char* why){if(!value){std::cerr<<why<<'\n'<<std::flush;throw std::runtime_error(why);}}
void Fail(int stage){if(target==stage)throw InitFault{stage};}
void CALLBACK Observe(D3D12_MESSAGE_CATEGORY,D3D12_MESSAGE_SEVERITY severity,D3D12_MESSAGE_ID,LPCSTR,void*){
    if(severity==D3D12_MESSAGE_SEVERITY_ERROR || severity==D3D12_MESSAGE_SEVERITY_CORRUPTION)++gpuErrors;
}
class Host final:public Engine::App::RuntimeGame {
public:
    void Update() override {Fail(1000);throw std::runtime_error("Unexpected partial Player Update");}
    void Draw() override {}
    auto* Graphics() const{return dxCommon.get();}auto* Descriptors() const{return srvManager.get();}
    HWND Window() const{return winApp ? winApp->GetHwnd() : nullptr;}
    std::size_t Pending() const{return services_.PendingCount();}
};
struct SafeRetirement {
    std::unique_ptr<Host>& host;
    ~SafeRetirement(){if(host && host->Graphics() && host->Graphics()->GetCommandState()!=Engine::Base::DirectXCommon::CommandState::Retired){std::cerr<<"Refuse unsafe partial Player unwind\n"<<std::flush;std::_Exit(81);}}
};
struct ForeignScene final:Engine::Scene::BaseScene {
    unsigned& destroyed;explicit ForeignScene(unsigned& count):destroyed(count){}~ForeignScene() override{++destroyed;}
    void Initialize() override {}void Update() override {}void Draw() override {}void Finalize() override {}
};
}
void FixturePartialCore(std::size_t stage){Fail(static_cast<int>(stage));}
void FixturePartialBefore(std::size_t stage){Fail(100+static_cast<int>(stage));}
void FixturePartialAfter(std::size_t stage){Fail(200+static_cast<int>(stage));}
void FixturePartialWindow(Engine::Base::WinApp* window){ShowWindow(window->GetHwnd(),SW_HIDE);}
void FixturePartialGraphics(Engine::Base::DirectXCommon* graphics){
    Require(SUCCEEDED(graphics->GetDevice()->QueryInterface(IID_PPV_ARGS(&observed))),"No partial Player InfoQueue1");
    Require(SUCCEEDED(observed->RegisterMessageCallback(Observe,D3D12_MESSAGE_CALLBACK_IGNORE_FILTERS,nullptr,&cookie)),"Partial GPU observer unavailable");
}
int main(int argc,char** argv){try{
    if(argc!=3)return 2;SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    unsigned cases=0;
    for(int stage=-1;stage<=1000;stage=(stage<3 ? stage+1 : stage==3 ? 100 : stage<110 ? stage+1 : stage==110 ? 200 : stage<210 ? stage+1 : stage==210 ? 1000 : 1001)){
        target=stage;unsigned foreignDestroyed=0;Engine::CameraSystem::Camera foreignCamera;
        const bool preserve=stage<4;
        if(preserve){
            Engine::CameraSystem::CameraManager::GetInstance()->AddCamera("partial-unacquired",&foreignCamera);
            Engine::Scene::SceneManager::GetInstance()->SetNextScene(std::make_unique<ForeignScene>(foreignDestroyed));
        }
        auto host=std::make_unique<Host>();SafeRetirement guard{host};
        const auto project=std::filesystem::path(argv[2])/std::to_string(stage);std::filesystem::create_directories(project);host->SetProjectRoot(project);
        if(stage==-1){host->Finalize();host->Finalize();}
        else {
            bool original=false;try{host->Run();}catch(const InitFault& fault){original=fault.stage==stage;}
            Require(original,"Partial Player original initialization fault lost");target=-1;host->Finalize();host->Finalize();
        }
        Require(host->Pending()==0 && !IsWindow(host->Window()) && (!host->Descriptors() || host->Descriptors()->GetUsedCount()==0) &&
            (!host->Graphics() || host->Graphics()->GetCommandState()==Engine::Base::DirectXCommon::CommandState::Retired),"Partial Player retirement incomplete");
        if(preserve){
            Require(!foreignDestroyed && Engine::CameraSystem::CameraManager::GetInstance()->FindCamera("partial-unacquired")==&foreignCamera,"Partial Player touched an unacquired foreign service");
            Engine::CameraSystem::CameraManager::GetInstance()->DetachCamera("partial-unacquired",&foreignCamera);
            Engine::Scene::SceneManager::GetInstance()->Finalize();Require(foreignDestroyed==1,"Foreign fixture did not retire after caller cleanup");
        }
        Require(!gpuErrors,"Partial Player GPU Error/Corruption observed");
        if(observed){observed->UnregisterMessageCallback(cookie);observed.Reset();}
        host.reset();++cases;
    }
    Require(cases==28,"Partial Player boundary coverage mismatch");
    std::cout<<"RUNTIME_GAME_PARTIAL_CANDIDATE variant="<<argv[1]<<" cases=28 unacquired_preserved=1 srv_zero=1 gpu_errors=0 native_exit=0\n";return 0;
}catch(const std::exception& failure){std::cerr<<failure.what()<<'\n';return 1;}}
