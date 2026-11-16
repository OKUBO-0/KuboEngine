#include "Framework.h"
#include "DirectXCommon.h"
#include "WinApp.h"
#include "SrvManager.h"
#include "CameraManager.h"
#include "Camera.h"
#include "SceneManager.h"
#include "BaseScene.h"
#include <Windows.h>
#include <d3d12sdklayers.h>
#include <array>
#include <atomic>
#include <cstring>
#include <iostream>
#include <string>

struct Boundary { int stage; };
int target = -1;
std::array<unsigned,11> retirementCalls{};
unsigned coreAcquisitions=0, serviceAcquisitions=0;
Microsoft::WRL::ComPtr<ID3D12InfoQueue1> observedQueue;
DWORD observerCookie=0;
std::atomic<unsigned> observedErrors{0};
void CALLBACK ObserveGpu(D3D12_MESSAGE_CATEGORY, D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID,
    LPCSTR message, void*) {
    if(severity!=D3D12_MESSAGE_SEVERITY_ERROR && severity!=D3D12_MESSAGE_SEVERITY_CORRUPTION) return;
    ++observedErrors;
    DWORD written=0;
    const char prefix[]="OWNED_GPU_ERROR: ";
    WriteFile(GetStdHandle(STD_ERROR_HANDLE),prefix,sizeof(prefix)-1,&written,nullptr);
    WriteFile(GetStdHandle(STD_ERROR_HANDLE),message,static_cast<DWORD>(strlen(message)),&written,nullptr);
    WriteFile(GetStdHandle(STD_ERROR_HANDLE),"\n",1,&written,nullptr);
}
void FixtureGraphicsReady(Engine::Base::DirectXCommon* graphics) {
    if(FAILED(graphics->GetDevice()->QueryInterface(IID_PPV_ARGS(&observedQueue))) ||
       FAILED(observedQueue->RegisterMessageCallback(ObserveGpu,D3D12_MESSAGE_CALLBACK_IGNORE_FILTERS,nullptr,&observerCookie)))
        throw std::runtime_error("Owned GPU observer unavailable");
}
void FailAt(int stage) { if (target == stage) throw Boundary{stage}; }
void FixtureCoreAcquired(std::size_t stage) { ++coreAcquisitions; std::cout << "CORE_ACQUIRED=" << stage << '\n' << std::flush; FailAt(static_cast<int>(stage)); }
void FixtureServiceAcquired(std::size_t stage) { ++serviceAcquisitions; std::cout << "SERVICE_ACQUIRED=" << stage << '\n' << std::flush; FailAt(100+static_cast<int>(stage)); }
void FixtureServiceReady(std::size_t stage) { FailAt(200+static_cast<int>(stage)); }
void FixtureReady() { FailAt(1000); }
void FixtureWindowReady(Engine::Base::WinApp* window) { ShowWindow(window->GetHwnd(), SW_HIDE); }
void FixtureBeforeRetire(std::size_t stage, Engine::Base::Framework* host) {
    ++retirementCalls.at(stage);
    host->Finalize(); // Actual base reentrancy guard, not a mock journal.
    FailAt(900+static_cast<int>(stage));
}
class Host final : public Engine::Base::Framework {
public:
    void Draw() override {}
    std::size_t Pending() const { return services_.PendingCount(); }
    auto* Graphics() const { return dxCommon.get(); }
    HWND Window() const { return winApp ? winApp->GetHwnd() : nullptr; }
    auto* Descriptors() const { return srvManager.get(); }
};
bool ReinitializationPreservesOwners(Host& host) {
    auto* graphics=host.Graphics();auto* descriptors=host.Descriptors();
    const auto window=host.Window();const auto pending=host.Pending();
    const auto core=coreAcquisitions, service=serviceAcquisitions;
    const auto retired=retirementCalls;
    const auto state=graphics ? graphics->GetCommandState() : Engine::Base::DirectXCommon::CommandState::Empty;
    bool refused=false;
    try { host.Initialize(); }
    catch(const std::logic_error& error) { refused=std::string_view(error.what())=="Framework initialization is single-use"; }
    return refused && host.Graphics()==graphics && host.Descriptors()==descriptors &&
        host.Window()==window && host.Pending()==pending && coreAcquisitions==core &&
        serviceAcquisitions==service && retirementCalls==retired &&
        (!graphics || graphics->GetCommandState()==state);
}
struct ForeignScene final : Engine::Scene::BaseScene {
    unsigned& destroyed;
    explicit ForeignScene(unsigned& count) : destroyed(count) {}
    ~ForeignScene() override { ++destroyed; }
    void Initialize() override {}
    void Update() override {}
    void Draw() override {}
    void Finalize() override {}
};
int main(int argc, char** argv) {
    if (argc != 3) return 2;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    const std::string mode = argv[1];
    Host host;
    host.SetProjectRoot(std::filesystem::path(argv[2]));
    if (mode == "cold") {
        Engine::CameraSystem::Camera camera;
        auto* cameras=Engine::CameraSystem::CameraManager::GetInstance();
        cameras->AddCamera("fixture-unacquired",&camera);
        unsigned destroyed=0;
        auto* scenes=Engine::Scene::SceneManager::GetInstance();
        scenes->SetNextScene(std::make_unique<ForeignScene>(destroyed));
        host.Finalize();host.Finalize();
        if (destroyed || cameras->FindCamera("fixture-unacquired") != &camera || host.Pending()) return 3;
        cameras->DetachCamera("fixture-unacquired",&camera);
        scenes->Finalize();
        if (destroyed != 1) return 4;
        std::cout << "FRAMEWORK_SERVICES mode=cold unacquired_preserved=1 repeated=1 pending=0\n";return 0;
    }
    if (mode.starts_with("core-")) target=std::stoi(mode.substr(5));
    else if (mode.starts_with("before-")) target=100+std::stoi(mode.substr(7));
    else if (mode.starts_with("after-")) target=200+std::stoi(mode.substr(6));
    else if (mode=="ready") target=1000;
    else if (mode=="repeat-failed") target=0;
    else if (mode!="normal" && mode!="retire" && mode!="shader-missing" &&
             mode!="repeat-ready" && mode!="repeat-retired") return 2;
    const auto expected=target;
    bool original=false;
    try { host.Initialize(); }
    catch (const Boundary& error) { original=error.stage==expected; }
    catch (const std::exception& error) {
        if(mode=="shader-missing" && std::string_view(error.what()).starts_with("IDxcUtils::LoadFile failed with HRESULT")) original=true;
        else { std::cerr << "Unexpected initialization: " << error.what() << '\n'; target=-1; host.Finalize();return 5; }
    }
    if ((expected >= 0 || mode=="shader-missing") != original) { target=-1;host.Finalize();return 6; }
    const auto before=host.Pending();
    std::cout << "INITIALIZATION_OBSERVED mode=" << mode << " pending=" << before << " original=" << original << '\n' << std::flush;
    if(mode=="repeat-ready" || mode=="repeat-failed") {
        target=-1;
        if(!ReinitializationPreservesOwners(host)) { host.Finalize();return 12; }
    }
    if (mode=="normal") {
        for(unsigned frame=0;frame!=2;++frame){auto* graphics=host.Graphics();graphics->BeginGpuFrameTiming();graphics->Begin();graphics->End();}
    }
    if (mode=="retire") {
        target=910;bool refused=false;
        auto* graphics=host.Graphics();auto* descriptors=host.Descriptors();auto window=host.Window();
        try { host.Finalize(); } catch (const Boundary& error) { refused=error.stage==910; }
        if(!refused || host.Pending()!=10 || host.Graphics()!=graphics || host.Descriptors()!=descriptors ||
           !IsWindow(window) || graphics->GetCommandState()!=Engine::Base::DirectXCommon::CommandState::Retired) return 7;
    }
    target=-1;host.Finalize();
    if(mode=="repeat-retired" && !ReinitializationPreservesOwners(host)) return 13;
    const auto calls=retirementCalls;host.Finalize();
    if(host.Pending() || calls!=retirementCalls || IsWindow(host.Window()) ||
       (host.Graphics() && host.Graphics()->GetCommandState()!=Engine::Base::DirectXCommon::CommandState::Retired)) return 8;
    if(host.Descriptors() && host.Descriptors()->GetUsedCount()!=0) return 9;
    if(observedErrors) return 11;
    if(observedQueue){observedQueue->UnregisterMessageCallback(observerCookie);observedQueue.Reset();}
    unsigned retired=0;for(auto count:retirementCalls)if(count)++retired;
    if(retired!=before) return 10;
    std::cout << "FRAMEWORK_SERVICES mode=" << mode << " original=" << original << " acquired=" << before
        << " retired=" << retired << " pending=0 repeated=1 srv_zero=1 owners_alive_until_cleanup=1\n";
}
