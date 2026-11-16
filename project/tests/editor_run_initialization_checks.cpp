#include "Game.h"
#include "DirectXCommon.h"
#include "WinApp.h"
#include "SrvManager.h"
#include "Camera.h"
#include "CameraManager.h"
#include "SceneManager.h"
#include "BaseScene.h"
#include "framework_run_failure_candidate.h"
#include <Windows.h>
#include <d3d12sdklayers.h>
#include <imgui.h>
#include <implot.h>
#include <imgui_node_editor.h>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace Engine::Scene {
class GameApplicationChecksAccess {
public:
    static bool Mcp(const Game& game){return game.mcpSession_.IsRunning();}
    static bool Import(const Game& game){return game.assetImportJob_.IsBusy();}
};
}
namespace {
void Require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
struct InitializationFault {const void* token;unsigned stage;};
struct CleanupFault {const void* token;};
std::atomic<unsigned> gpuErrors{0};
void CALLBACK Observe(D3D12_MESSAGE_CATEGORY,D3D12_MESSAGE_SEVERITY severity,D3D12_MESSAGE_ID,LPCSTR,void*){
    if(severity==D3D12_MESSAGE_SEVERITY_ERROR || severity==D3D12_MESSAGE_SEVERITY_CORRUPTION)++gpuErrors;
}
struct Counts {unsigned finalizations=0,hostDestroyed=0,memberDestroyed=0,uiConsumed=0;};
struct Member {Counts& counts;~Member(){++counts.memberDestroyed;}};
class Host;
Host* activeHost=nullptr;
class Host final:public Engine::Scene::Game {
public:
    Host(Counts& counts,unsigned stage,bool refuse):counts_(counts),member_{counts},stage_(stage),refuse_(refuse){}
    ~Host() override{if(messages_)messages_->UnregisterMessageCallback(cookie_);++counts_.hostDestroyed;}
    void Initialize() override{
        try{
            if(stage_==0)throw InitializationFault{this,stage_};
            Game::Initialize();
            Require(stage_==9,"Editor initialization unexpectedly passed its fault boundary");
            throw InitializationFault{this,stage_};
        }catch(...){original_=std::current_exception();throw;}
    }
    void Update() override{throw std::runtime_error("Update reached after failed Initialize");}
    void Draw() override{throw std::runtime_error("Draw reached after failed Initialize");}
    void Finalize() override{
        ++counts_.finalizations;
        Require(!counts_.hostDestroyed && !counts_.memberDestroyed && runActiveCandidate_,"Initialization cleanup lost complete host/admission");
        if(refuse_ && counts_.finalizations==1)throw CleanupFault{this};
        Game::Finalize();
    }
    void GraphicsReady(Engine::Base::DirectXCommon* graphics){
        Require(graphics==Graphics(),"Editor observation lost graphics owner");
        ShowWindow(Window(),SW_HIDE);
        Require(SUCCEEDED(graphics->GetDevice()->QueryInterface(IID_PPV_ARGS(&messages_))) &&
            SUCCEEDED(messages_->RegisterMessageCallback(Observe,D3D12_MESSAGE_CALLBACK_IGNORE_FILTERS,nullptr,&cookie_)),"Initialization observer unavailable");
    }
    void UiAcquired(unsigned stage){
        if(stage!=stage_)return;
        Require(Pending()==11 && IsWindow(Window()) && ImGui::GetCurrentContext() && runActiveCandidate_,"UI fault not inside live complete Editor acquisition");
        if(stage>=2)Require(ImPlot::GetCurrentContext()!=nullptr,"ImPlot boundary not acquired");
        if(stage>=3)Require(ax::NodeEditor::GetCurrentEditor()!=nullptr,"Node boundary not acquired");
        if(stage>=4)Require(ImGui::GetIO().BackendPlatformUserData!=nullptr,"Win32 boundary not acquired");
        if(stage>=6)Require(ImGui::GetIO().BackendRendererUserData!=nullptr,"DX12 boundary not acquired");
        ++counts_.uiConsumed;throw InitializationFault{this,stage_};
    }
    auto* Graphics() const{return dxCommon.get();}auto* Descriptors() const{return srvManager.get();}
    HWND Window() const{return winApp ? winApp->GetHwnd() : nullptr;}
    std::size_t Pending() const{return services_.PendingCount();}
    bool RunActive() const{return runActiveCandidate_;}
    const std::exception_ptr& Original() const{return original_;}
private:
    Counts& counts_;Member member_;unsigned stage_;bool refuse_;std::exception_ptr original_;
    Microsoft::WRL::ComPtr<ID3D12InfoQueue1> messages_;DWORD cookie_=0;
};
struct ForeignScene final:Engine::Scene::BaseScene {
    unsigned& destroyed;explicit ForeignScene(unsigned& value):destroyed(value){}~ForeignScene() override{++destroyed;}
    void Initialize() override{}void Finalize() override{}void Update() override{}void Draw() override{}
};
void Check(){
    const auto root=std::filesystem::temp_directory_path()/(L"kubo-editor-run-init-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    std::cout<<"Retained Editor Run initialization project: "<<root<<'\n';unsigned cases=0;
    for(unsigned stage=0;stage!=10;++stage)for(unsigned refusal=0;refusal!=2;++refusal){
        std::cout<<"EDITOR_RUN_INIT_BEGIN stage="<<stage<<" refusal="<<refusal<<'\n';
        const auto project=root/(std::to_string(stage)+"-"+std::to_string(refusal));std::filesystem::create_directories(project/"Assets");
        {std::ofstream manifest(project/"entry.kubo");manifest<<R"({"name":"Owned initialization","startupScene":"entry.scene"})";Require(bool(manifest),"Initialization manifest write failed");}
        Counts counts;auto host=std::make_unique<Host>(counts,stage,refusal!=0);activeHost=host.get();
        struct Hold {std::unique_ptr<Host>& host;~Hold(){activeHost=nullptr;if(host && (host->Pending()!=0 || (host->Graphics() && host->Graphics()->GetCommandState()!=Engine::Base::DirectXCommon::CommandState::Retired))){std::cerr<<"Refuse unsafe Editor initialization fixture unwind\n"<<std::flush;std::_Exit(81);}}} hold{host};
        host->SetEditorMode(true);host->SetProjectRoot(project);host->SetProjectFile(project/"entry.kubo");host->SetProjectAssetRoot(project/"Assets");host->SetMcpAtStartup(true);
        Engine::Scene::EntityWorld seed;seed.CreateEntity("Initialization owner");std::string error;
        Require(seed.Save(project/"entry.scene",error),error.c_str());Require(host->ConfigureStartupScene(project/"entry.scene",error),error.c_str());
        if(stage==8){std::ofstream broken(project/"entry.scene",std::ios::trunc);broken<<"{";Require(bool(broken),"Scene fault staging failed");}
        unsigned foreignDestroyed=0;Engine::CameraSystem::Camera foreignCamera;
        if(stage==0){Engine::CameraSystem::CameraManager::GetInstance()->AddCamera("owned-cold-run",&foreignCamera);Engine::Scene::SceneManager::GetInstance()->SetNextScene(std::make_unique<ForeignScene>(foreignDestroyed));}
        std::exception_ptr operation,cleanup;
        try{host->Run();}catch(const FrameworkRunFailure& pair){operation=pair.Operation();cleanup=pair.Cleanup();}catch(...){operation=std::current_exception();}
        // MSVC current_exception may make a fresh exception object at each catch;
        // shared_ptr-style exception_ptr equality is not an identity guarantee.
        Require(operation && host->Original() && bool(cleanup)==bool(refusal),"Run lost initialization operation/cleanup errors");
        if(stage==8){std::string originalDiagnostic;try{std::rethrow_exception(host->Original());}catch(const std::runtime_error& fault){originalDiagnostic=fault.what();}bool diagnostic=false;try{std::rethrow_exception(operation);}catch(const std::runtime_error& fault){diagnostic=!originalDiagnostic.empty() && originalDiagnostic==fault.what();}Require(diagnostic,"Actual Scene-read diagnostic lost");}
        else{bool identity=false;try{std::rethrow_exception(operation);}catch(const InitializationFault& fault){identity=fault.token==host.get() && fault.stage==stage;}Require(identity,"Typed initialization token changed");}
        if(cleanup){bool identity=false;try{std::rethrow_exception(cleanup);}catch(const CleanupFault& fault){identity=fault.token==host.get();}Require(identity,"First cleanup token changed");}
        Require(counts.finalizations==1+refusal && counts.uiConsumed==unsigned(stage>=1 && stage<=7) && !counts.hostDestroyed && !counts.memberDestroyed && !host->RunActive(),"Initialization cleanup attempts/ownership mismatch");
        Require(host->Pending()==0 && !ImGui::GetCurrentContext() && !ImPlot::GetCurrentContext() && !ax::NodeEditor::GetCurrentEditor() && !IsWindow(host->Window()) && (!host->Descriptors() || host->Descriptors()->GetUsedCount()==0) && (!host->Graphics() || host->Graphics()->GetCommandState()==Engine::Base::DirectXCommon::CommandState::Retired) && !Engine::Scene::GameApplicationChecksAccess::Mcp(*host) && !Engine::Scene::GameApplicationChecksAccess::Import(*host),"Run propagated before partial Editor terminal retirement");
        Require(!std::filesystem::exists(Engine::Services::EditorMcpSession::Descriptor(project)),"Initialization Run retained owned MCP descriptor");
        if(stage==0){Require(!foreignDestroyed && Engine::CameraSystem::CameraManager::GetInstance()->FindCamera("owned-cold-run")==&foreignCamera,"Cold Run retired foreign service owners");Engine::CameraSystem::CameraManager::GetInstance()->DetachCamera("owned-cold-run",&foreignCamera);Engine::Scene::SceneManager::GetInstance()->Finalize();Require(foreignDestroyed==1,"Foreign Scene fixture cleanup failed");}
        else Require(!Engine::Scene::SceneManager::GetInstance()->GetCurrentScene(),"Initialized Scene ownership not retired");
        Require(!gpuErrors,"Initialization retirement GPU Error/Corruption");activeHost=nullptr;host.reset();Require(counts.hostDestroyed==1 && counts.memberDestroyed==1,"Initialization complete host destruction not exact once");
        ++cases;std::cout<<"EDITOR_RUN_INIT_CASE stage="<<stage<<" refusal="<<refusal<<" terminal=1 original_identity=1\n";
    }
    Require(cases==20,"Initialization case count mismatch");
    std::cout<<"EDITOR_RUN_INITIALIZATION cases=20 ui_boundaries=7 scene_read_fault=1 cold_foreign_preserved=1 original_identity=1 terminal_before_throw=1 gpu_errors=0 native_exit=0\n";
}
}
void FixtureEditorGraphicsReady(Engine::Base::DirectXCommon* graphics){Require(activeHost!=nullptr,"No owned initialization host");activeHost->GraphicsReady(graphics);}
void FixtureEditorUiAcquired(unsigned stage){Require(activeHost!=nullptr,"No owned UI initialization host");activeHost->UiAcquired(stage);}
void FixtureRunFatal(Engine::Base::Framework*,const std::exception_ptr&,const std::exception_ptr&,unsigned) noexcept{std::cerr<<"Unexpected persistent Editor initialization refusal\n"<<std::flush;std::_Exit(74);}
int WINAPI WinMain(HINSTANCE,HINSTANCE,LPSTR,int){std::cout<<std::unitbuf;std::cerr<<std::unitbuf;SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);try{Check();return 0;}catch(const std::exception& fault){std::cerr<<fault.what()<<'\n';return 1;}}
