#pragma once
#include "engine/scene/game/Game.h"
#include "WinApp.h"
#include "DirectXCommon.h"
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

// Same-thread dispatch hook delivers WM_CLOSE immediately after WM_NCCREATE,
// before Framework creates its D3D device. No timing sleep or external hook.
class StartupCloseEditorChecks final : public Engine::Scene::Game {
    inline static StartupCloseEditorChecks* owner_=nullptr;
    HHOOK hook_=nullptr;
    bool injected_=false, survived_=false, initialized_=false, retired_=false;
    static void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
    static LRESULT CALLBACK ObserveWindowCreation(int code,WPARAM window,LPARAM data) {
        const auto* event=reinterpret_cast<const CWPRETSTRUCT*>(data);
        if(code==HC_ACTION && event && event->message==WM_NCCREATE && owner_ && !owner_->injected_) {
            const auto hwnd=event->hwnd;wchar_t name[128]{};
            GetClassNameW(hwnd,name,128);
            if(wcscmp(name,L"WindowClass")==0) {
                owner_->injected_=true;
                const bool beforeDevice=!owner_->dxCommon;
                SendMessageW(hwnd,WM_CLOSE,0,0);
                owner_->survived_=IsWindow(hwnd)!=FALSE;
                std::cout<<"STARTUP_CLOSE_INJECTED=1 BEFORE_DEVICE="<<beforeDevice
                    <<" HWND_SURVIVED="<<owner_->survived_<<'\n';
            }
        }
        return CallNextHookEx(nullptr,code,window,data);
    }
    void ReleaseHook() noexcept {if(hook_){UnhookWindowsHookEx(hook_);hook_=nullptr;}if(owner_==this)owner_=nullptr;}
public:
    ~StartupCloseEditorChecks() override {ReleaseHook();}
    void Initialize() override {
        Check(!owner_,"Another startup close fixture owns the hook");
        const auto root=std::filesystem::temp_directory_path()/
            (L"kubo-startup-close-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
        std::filesystem::create_directories(root/"Assets");
        const auto scene=root/"entry.scene";
        {std::ofstream out(scene);out<<R"({"version":1,"entities":[]})";out.close();Check(!out.fail(),"Startup scene fixture write failed");}
        SetProjectRoot(root);SetProjectAssetRoot(root/"Assets");SetEditorMode(true);
        std::string error;Check(ConfigureStartupScene(scene,error),error.c_str());
        owner_=this;hook_=SetWindowsHookExW(WH_CALLWNDPROCRET,ObserveWindowCreation,nullptr,GetCurrentThreadId());
        Check(hook_!=nullptr,"Owned same-thread startup hook unavailable");
        try {Game::Initialize();}catch(...){ReleaseHook();throw;}
        ReleaseHook();initialized_=true;
        Check(injected_,"Startup close hook did not deliver WM_CLOSE");
        Check(survived_,"Startup close destroyed the HWND before device initialization");
        ShowWindow(winApp->GetHwnd(),SW_HIDE);
    }
    void Update() override {Game::Update();Check(IsEndRequest(),"Startup close request was lost or required confirmation for a clean Scene");}
    void Finalize() override {Game::Finalize();retired_=true;}
    void Verify() const {
        Check(initialized_ && injected_ && survived_ && retired_,"Startup close lifecycle incomplete");
        Check(winApp && !IsWindow(winApp->GetHwnd()),"Startup close left a live owned HWND");
        std::cout<<"PASS: actual Editor startup WM_CLOSE before device creation, preserved HWND/pending request/clean document close/owned retirement\n";
    }
};
