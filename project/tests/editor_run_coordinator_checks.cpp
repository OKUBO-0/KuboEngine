#include "Game.h"
#include "DirectXCommon.h"
#include "WinApp.h"
#include "SrvManager.h"
#include "SceneManager.h"
#include "framework_run_failure_candidate.h"
#include <Windows.h>
#include <shellapi.h>
#pragma comment(lib,"shell32.lib")
#include <d3d12sdklayers.h>
#include <imgui.h>
#include <array>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <string_view>

namespace Engine::Scene {
class GameApplicationChecksAccess {
public:
    static auto& Scripts(Game& game){return game.scriptRuntime_;}
    static auto& World(Game& game){return game.editorWorld_;}
    static bool Mcp(const Game& game){return game.mcpSession_.IsRunning();}
    static bool Import(const Game& game){return game.assetImportJob_.IsBusy();}
    static bool StartImport(Game& game,const std::filesystem::path& root){Assets::AssetImportJob::Request request;request.projectRoot=root;request.assetRoot=root/"Assets";return game.assetImportJob_.Start(std::move(request));}
};
}
namespace {
using Access=Engine::Scene::GameApplicationChecksAccess;
struct OperationFault {const void* token;};
struct DestroyFault {const void* token;Engine::Scene::EntityId id;};
void Require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
std::atomic<unsigned> gpuErrors{0};
void CALLBACK Observe(D3D12_MESSAGE_CATEGORY,D3D12_MESSAGE_SEVERITY severity,D3D12_MESSAGE_ID,LPCSTR,void*){if(severity==D3D12_MESSAGE_SEVERITY_ERROR || severity==D3D12_MESSAGE_SEVERITY_CORRUPTION)++gpuErrors;}
struct Counts {unsigned finalized=0,hostDestroyed=0,memberDestroyed=0;};
struct Member {Counts& counts;~Member(){++counts.memberDestroyed;}};
class Host final:public Engine::Scene::Game {
public:
    Host(Counts& counts,const std::filesystem::path& root,bool persistent=false):counts_(counts),member_{counts},root_(root),persistent_(persistent){}
    ~Host() override{if(messages_)messages_->UnregisterMessageCallback(cookie_);++counts_.hostDestroyed;}
    void Initialize() override{
        Game::Initialize();ShowWindow(Window(),SW_HIDE);
        Require(SUCCEEDED(Graphics()->GetDevice()->QueryInterface(IID_PPV_ARGS(&messages_))) && SUCCEEDED(messages_->RegisterMessageCallback(Observe,D3D12_MESSAGE_CALLBACK_IGNORE_FILTERS,nullptr,&cookie_)),"Editor coordinator GPU observer missing");
        Require(Access::Mcp(*this) && ImGui::GetCurrentContext() && Access::StartImport(*this,root_) && Access::Import(*this),"Editor coordinator MCP/UI/import ownership missing");
    }
    void Update() override{Engine::Base::Framework::Update();Access::Scripts(*this).Update(Access::World(*this),0.1f);Access::Scripts(*this).FixedUpdate(Access::World(*this),0.1f);throw OperationFault{this};}
    void Draw() override{}
    void Finalize() override{++counts_.finalized;
#ifdef KUBO_PRODUCTION_RUN_CHECKS
        if(persistent_){
            Require(runActive_ && Pending()==11 && IsWindow(Window()) && ImGui::GetCurrentContext() && Access::Mcp(*this) && Access::Import(*this) && Access::Scripts(*this).GetRunningCount()==32 && !counts_.hostDestroyed && !counts_.memberDestroyed && !gpuErrors,"Persistent Run unwound or retired owned host early");
            if(counts_.finalized==3)std::cout<<"PRODUCTION_RUN_PERSISTENT attempts=3 pending=11 complete_host_alive=1 unwind=0 gpu_errors=0\n";
            throw std::runtime_error("owned persistent shutdown refusal "+std::to_string(counts_.finalized));
        }
#endif
        Game::Finalize();}
    auto* Graphics() const{return dxCommon.get();}auto* Descriptors() const{return srvManager.get();}
    HWND Window() const{return winApp ? winApp->GetHwnd() : nullptr;}
    std::size_t Pending() const{return services_.PendingCount();}
    bool RunActive() const{return runActiveCandidate_;}
private:
    Counts& counts_;Member member_;std::filesystem::path root_;bool persistent_;Microsoft::WRL::ComPtr<ID3D12InfoQueue1> messages_;DWORD cookie_=0;
};
#ifdef KUBO_PRODUCTION_RUN_CHECKS
class CompetingHost final:public Engine::Base::Framework {
public:
    unsigned initialized=0,updated=0,finalized=0;
    void Initialize() override{++initialized;}
    void Update() override{++updated;}
    void Draw() override{throw std::runtime_error("Unexpected competing-host Draw");}
    bool IsEndRequest() const override{return true;}
    void Finalize() override{++finalized;}
};
#endif
void Check(bool persistent=false){
    const auto root=std::filesystem::temp_directory_path()/(L"kubo-editor-run-coordinator-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));std::filesystem::create_directories(root/"Assets");
    std::cout<<"Retained Editor coordinator project: "<<root<<'\n';
    {std::ofstream manifest(root/"entry.kubo");manifest<<R"({"name":"Owned coordinator","startupScene":"entry.scene"})";Require(bool(manifest),"Coordinator manifest write failed");}
    Engine::Scene::EntityWorld seed;for(unsigned index=0;index!=32;++index){const auto id=seed.CreateEntity("Owned Editor coordinator");Require(seed.SetScript(id,"owned/coordinator"),"Coordinator seed failed");}
    std::string error;Require(seed.Save(root/"entry.scene",error),error.c_str());
    Counts counts;auto host=std::make_unique<Host>(counts,root,persistent);
    struct Hold {std::unique_ptr<Host>& host;~Hold(){if(host && (host->Pending()!=0 || (host->Graphics() && host->Graphics()->GetCommandState()!=Engine::Base::DirectXCommon::CommandState::Retired))){std::cerr<<"Refuse unsafe Editor coordinator fixture unwind\n"<<std::flush;std::_Exit(81);}}} hold{host};
    host->SetProjectRoot(root);host->SetProjectFile(root/"entry.kubo");host->SetProjectAssetRoot(root/"Assets");host->SetEditorMode(true);host->SetMcpAtStartup(true);Require(host->ConfigureStartupScene(root/"entry.scene",error),error.c_str());
    std::array<unsigned,32> destroyed{};unsigned first=32,runRefusals=0,finalizeReentries=0;
#ifdef KUBO_PRODUCTION_RUN_CHECKS
    CompetingHost competitor;unsigned competingRefusals=0;bool foreignThreadRefused=false;
#endif
    auto refuseRun=[&]{const auto before=counts.finalized;bool refused=false;try{host->Run();}catch(const std::logic_error&){refused=true;}Require(refused && before==counts.finalized && host->Pending()==11 && IsWindow(host->Window()) && ImGui::GetCurrentContext(),"Editor nested Run changed borrowed host/UI ownership");++runRefusals;
#ifdef KUBO_PRODUCTION_RUN_CHECKS
        bool competing=false;try{competitor.Run();}catch(const std::logic_error&){competing=true;}
        Require(competing && !competitor.initialized && !competitor.finalized,"Competing Run touched shared services");++competingRefusals;
        if(!foreignThreadRefused){std::exception_ptr workerError;std::thread worker([&]{try{competitor.Run();}catch(...){workerError=std::current_exception();}});worker.join();if(workerError){try{std::rethrow_exception(workerError);}catch(const std::logic_error&){foreignThreadRefused=true;}}Require(foreignThreadRefused && !competitor.initialized && !competitor.finalized,"Foreign-thread Run changed owner admission");}
#endif
    };
    Engine::Scripting::ScriptCallbacks hooks;hooks.onCreate=[&](auto&){refuseRun();};hooks.onUpdate=[&](auto&,float){refuseRun();};hooks.onFixedUpdate=[&](auto&,float){refuseRun();};
    hooks.onDestroy=[&](auto& entity){
        const auto index=static_cast<unsigned>(entity.id-1);if(first==32)first=index;++destroyed.at(index);
        Require(host->Graphics()->GetCommandState()==Engine::Base::DirectXCommon::CommandState::Retired && !Access::Mcp(*host) && !Access::Import(*host),"Editor callback ran before GPU/MCP/import retirement");
        refuseRun();host->Finalize();++finalizeReentries;
        Require(host->Pending()==11 && ImGui::GetCurrentContext() && Engine::Scene::SceneManager::GetInstance()->GetCurrentScene() && !counts.hostDestroyed && !counts.memberDestroyed,"Editor nested Finalize destroyed live borrowed World/Scene/UI");
        throw DestroyFault{host.get(),entity.id};
    };
    Require(Access::Scripts(*host).Register("owned/coordinator",std::move(hooks)),"Coordinator script registration failed");
    bool operation=false,cleanup=false;try{host->Run();}catch(const FrameworkRunFailure& fault){try{std::rethrow_exception(fault.Operation());}catch(const OperationFault& original){operation=original.token==host.get();}try{std::rethrow_exception(fault.Cleanup());}catch(const DestroyFault& original){cleanup=original.token==host.get() && original.id==first+1;}}
    Require(operation && cleanup && !host->RunActive() && runRefusals==128 && finalizeReentries==32 && counts.finalized==34 && !counts.hostDestroyed && !counts.memberDestroyed,"Editor coordinated Run lost original pair or complete host lifetime");
    Require(host->Pending()==0 && host->Descriptors()->GetUsedCount()==0 && !IsWindow(host->Window()) && !ImGui::GetCurrentContext() && !Access::Mcp(*host) && !Access::Import(*host) && !std::filesystem::exists(Engine::Services::EditorMcpSession::Descriptor(root)) && !Engine::Scene::SceneManager::GetInstance()->GetCurrentScene() && host->Graphics()->GetCommandState()==Engine::Base::DirectXCommon::CommandState::Retired && Access::Scripts(*host).GetRunningCount()==0 && Access::World(*host).GetEntities().size()==32,"Editor Run propagated before terminal cleanup or lost retained World");
    for(auto count:destroyed)Require(count==1,"Editor coordinated Destroy skipped/repeated");Require(!gpuErrors,"Editor coordinated teardown GPU Error/Corruption");host.reset();Require(counts.hostDestroyed==1 && counts.memberDestroyed==1,"Editor complete host/member deletion not exact once");
#ifdef KUBO_PRODUCTION_RUN_CHECKS
    competitor.Run();Require(competingRefusals==128 && foreignThreadRefused && competitor.initialized==1 && competitor.updated==1 && competitor.finalized==1,"Production Run did not release global admission after terminal propagation");
    std::cout<<"PRODUCTION_RUN_GLOBAL competing_refusals=128 foreign_thread_refused=1 admission_released=1\n";
#endif
    std::cout<<"EDITOR_RUN_COORDINATOR destroyed=32 run_refusals=128 finalize_reentries=32 terminal_before_throw=1 original_pair=1 complete_host_alive=1 gpu_errors=0 native_exit=0\n";
}
}
void FixtureRunFatal(Engine::Base::Framework*,const std::exception_ptr&,const std::exception_ptr&,unsigned) noexcept{std::cerr<<"Unexpected persistent Editor coordinator refusal\n"<<std::flush;std::_Exit(74);}
int WINAPI WinMain(HINSTANCE,HINSTANCE,LPSTR,int){std::cout<<std::unitbuf;std::cerr<<std::unitbuf;SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);try{int count=0;auto* arguments=CommandLineToArgvW(GetCommandLineW(),&count);Require(arguments!=nullptr,"Owned Editor argument decode failed");const bool persistent=count==2 && std::wstring_view(arguments[1])==L"persistent";LocalFree(arguments);Require(count==1 || persistent,"Unknown owned Editor probe arguments");std::cout<<"PRODUCTION_RUN_MODE persistent="<<persistent<<'\n';Check(persistent);return 0;}catch(const std::exception& fault){std::cerr<<fault.what()<<'\n';return 1;}}
