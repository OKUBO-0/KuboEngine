#include "Game.h"
#include "BaseScene.h"
#include "SceneManager.h"
#include "CameraManager.h"
#include "Camera.h"
#include "WinApp.h"
#include "DirectXCommon.h"
#include "SrvManager.h"
#include <imgui.h>
#include <Windows.h>
#include <d3d12sdklayers.h>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
void Require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
std::atomic<unsigned> gpuErrors{0};
void CALLBACK Observe(D3D12_MESSAGE_CATEGORY,D3D12_MESSAGE_SEVERITY severity,D3D12_MESSAGE_ID,LPCSTR,void*){
    if(severity==D3D12_MESSAGE_SEVERITY_ERROR || severity==D3D12_MESSAGE_SEVERITY_CORRUPTION)++gpuErrors;
}
class Host final:public Engine::Scene::Game {
public:
    auto* Graphics() const{return dxCommon.get();}auto* Descriptors() const{return srvManager.get();}
    HWND Window() const{return winApp ? winApp->GetHwnd() : nullptr;}
    std::size_t Pending() const{return services_.PendingCount();}
};
struct Hold {
    std::unique_ptr<Host>& host;
    ~Hold(){if(host && (host->Pending()!=0 || (host->Graphics() && host->Graphics()->GetCommandState()!=Engine::Base::DirectXCommon::CommandState::Retired))){std::cerr<<"Refuse unsafe partial Editor unwind\n"<<std::flush;std::_Exit(81);}}
};
struct ForeignScene final:Engine::Scene::BaseScene {
    unsigned& destroyed;explicit ForeignScene(unsigned& value):destroyed(value){}~ForeignScene() override{++destroyed;}
    void Initialize() override{}void Finalize() override{}void Update() override{}void Draw() override{}
};
void Check(){
    const auto root=std::filesystem::temp_directory_path()/(L"kubo-editor-partial-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    std::cout<<"Retained partial Editor fixture: "<<root<<'\n';
    unsigned cases=0;
    for(unsigned stage=0;stage!=4;++stage){
        const auto project=root/std::to_string(stage);std::filesystem::create_directories(project/"Assets");
        {std::ofstream manifest(project/"entry.kubo");manifest<<R"({"name":"Owned partial Editor","startupScene":"entry.scene"})";Require(bool(manifest),"Partial Editor manifest write failed");}
        unsigned foreignDestroyed=0;Engine::CameraSystem::Camera foreignCamera;
        if(stage==0){Engine::CameraSystem::CameraManager::GetInstance()->AddCamera("owned-cold-editor",&foreignCamera);Engine::Scene::SceneManager::GetInstance()->SetNextScene(std::make_unique<ForeignScene>(foreignDestroyed));}
        auto host=std::make_unique<Host>();Hold hold{host};host->SetEditorMode(true);host->SetProjectRoot(project);host->SetProjectFile(project/"entry.kubo");host->SetProjectAssetRoot(project/"Assets");host->SetMcpAtStartup(true);
        Engine::Scene::EntityWorld seed;seed.CreateEntity("Partial Editor");std::string error;
        Require(seed.Save(project/"entry.scene",error),error.c_str());Require(host->ConfigureStartupScene(project/"entry.scene",error),error.c_str());
        const auto descriptor=Engine::Services::EditorMcpSession::Descriptor(project);
        if(stage==2){std::ofstream broken(project/"entry.scene",std::ios::trunc);broken<<"{";Require(bool(broken),"Owned broken Scene write failed");}
        if(stage==3){std::filesystem::create_directories(descriptor.parent_path());std::ofstream foreign(descriptor);foreign<<"foreign descriptor preserved";Require(bool(foreign),"Owned foreign MCP descriptor write failed");}
        std::exception_ptr initialization;
        if(stage!=0){
            try{if(stage==1)host->Engine::Base::Framework::Initialize();else host->Initialize();}catch(...){initialization=std::current_exception();}
            Require(bool(initialization)==(stage==2),"Partial Editor Scene initialization fault boundary mismatch");
            Require(host->Pending()==11 && ImGui::GetCurrentContext() && IsWindow(host->Window()),"Actual partial Editor did not retain base/UI ownership");ShowWindow(host->Window(),SW_HIDE);
        }
        Microsoft::WRL::ComPtr<ID3D12InfoQueue1> messages;DWORD cookie=0;
        if(host->Graphics()){Require(SUCCEEDED(host->Graphics()->GetDevice()->QueryInterface(IID_PPV_ARGS(&messages))) && SUCCEEDED(messages->RegisterMessageCallback(Observe,D3D12_MESSAGE_CALLBACK_IGNORE_FILTERS,nullptr,&cookie)),"Partial Editor GPU observer missing");}
        struct Observer {ID3D12InfoQueue1* queue;DWORD cookie;~Observer(){if(queue)queue->UnregisterMessageCallback(cookie);}} observer{messages.Get(),cookie};
        host->Finalize();host->Finalize();
        Require(host->Pending()==0 && !ImGui::GetCurrentContext() && !IsWindow(host->Window()) && (!host->Descriptors() || host->Descriptors()->GetUsedCount()==0) && (!host->Graphics() || host->Graphics()->GetCommandState()==Engine::Base::DirectXCommon::CommandState::Retired),"Partial Editor teardown incomplete");
        if(stage==0){Require(!foreignDestroyed && Engine::CameraSystem::CameraManager::GetInstance()->FindCamera("owned-cold-editor")==&foreignCamera,"Cold Editor retired foreign services");Engine::CameraSystem::CameraManager::GetInstance()->DetachCamera("owned-cold-editor",&foreignCamera);Engine::Scene::SceneManager::GetInstance()->Finalize();Require(foreignDestroyed==1,"Owned foreign Scene cleanup failed");}
        if(stage==3){std::ifstream foreign(descriptor);const std::string bytes(std::istreambuf_iterator<char>(foreign),{});Require(bytes=="foreign descriptor preserved","Editor took ownership of foreign MCP descriptor");}
        if(initialization){bool original=false;try{std::rethrow_exception(initialization);}catch(const std::exception& fault){original=std::string(fault.what()).size()>0;}Require(original,"Partial Editor initialization diagnostic lost");}
        Require(!gpuErrors,"Partial Editor teardown GPU Error/Corruption observed");host.reset();++cases;
    }
    Require(cases==4,"Partial Editor case count mismatch");std::cout<<"EDITOR_PARTIAL cases=4 cold_foreign_preserved=1 scene_fault=1 mcp_conflict_preserved=1 gpu_errors=0 native_exit=0\n";
}
}
int WINAPI WinMain(HINSTANCE,HINSTANCE,LPSTR,int){std::cout<<std::unitbuf;std::cerr<<std::unitbuf;SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);try{Check();return 0;}catch(const std::exception& fault){std::cerr<<fault.what()<<'\n';return 1;}}
