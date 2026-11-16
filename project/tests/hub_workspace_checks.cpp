#define UNICODE
#define _UNICODE
#include "../launcher/main.cpp"
#include <iostream>

namespace {
void NeedHub(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::string ReadHubBytes(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
}
int wmain(int argc, wchar_t** argv)
{
    if (argc == 5 && std::wstring_view(argv[3]) == L"--hub-ready-event") {
        if (std::filesystem::path(argv[2]).filename() == L"Failed.kubo") return 19;
        const auto ready = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(std::wcstoull(argv[4],nullptr,10)));
        if (!SetEvent(ready)) return 20;
        Sleep(700);
        return 0;
    }
    HWND window=nullptr;
    try {
        namespace fs=std::filesystem;
        const auto root=fs::temp_directory_path()/(L"kubo-hub-workspace-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
        NeedHub(!fs::exists(root),"Expected fresh fixture");fs::create_directories(root);
        g_recentFile=root/L"profile/recent_projects.json";g_engineFile=root/L"profile/engine_catalog.json";g_engineError.clear();
        std::string error;NeedHub(KuboHub::EngineCatalogFile::Load(g_engineFile,g_engineCatalog,g_engineSnapshot,error),"Catalog init failed");
        Engine::Base::ProjectContext a,b;
        NeedHub(a.Create(root/L"alpha", "Alpha Lab",error),"Alpha fixture failed");
        NeedHub(b.Create(root/L"日本語", "日本語ゲーム",error),"Japanese fixture failed");
        const auto missing=root/L"moved/Moved.kubo";
        AddRecent(a.GetProjectFile());AddRecent(b.GetProjectFile());AddRecent(missing);
        const auto aBytes=ReadHubBytes(a.GetProjectFile()),bBytes=ReadHubBytes(b.GetProjectFile()),history=ReadHubBytes(g_recentFile);
        WNDCLASSW wc{};wc.lpfnWndProc=WindowProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"KuboHubWorkspaceChecks";
        NeedHub(RegisterClassW(&wc)!=0,"Window class failed");
        window=CreateWindowW(wc.lpszClassName,L"Workspace checks",WS_OVERLAPPEDWINDOW,0,0,1180,800,nullptr,nullptr,wc.hInstance,nullptr);
        NeedHub(window && g_recentList && g_hubRows.size()==3,"Production controls or missing project absent");
        MSG engineRefresh{};
        NeedHub(PeekMessageW(&engineRefresh,window,kRefreshEngineDetails,kRefreshEngineDetails,PM_REMOVE) &&
            engineRefresh.hwnd==window,"Engine details refresh did not reach the Hub window");
        DispatchMessageW(&engineRefresh);
        NeedHub(!g_hubRows.front().readable && !IsWindowEnabled(GetDlgItem(window,kLaunchButton)),"Missing project should be visible but cannot launch");
        SetWindowTextW(GetDlgItem(window,kHubSearch),L"ALPHA");
        NeedHub(g_hubRows.size()==1 && g_hubRows[0].name==L"Alpha Lab","Case-insensitive metadata search failed");
        NeedHub(IsWindowEnabled(GetDlgItem(window,kLaunchButton)),"Readable result cannot launch");
        SendMessageW(window,WM_COMMAND,kHubPin,0);
        NeedHub(ReadHubPins().size()==1 && g_hubRows[0].pinned,"Pin action did not persist");
        SetWindowTextW(GetDlgItem(window,kHubSearch),L"");
        NeedHub(g_hubRows.size()==3 && g_hubRows.front().name==L"Alpha Lab","Pin priority failed");
        SetWindowTextW(GetDlgItem(window,kHubSearch),L"Alpha");
        SelectHubProject(b.GetProjectFile());std::filesystem::path selected;
        NeedHub(GetSelectedProject(selected) && selected==b.GetProjectFile() && HubText(window,kHubSearch).empty(),"Opening/creating a project retained previous details or hid the opened project behind a filter");
        SelectHubProject(a.GetProjectFile());
        SendMessageW(window,WM_COMMAND,kHubRefresh,0);NeedHub(g_hubRows.front().pinned,"Pin not retained on refresh");
        SetWindowTextW(GetDlgItem(window,kHubSearch),L"日本語");NeedHub(g_hubRows.size()==1 && g_hubRows[0].name==L"日本語ゲーム","Unicode search failed");
        SetWindowTextW(GetDlgItem(window,kHubSearch),L"no matches");
        NeedHub(g_hubRows.empty() && !IsWindowEnabled(GetDlgItem(window,kLaunchButton)) && (GetWindowLongPtrW(GetDlgItem(window,kProjectEmpty),GWL_STYLE)&WS_VISIBLE),"Empty filtered state is stale");
        NeedHub(ReadHubBytes(g_recentFile)==history && ReadHubBytes(a.GetProjectFile())==aBytes && ReadHubBytes(b.GetProjectFile())==bBytes,"Search/pin changed history or project manifests");
        KuboHub::UpdateUrl feedUrl;
        NeedHub(KuboHub::ParseUpdateUrl(L"https://updates.example.test/kubo/latest.json",feedUrl),
            "HTTPS update source rejected");
        NeedHub(!KuboHub::ParseUpdateUrl(L"https://user:password@updates.example.test/feed.json",feedUrl),
            "Update URL accepted embedded credentials");
        const auto feed = crude_json::value::parse(R"({
            "format":"kubo-hub-update-1","platform":"Windows-x64","version":"1.10.0",
            "notes":"更新内容","manifestUrl":"https://updates.example.test/v1.10.0/engine-package.json",
            "manifestSha256":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
        })");
        using UpdateState = KuboHub::UpdateFeedResult::State;
        NeedHub(KuboHub::DecodeUpdateFeed(feed.dump(),"1.9.0",feedUrl).state == UpdateState::Available,
            "Dotted numeric version comparison did not offer a newer release");
        NeedHub(KuboHub::DecodeUpdateFeed(feed.dump(),"1.10.0.0",feedUrl).state == UpdateState::Current,
            "Equivalent dotted version was offered as an update");
        auto wrongOrigin = feed;
        wrongOrigin["manifestUrl"] = "https://other.example.test/engine-package.json";
        NeedHub(KuboHub::DecodeUpdateFeed(wrongOrigin.dump(),"1.9",feedUrl).state == UpdateState::Invalid,
            "Update could transfer trust to another host");
        auto wrongDigest = feed;
        wrongDigest["manifestSha256"] = "not-a-sha256";
        NeedHub(KuboHub::DecodeUpdateFeed(wrongDigest.dump(),"1.9",feedUrl).state == UpdateState::Invalid,
            "Malformed manifest digest accepted");
        const auto runChild = [&](const fs::path& path, bool expectReady) {
            KuboHub::EditorSession session;
            session.project = path;
            session.name = L"Owned Editor probe";
            NeedHub(KuboHub::StartEditor(fs::absolute(argv[0]),false,session) == ERROR_SUCCESS,
                "Cannot launch owned Editor readiness probe");
            g_editorSessions.push_back(std::move(session));
            const auto& owned = g_editorSessions.back();
            if (expectReady) {
                NeedHub(WaitForSingleObject(owned.ready.get(),5000)==WAIT_OBJECT_0,
                    "Ready event was not inherited by the child");
                PollEditorSessions(window);
                NeedHub(g_editorSessions.size()==1 && g_editorSessions[0].confirmed,
                    "Hub did not observe the readiness notification");
            }
            if (!g_editorSessions.empty()) {
                NeedHub(WaitForSingleObject(g_editorSessions[0].process.get(),5000)==WAIT_OBJECT_0,
                    "Owned Editor probe did not terminate");
                PollEditorSessions(window);
            }
            NeedHub(g_editorSessions.empty(),"Exited Editor retained handles");
            const auto status=HubText(window,kEditorSessionStatus);
            NeedHub(status.find(expectReady?L"エディタを閉じました":L"終了コード 19")!=std::wstring::npos,
                "Editor exit was reported as the wrong state");
        };
        runChild(a.GetProjectFile(),true);
        runChild(root/L"Failed.kubo",false);
        SetWindowTextW(GetDlgItem(window,kHubSearch),L"");SendMessageW(g_recentList,LB_SETCURSEL,0,0);SendMessageW(window,WM_COMMAND,kHubPin,0);
        NeedHub(ReadHubPins().empty(),"Unpin failed");
        SendMessageW(GetDlgItem(window,kHubSort),CB_SETCURSEL,1,0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(kHubSort,CBN_SELCHANGE),0);
        NeedHub(g_hubRows.front().name==L"Alpha Lab","Name sorting failed");
        SwitchHubPage(window,HubPage::Create);
        SetWindowTextW(GetDlgItem(window,kCreateName),L"試作 Game");SetWindowTextW(GetDlgItem(window,kCreateLocation),root.c_str());
        NeedHub(IsWindowEnabled(GetDlgItem(window,kCreateSubmit)),"Valid Unicode destination disabled");
        NeedHub(!fs::exists(root/L"試作 Game"),"Preview wrote project directory");
        SetWindowTextW(GetDlgItem(window,kCreateName),L"CON");NeedHub(!IsWindowEnabled(GetDlgItem(window,kCreateSubmit)),"Reserved name enabled");
        SetWindowTextW(GetDlgItem(window,kCreateName),L"alpha");NeedHub(!IsWindowEnabled(GetDlgItem(window,kCreateSubmit)),"Occupied output enabled");
        SetWindowTextW(GetDlgItem(window,kCreateName),L"試作 Game");SendMessageW(window,WM_COMMAND,kTemplateNative,0);
        NeedHub(g_hubNative && IsWindowEnabled(GetDlgItem(window,kCreateSdk)) && !IsWindowEnabled(GetDlgItem(window,kCreateSubmit)),"Native template accepted missing SDK");
        SendMessageW(window,WM_COMMAND,kTemplateBlank,0);NeedHub(!g_hubNative && IsWindowEnabled(GetDlgItem(window,kCreateSubmit)),"Blank template requires SDK");
        std::filesystem::path output;std::wstring message;
        for(const auto* name:{L"",L"NUL.txt",L"COM1",L"LPT9",L"../bad",L"bad.",L"bad ",L"a:b"})NeedHub(!HubCreationDestination(name,root.wstring(),false,L"",output,message),"Invalid name accepted");
        NeedHub(!HubCreationDestination(L"Game",L"relative",false,L"",output,message),"Relative parent accepted");
        NeedHub(HubCreationDestination(L"試作 Game",root.wstring(),false,L"",output,message) && output==root/L"試作 Game/試作 Game.kubo","Preview destination differs from submitted manifest");
        // Exercise the production layout at 150%, with no changes to system DPI.
        g_hubDpi=144;UpdateHubFonts(window);SetWindowPos(window,nullptr,0,0,1560,1110,SWP_NOMOVE|SWP_NOZORDER);LayoutHub(window);
        RECT client{},submit{};GetClientRect(window,&client);GetWindowRect(GetDlgItem(window,kCreateSubmit),&submit);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&submit),2);
        NeedHub(submit.left>=0 && submit.top>=0 && submit.right<=client.right && submit.bottom<=client.bottom,"Scaled primary action outside client");
        SwitchHubPage(window,HubPage::Engines);NeedHub((GetWindowLongPtrW(g_engineList,GWL_STYLE)&WS_VISIBLE) && !(GetWindowLongPtrW(g_recentList,GWL_STYLE)&WS_VISIBLE),"Page visibility leaks");
        const auto placementFile = root / L"profile/hub_window.txt";
        const KuboHub::WindowPlacement saved{{-2200, 120, -1000, 920}, 144, true};
        NeedHub(KuboHub::SaveWindowPlacement(placementFile, saved), "Window placement save failed");
        const auto restored = KuboHub::ReadWindowPlacement(placementFile);
        NeedHub(restored.Valid() && restored.maximized && restored.dpi == 144 &&
            EqualRect(&saved.normal, &restored.normal), "Window placement did not round trip");
        const RECT newWorkArea{0, 0, 1280, 720};
        const RECT fitted = KuboHub::FitWindowToWorkArea(restored, newWorkArea, 192);
        NeedHub(fitted.left == 0 && fitted.right <= newWorkArea.right &&
            fitted.top >= newWorkArea.top && fitted.bottom <= newWorkArea.bottom,
            "Removed monitor or higher DPI left the window outside the work area");
        { std::ofstream broken(placementFile); broken << "kubo-hub-window-1 -999999999 0 1 1 0 2"; }
        NeedHub(!KuboHub::ReadWindowPlacement(placementFile).Valid(), "Malformed placement was restored");
        for (UINT dpi : {96u, 120u, 144u, 192u}) {
            g_hubDpi = dpi;
            UpdateHubFonts(window);
            SetWindowPos(window,nullptr,0,0,HubPx(700),HubPx(400),SWP_NOMOVE|SWP_NOZORDER);
            SwitchHubPage(window, HubPage::Projects);
            NeedHub(MeasureHub(window).compact, "Narrow window retained side-by-side details");
            ScrollHub(window, SB_BOTTOM);
            GetClientRect(window, &client);
            GetWindowRect(GetDlgItem(window,kLaunchButton),&submit);
            MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&submit),2);
            NeedHub(submit.top >= 0 && submit.bottom <= client.bottom && submit.right <= client.right,
                "Compact project launch action is unreachable by scrolling");
            SwitchHubPage(window, HubPage::Create);
            ScrollHub(window, SB_BOTTOM);
            GetWindowRect(GetDlgItem(window,kCreateSubmit),&submit);
            MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&submit),2);
            NeedHub(submit.top >= 0 && submit.bottom <= client.bottom && submit.right <= client.right,
                "Compact creation action is unreachable by scrolling");
        }
        g_hubDpi = 96;
        UpdateHubFonts(window);
        SetWindowPos(window,nullptr,0,0,1100,800,SWP_NOMOVE|SWP_NOZORDER);
        SwitchHubPage(window,HubPage::Engines);
        const auto pageBar = GetDlgItem(window,kHubPageScroll);
        HubScrollGeometry bar;
        NeedHub(MeasureHubScrollbar(pageBar,true,bar) && bar.range > 0 && bar.travel > 0,
            "Scrollable page has no draggable thumb");
        const int grip = bar.thumbSize / 2;
        NeedHub(HubDragPosition(bar,bar.thumbTop+grip,grip) == bar.position &&
            HubDragPosition(bar,bar.track.bottom+50,grip) == bar.minimum+bar.range &&
            HubDragPosition(bar,bar.track.top-50,grip) == bar.minimum,
            "Drag jumps at its grab point or overshoots the track");
        const int middle = HubDragPosition(bar,bar.track.top+bar.arrow+bar.travel/2+grip,grip);
        SetHubScrollbarPosition(pageBar,true,bar,middle);
        NeedHub(g_hubScroll == middle && middle > 0 && middle < bar.range,
            "Page drag did not publish its absolute position");
        RECT navBefore{}, navAfter{};
        GetWindowRect(GetDlgItem(window,kNavProjects),&navBefore);
        ScrollHub(window,SB_BOTTOM);
        GetWindowRect(GetDlgItem(window,kNavProjects),&navAfter);
        NeedHub(g_hubScroll > 0 && EqualRect(&navBefore,&navAfter), "Navigation scrolled out of a tall Hub window");
        ScrollHub(window,SB_TOP);
        const auto engineChoice = SendMessageW(g_engineList,CB_GETCURSEL,0,0);
        POINT wheelPoint{HubPx(230),HubPx(125)};
        ClientToScreen(window,&wheelPoint);
        const LPARAM wheelAt = MAKELPARAM(wheelPoint.x,wheelPoint.y);
        UINT wheelLines = 3;
        SystemParametersInfoW(SPI_GETWHEELSCROLLLINES,0,&wheelLines,0);
        SendMessageW(window,WM_MOUSEWHEEL,MAKEWPARAM(0,-WHEEL_DELTA/2),wheelAt);
        NeedHub(g_hubScroll==0,"Partial wheel detent moved the page prematurely");
        SendMessageW(window,WM_MOUSEWHEEL,MAKEWPARAM(0,-WHEEL_DELTA/2),wheelAt);
        NeedHub((wheelLines==0 || g_hubScroll>0) && SendMessageW(g_engineList,CB_GETCURSEL,0,0)==engineChoice,
            "Wheel did not scroll the engine page or changed the selected editor");
        g_hubDpi = 120;
        UpdateHubFonts(window);
        LayoutHub(window);
        ScrollHub(window,SB_TOP);
        const HWND package = GetHubControl(window,kVerifyPackage);
        const HWND viewport = GetDlgItem(window,kHubEngineViewport);
        NeedHub(viewport && GetParent(g_engineList)==viewport && GetParent(package)==viewport,
            "Engine controls are not clipped by the content viewport");
        RECT before{}, after{};
        GetWindowRect(package,&before);
        SetHubPageScroll(window,1);
        GetWindowRect(package,&after);
        NeedHub(before.bottom-before.top==after.bottom-after.top &&
            before.top-after.top==HubPx(1),"Dragging resized a control instead of translating it");
        RECT bodyBefore{}, bodyAfter{}, combo{};
        GetWindowRect(viewport,&bodyBefore);
        ScrollHub(window,SB_BOTTOM);
        GetWindowRect(viewport,&bodyAfter);
        GetWindowRect(g_engineList,&combo);
        NeedHub(EqualRect(&bodyBefore,&bodyAfter) && combo.bottom<=bodyAfter.top,
            "Engine viewport moved with its content or did not clip the old controls");
        ScrollHub(window,SB_TOP);
        GetWindowRect(g_engineList,&combo);
        NeedHub(combo.top>=bodyAfter.top && combo.bottom<=bodyAfter.bottom,
            "Engine selector stayed outside the viewport after returning to the top");
        DestroyWindow(window);window=nullptr;UnregisterClassW(wc.lpszClassName,wc.hInstance);
        NeedHub(g_hubControls.empty() && !g_hubFont && !g_hubPanelBrush,"Presentation resources retained");
        std::cout<<"PASS: Hub workspace, placement persistence, removed-monitor fit and compact layout at simulated 100/125/150/200 percent; retained="<<root<<'\n';return 0;
    }catch(const std::exception& e){if(window)DestroyWindow(window);std::cerr<<e.what()<<'\n';return 1;}
}
