#pragma once
// Native Hub presentation, included inside main.cpp's private namespace.
// Project creation, catalog publication and package workers stay in their services.
enum class HubPage { Projects, Create, Engines, Guide, Import };
constexpr int kNavProjects=200,kNavCreate=201,kNavEngines=202,kNavGuide=203;
constexpr int kHubSearch=204,kHubSort=205,kHubPin=206,kHubForget=207,kHubFolder=208,kHubCopy=209,kHubRefresh=210;
constexpr int kTemplateBlank=211,kTemplateNative=212,kCreateName=213,kCreateLocation=214,kBrowseLocation=215;
constexpr int kCreateSdk=216,kBrowseSdk=217,kCreateSubmit=218,kCreateStatus=219,kCreatePreview=220,kProjectCount=221,kGuideText=222;
constexpr int kProjectEmpty=223;
constexpr int kEditorSessionStatus=224;
constexpr int kUpdateBadge=225,kUpdateSource=226,kCheckUpdates=227,kSkipUpdate=228,kLaterUpdate=229,kUpdateDetails=230;
constexpr int kDownloadUpdate=231;
constexpr int kToggleProjectView=232,kRelinkProject=233,kUseEditorForProject=234,kClearProjectEditor=235;
constexpr int kTemplateSprite=236;
constexpr int kNavImport=237,kImportUrl=238,kImportLocation=239,kImportName=240,kImportBrowse=241,kImportStart=242,kImportStatus=243;
constexpr int kHubPageScroll=244;
constexpr int kHubEngineViewport=245;
constexpr COLORREF kHubBackground=RGB(20,16,20),kHubSidebar=RGB(24,17,22),kHubPanel=RGB(32,24,30);
constexpr COLORREF kHubText=RGB(250,239,241),kHubMuted=RGB(189,168,177),kHubAccent=RGB(250,113,125),kHubBorder=RGB(64,44,54);
constexpr COLORREF kHubSelected=RGB(67,31,43),kHubSelectedBorder=RGB(149,61,79);
constexpr UINT_PTR kHubNavTimer = 120;
HubPage g_hubPage=HubPage::Projects;
UINT g_hubDpi=96;
HFONT g_hubFont=nullptr,g_hubTitleFont=nullptr,g_hubStrongFont=nullptr,g_hubSmallFont=nullptr;
HBRUSH g_hubPanelBrush=nullptr;
bool g_hubNative=false,g_hubPopulating=false;
bool g_hubSprite = false;
int g_hubScroll = 0;
constexpr int kHubFixedSidebarHeight = 696;
constexpr int kHubEngineBodyTop = 112;
constexpr int kHubEngineBodyBottomInset = 12;
constexpr int kHubCardHeight = 128;
bool g_hubDenseProjects = false;
struct HubControl {
    int id;
    HubPage page;
    bool shared;
    bool entry;
    RECT bounds{}, applied{};
};
std::vector<HubControl> g_hubControls;
struct HubProjectRow {std::wstring path,name,version;bool readable=false,pinned=false;};
std::vector<HubProjectRow> g_hubRows;
std::wstring g_hubReleaseLabel;
std::vector<KuboHub::ProjectPreview> g_hubPreviews;
std::future<std::vector<KuboHub::ProjectPreview>> g_hubPreviewWorker;
std::atomic_bool g_hubPreviewCancelled{false};
uint64_t g_hubPreviewRevision = 0, g_hubScanRevision = 0;
ULONGLONG g_hubNextScan = 0;
ULONG_PTR g_hubGraphics = 0;
HICON g_hubBrandIcon = nullptr;
HWND g_hubHoveredButton = nullptr;
float g_hubNavFrom = 0;
float g_hubNavFrame = 0;
ULONGLONG g_hubNavStarted = 0;
HWND g_hubWheelTarget = nullptr;
int g_hubWheelRemainder = 0;
void LayoutHub(HWND window);

HWND GetHubControl(HWND window,int id)
{
    if (const auto control = GetDlgItem(window,id)) return control;
    if (const auto viewport = GetDlgItem(window,kHubEngineViewport)) return GetDlgItem(viewport,id);
    return nullptr;
}

bool IsHubEngineChild(const HubControl& control)
{
    return control.page==HubPage::Engines && !control.shared && control.id!=kHubEngineViewport;
}

float HubNavPosition(ULONGLONG now)
{
    const float target = static_cast<float>(g_hubPage);
    if (!g_hubNavStarted) return target;
    const float t = (std::min)(1.0f,static_cast<float>(now-g_hubNavStarted)/210.0f);
    const float remaining = 1.0f-t;
    return target+(g_hubNavFrom-target)*remaining*remaining*remaining;
}

const KuboHub::ProjectPreview* FindHubPreview(std::wstring_view path)
{
    const auto found = std::find_if(g_hubPreviews.begin(), g_hubPreviews.end(),
        [&](const auto& preview) { return preview.project == path; });
    return found == g_hubPreviews.end() ? nullptr : &*found;
}

const KuboHub::ProjectPreview* SelectedHubPreview()
{
    std::filesystem::path path;
    return GetSelectedProject(path) ? FindHubPreview(path.native()) : nullptr;
}

int HubPreviewInset(bool compact)
{
    const auto* preview = SelectedHubPreview();
    if (!preview) return 0;
    return (preview->bgra ? (compact ? 120 : 152) : 0) + (preview->lastOpened.empty() ? 0 : 24);
}

void PollHubPreviews(HWND window)
{
    try {
        if (g_hubPreviewWorker.valid()) {
            if (g_hubPreviewWorker.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
            auto result = g_hubPreviewWorker.get();
            if (g_hubScanRevision == g_hubPreviewRevision) {
                const bool changed = result.size() != g_hubPreviews.size() ||
                    !std::equal(result.begin(), result.end(), g_hubPreviews.begin(), [](const auto& a, const auto& b) {
                        return a.project == b.project && a.bgra == b.bgra && a.lastOpened == b.lastOpened;
                    });
                g_hubPreviews = std::move(result);
                if (changed) {
                    InvalidateRect(g_recentList,nullptr,FALSE);
                    LayoutHub(window);
                }
            }
        }
        const auto now = GetTickCount64();
        if (g_hubPage != HubPage::Projects || IsIconic(window) || now < g_hubNextScan || g_hubRows.empty()) return;
        g_hubNextScan = now + 1500;
        std::vector<std::wstring> paths;
        for (const auto& row : g_hubRows) {
            if (row.readable) paths.push_back(row.path);
            if (paths.size() == 36) break;
        }
        g_hubScanRevision = g_hubPreviewRevision;
        g_hubPreviewWorker = std::async(std::launch::async, KuboHub::ScanProjectPreviews,
            std::move(paths), g_hubPreviews, std::cref(g_hubPreviewCancelled));
    } catch (...) {
        // Preview failure keeps the textual project list usable.
    }
}

int HubPx(int value){return MulDiv(value,static_cast<int>(g_hubDpi),96);}
std::wstring HubText(HWND control)
{
    const auto length=GetWindowTextLengthW(control);
    std::wstring text(static_cast<size_t>(length)+1,L'\0');
    GetWindowTextW(control,text.data(),length+1);text.resize(static_cast<size_t>(length));return text;
}
std::wstring HubText(HWND window,int id){return HubText(GetHubControl(window,id));}
bool HubSame(const std::wstring& a,const std::wstring& b)
{
    return CompareStringOrdinal(a.c_str(),-1,b.c_str(),-1,TRUE)==CSTR_EQUAL;
}
bool HubContains(const std::wstring& text,const std::wstring& query)
{
    if(query.empty())return true;
    if(query.size()>text.size())return false;
    for(size_t i=0;i+query.size()<=text.size();++i)
        if(CompareStringOrdinal(text.data()+i,static_cast<int>(query.size()),query.data(),static_cast<int>(query.size()),TRUE)==CSTR_EQUAL)return true;
    return false;
}
std::filesystem::path HubPinsFile(){return g_recentFile.parent_path()/L"hub_pins.json";}
std::vector<std::wstring> ReadHubPins()
{
    std::vector<std::wstring> pins;
    try {
        std::ifstream input(HubPinsFile(),std::ios::binary);std::string text;
        if(!input || !ReadBoundedRecentText(input,text))return pins;
        const auto data=crude_json::value::parse(text);if(!data.is_array())return pins;
        for(const auto& item:data.get<crude_json::array>()) {
            if(!item.is_string())continue;
            const auto& value=item.get<crude_json::string>();
            if(value.empty() || value.find('\0')!=std::string::npos || !MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0))continue;
            auto path=std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(value.data()),value.size())).wstring();
            if(path.size()<=kRecentPathLimit)pins.push_back(std::move(path));
            if(pins.size()==kHubPinLimit)break;
        }
    }catch(...){}return pins;
}
// Atomic replacement. Project files are never touched by pin/history operations.
bool SaveHubPaths(const std::filesystem::path& file,const std::vector<std::wstring>& paths)
{
    try {
        crude_json::value data(crude_json::array{});
        for(const auto& path:paths) {
            const auto value=std::filesystem::path(path).u8string();
            data.push_back(std::string(reinterpret_cast<const char*>(value.data()),value.size()));
        }
        std::string error;
        return Engine::Base::AtomicFile::Write(file,data.dump(2),kRecentTextLimit,error);
    }catch(...){return false;}
}
void RefreshHubActions(HWND window)
{
    std::filesystem::path selected;const bool has=GetSelectedProject(selected);
    Engine::Base::ProjectContext context;std::string error;
    const bool readable=has && context.Open(selected,error);
    EnableWindow(GetDlgItem(window,kLaunchButton),readable);
    for(auto id:{kHubPin,kHubForget,kHubCopy})EnableWindow(GetDlgItem(window,id),has);
    std::error_code status;
    EnableWindow(GetDlgItem(window,kHubFolder),has && std::filesystem::is_directory(selected.parent_path(),status));
    const auto pins=ReadHubPins();const auto path=selected.wstring();
    const bool pinned=std::any_of(pins.begin(),pins.end(),[&](const auto& p){return HubSame(p,path);});
    SetWindowTextW(GetDlgItem(window,kHubPin),pinned?L"ピン留めを解除":L"ピン留めする");
}
void PopulateRecent()
{
    if(!g_recentList)return;
    ++g_hubPreviewRevision;
    g_hubNextScan = 0;
    auto window=GetParent(g_recentList);std::filesystem::path previous;GetSelectedProject(previous);
    const auto query=HubText(window,kHubSearch);const auto pins=ReadHubPins();
    g_hubPopulating=true;SendMessageW(g_recentList,WM_SETREDRAW,FALSE,0);
    SendMessageW(g_recentList,LB_RESETCONTENT,0,0);g_hubRows.clear();
    for(const auto& path:ReadRecent()) {
        HubProjectRow row;row.path=path;row.name=std::filesystem::path(path).stem().wstring();
        Engine::Base::ProjectContext context;std::string error;row.readable=context.Open(path,error);
        if(row.readable){row.name=NativeCreationMessage(context.GetSettings().name);row.version=NativeCreationMessage(context.GetSettings().engineVersion);}
        row.pinned=std::any_of(pins.begin(),pins.end(),[&](const auto& pin){return HubSame(pin,path);});
        if(HubContains(row.name,query) || HubContains(path,query))g_hubRows.push_back(std::move(row));
    }
    const bool byName=SendMessageW(GetDlgItem(window,kHubSort),CB_GETCURSEL,0,0)==1;
    std::stable_sort(g_hubRows.begin(),g_hubRows.end(),[&](const auto& a,const auto& b){
        if(a.pinned!=b.pinned)return a.pinned;
        return byName && CompareStringOrdinal(a.name.c_str(),-1,b.name.c_str(),-1,TRUE)==CSTR_LESS_THAN;
    });
    int selected=0;
    for(size_t i=0;i<g_hubRows.size();++i) {
        const auto& row=g_hubRows[i];SendMessageW(g_recentList,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(row.path.c_str()));
        if(HubSame(row.path,previous.wstring()))selected=static_cast<int>(i);
    }
    if(!g_hubRows.empty())SendMessageW(g_recentList,LB_SETCURSEL,selected,0);
    const auto count=std::to_wstring(g_hubRows.size())+L" 件  /  ピン留めを先頭に表示";
    SetWindowTextW(GetDlgItem(window,kProjectCount),count.c_str());
    SendMessageW(g_recentList,WM_SETREDRAW,TRUE,0);InvalidateRect(g_recentList,nullptr,TRUE);
    g_hubPopulating=false;RefreshProjectDetails();RefreshHubActions(window);InvalidateRect(window,nullptr,FALSE);
    SetWindowTextW(GetDlgItem(window,kProjectEmpty),query.empty()?L"最初のプロジェクトを作りましょう\r\n\r\n「新規プロジェクト」から作成するか、\r\n「プロジェクトを開く」で .kubo を選択します。":L"一致するプロジェクトがありません\r\n\r\n名前やパスを変えて検索してください。\r\nEscキーで検索を解除できます。");
    LayoutHub(window);
}
void SelectHubProject(const std::filesystem::path& project)
{
    if(!g_recentList)return;
    auto index=SendMessageW(g_recentList,LB_FINDSTRINGEXACT,static_cast<WPARAM>(-1),reinterpret_cast<LPARAM>(project.c_str()));
    if(index==LB_ERR && !HubText(GetParent(g_recentList),kHubSearch).empty()) {
        SetWindowTextW(GetDlgItem(GetParent(g_recentList),kHubSearch),L"");
        index=SendMessageW(g_recentList,LB_FINDSTRINGEXACT,static_cast<WPARAM>(-1),reinterpret_cast<LPARAM>(project.c_str()));
    }
    if(index!=LB_ERR)SendMessageW(g_recentList,LB_SETCURSEL,index,0);
    RefreshProjectDetails();RefreshHubActions(GetParent(g_recentList));
    LayoutHub(GetParent(g_recentList));
}
void UpdateHubFonts(HWND window)
{
    const auto oldFont=g_hubFont,oldTitle=g_hubTitleFont,oldStrong=g_hubStrongFont,oldSmall=g_hubSmallFont;
    const auto make=[](int size,int weight){return CreateFontW(-HubPx(size),0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Yu Gothic UI");};
    g_hubFont=make(15,FW_NORMAL);g_hubTitleFont=make(28,FW_BOLD);g_hubStrongFont=make(16,FW_SEMIBOLD);g_hubSmallFont=make(13,FW_NORMAL);
    for(const auto& control:g_hubControls)SendMessageW(GetHubControl(window,control.id),WM_SETFONT,reinterpret_cast<WPARAM>(g_hubFont),TRUE);
    if(g_recentList)SendMessageW(g_recentList,LB_SETITEMHEIGHT,0,HubPx(g_hubDenseProjects?54:kHubCardHeight));
    if (g_hubBrandIcon) DestroyIcon(g_hubBrandIcon);
    const int brandSize = HubPx(48);
    g_hubBrandIcon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),
        MAKEINTRESOURCEW(IDI_KUBO_ENGINE),IMAGE_ICON,brandSize,brandSize,0));
    for (int id : {kHubSort,kEngineList}) {
        const auto combo = GetHubControl(window,id);
        SendMessageW(combo,CB_SETITEMHEIGHT,static_cast<WPARAM>(-1),HubPx(30));
        SendMessageW(combo,CB_SETITEMHEIGHT,0,HubPx(30));
    }
    for(auto font:{oldFont,oldTitle,oldStrong,oldSmall})if(font)DeleteObject(font);
}
void HubMove(HWND,int id,int x,int y,int width,int height,bool scrolls=true)
{
    const auto control = std::find_if(g_hubControls.begin(),g_hubControls.end(),
        [id](const HubControl& item) { return item.id == id; });
    if (control != g_hubControls.end()) {
        const int offset = scrolls?HubPx(g_hubScroll):0;
        control->bounds = {HubPx(x),HubPx(y)-offset,HubPx(x+(std::max)(1,width)),
            HubPx(y+(std::max)(1,height))-offset};
    }
}

void CommitHubLayout(HWND window,bool scrollable,const RECT& body,bool engineChildren)
{
    const HWND parent = engineChildren?GetDlgItem(window,kHubEngineViewport):window;
    if (!parent) return;
    struct Placement { HubControl* control; HWND child; RECT rect; UINT flags; bool visible; };
    std::vector<Placement> changes;
    changes.reserve(g_hubControls.size());
    for (auto& control : g_hubControls) {
        if (IsHubEngineChild(control)!=engineChildren) continue;
        const HWND child = GetDlgItem(parent,control.id);
        if (!child) continue;
        const bool visible = control.id != kNativeButton && (control.id != kHubPageScroll || scrollable) &&
            (control.id != kProjectEmpty || g_hubRows.empty()) && (control.shared || control.page == g_hubPage);
        const bool wasVisible = (GetWindowLongPtrW(child,GWL_STYLE)&WS_VISIBLE) != 0;
        auto rect = control.bounds;
        if (engineChildren) OffsetRect(&rect,-body.left,-body.top);
        if (control.entry) InflateRect(&rect,-HubPx(9),-HubPx(7));
        const bool moving = visible && !EqualRect(&rect,&control.applied);
        if (!moving && visible == wasVisible) continue;
        UINT flags = SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOREDRAW;
        if (visible != wasVisible) flags |= visible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW;
        if (!moving || (rect.left==control.applied.left && rect.top==control.applied.top)) flags |= SWP_NOMOVE;
        if (!moving || (rect.right-rect.left==control.applied.right-control.applied.left &&
            rect.bottom-rect.top==control.applied.bottom-control.applied.top)) flags |= SWP_NOSIZE;
        if (moving && control.page==HubPage::Engines && !control.shared) flags |= SWP_NOCOPYBITS;
        changes.push_back({&control,child,rect,flags,visible});
    }
    if (changes.empty()) return;
    auto batch = BeginDeferWindowPos(static_cast<int>(changes.size()));
    const auto apply = [&](bool deferred) {
        for (const auto& [control,child,rect,flags,visible] : changes) {
            if (deferred) {
                batch = DeferWindowPos(batch,child,nullptr,rect.left,rect.top,
                    rect.right-rect.left,rect.bottom-rect.top,flags);
                if (!batch) return false;
            } else {
                SetWindowPos(child,nullptr,rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,flags);
            }
        }
        return !deferred || EndDeferWindowPos(batch);
    };
    if (!batch || !apply(true)) apply(false);
    for (const auto& [control,child,rect,flags,visible] : changes) {
        if (!visible) continue;
        control->applied = rect;
        InvalidateRect(child,nullptr,FALSE);
    }
}

struct HubLayout {
    int width, viewportHeight, height, sidebar, x, right, middle, total;
    bool compact;
};

HubLayout MeasureHub(HWND window)
{
    RECT bounds{};
    GetClientRect(window,&bounds);
    const int width=MulDiv(bounds.right,96,static_cast<int>(g_hubDpi))-14;
    const int viewportHeight=MulDiv(bounds.bottom,96,static_cast<int>(g_hubDpi));
    const bool compact = width < 960;
    const int sidebar = compact ? 176 : 208;
    const int x = sidebar + 16;
    const int right = width - 304;
    const int total = width - x - 24;
    int minimumHeight = 690;
    if (g_hubPage == HubPage::Engines) minimumHeight = compact ? 1090 : 1050;
    if (compact) {
        if (g_hubPage == HubPage::Projects) minimumHeight = 824 + HubPreviewInset(true);
    }
    return {width, viewportHeight, (std::max)(viewportHeight, minimumHeight),
        sidebar, x, right, compact ? total : right-x-24, total, compact};
}

RECT HubEngineBody(const HubLayout& layout)
{
    return {HubPx(layout.x-8),HubPx(kHubEngineBodyTop),HubPx(layout.x+layout.total+8),
        HubPx(layout.viewportHeight-kHubEngineBodyBottomInset)};
}

void LayoutHub(HWND window)
{
    const auto layout = MeasureHub(window);
    const auto [width,viewportHeight,height,sidebar,x,right,middle,total,compact] = layout;
    if (viewportHeight <= 0 || width <= 0) return;
    g_hubScroll = std::clamp(g_hubScroll, 0, height - viewportHeight);
    const bool engines = g_hubPage==HubPage::Engines;
    const int railTop = engines?kHubEngineBodyTop:0;
    const int fixedHeight = engines?kHubEngineBodyTop+kHubEngineBodyBottomInset:0;
    HubMove(window,kHubPageScroll,width,railTop,14,viewportHeight-fixedHeight,false);
    HubMove(window,kHubEngineViewport,x-8,kHubEngineBodyTop,total+16,
        viewportHeight-kHubEngineBodyTop-kHubEngineBodyBottomInset,false);
    const bool fixedSidebar = viewportHeight >= kHubFixedSidebarHeight;
    const int sidebarHeight = fixedSidebar ? viewportHeight : height;
    const int navWidth = sidebar - 32;
    for (const auto [index,id] : std::array<std::pair<int,int>,5>{{
        {0,kNavProjects},{1,kNavCreate},{2,kNavEngines},{3,kNavGuide},{4,kNavImport}}})
        HubMove(window,id,14,126+52*index,navWidth+4,52,!fixedSidebar);
    HubMove(window,kOpenButton,16,388,navWidth,40,!fixedSidebar);
    HubMove(window,kExitButton,16,sidebarHeight-66,navWidth,38,!fixedSidebar);
    HubMove(window,kEditorSessionStatus,16,436,navWidth,106,!fixedSidebar);
    HubMove(window,kUpdateBadge,16,548,navWidth,32,!fixedSidebar);
    HubMove(window,kHubSearch,x,112,middle-132,36);HubMove(window,kHubSort,x+middle-124,112,124,160);
    const int listHeight = compact ? 210 : height - 280;
    HubMove(window,kProjectCount,x,158,middle,24);HubMove(window,kRecentList,x,194,middle,listHeight);
    HubMove(window,kProjectEmpty,x+24,220,middle-48,160);
    const int listFooter = compact ? 414 : height - 68;
    const int footerWidth = (middle-16)/3;
    HubMove(window,kHubRefresh,x,listFooter,footerWidth,36);
    HubMove(window,kNewButton,x+footerWidth+8,listFooter,footerWidth,36);
    HubMove(window,kToggleProjectView,x+2*(footerWidth+8),listFooter,footerWidth,36);
    const int detailX = compact ? x : right + 16;
    const int detailWidth = compact ? total : 256;
    const int actionWidth = (detailWidth - 8) / 2;
    const int previewInset = HubPreviewInset(compact);
    const int actionsY = compact ? 658 + previewInset : height - 218;
    HubMove(window,kProjectDetails,detailX,(compact?490:162)+previewInset,detailWidth,compact?156:height-396-previewInset);
    HubMove(window,kHubFolder,detailX,actionsY,actionWidth,36);HubMove(window,kHubCopy,detailX+actionWidth+8,actionsY,actionWidth,36);
    HubMove(window,kHubPin,detailX,actionsY+44,actionWidth,36);HubMove(window,kHubForget,detailX+actionWidth+8,actionsY+44,actionWidth,36);
    HubMove(window,kLaunchButton,detailX,compact?754+previewInset:height-112,detailWidth,44);
    const int templateWidth = (middle-16)/3;
    HubMove(window,kTemplateBlank,x,138,templateWidth,108);
    HubMove(window,kTemplateSprite,x+templateWidth+8,138,templateWidth,108);
    HubMove(window,kTemplateNative,x+2*(templateWidth+8),138,templateWidth,108);
    HubMove(window,kCreateName,x,290,middle,38);HubMove(window,kCreateLocation,x,374,middle-90,38);HubMove(window,kBrowseLocation,x+middle-78,374,78,38);
    HubMove(window,kCreateSdk,x,458,middle-90,38);HubMove(window,kBrowseSdk,x+middle-78,458,78,38);
    HubMove(window,kCreatePreview,x,516,middle,56);HubMove(window,kCreateStatus,x,580,middle,52);
    HubMove(window,kCreateSubmit,detailX,compact?636:height-112,detailWidth,44);
    const int engineActionWidth = (std::min)(220,(total-8)/2);
    HubMove(window,kEngineList,x,151,total,240);HubMove(window,kAddEngine,x,198,engineActionWidth,38);HubMove(window,kRemoveEngine,x+engineActionWidth+8,198,engineActionWidth,38);
    HubMove(window,kEngineDetails,x,252,total,100);
    if (compact) {
        HubMove(window,kVerifyPackage,x,402,engineActionWidth,38);
        HubMove(window,kInstallPackage,x+engineActionWidth+8,402,engineActionWidth,38);
        HubMove(window,kRecoverInstallation,x,446,total,38);
    } else {
        HubMove(window,kVerifyPackage,x,402,200,38);HubMove(window,kInstallPackage,x+212,402,180,38);
        HubMove(window,kRecoverInstallation,x+404,402,190,38);
    }
    const int packageBottom = height - 310;
    const int resultY = compact ? 498 : 458;
    HubMove(window,kVerificationDetails,x,resultY,total,packageBottom-resultY-90);
    const int cancelWidth = compact ? 126 : 180;
    HubMove(window,kCancelVerification,x,packageBottom-74,cancelWidth,38);
    HubMove(window,kSelectInstalled,x+cancelWidth+8,packageBottom-74,total-cancelWidth-8,38);
    HubMove(window,kUpdateSource,x,packageBottom+48,total,34);
    const int updateActionWidth = (total-16)/3;
    HubMove(window,kCheckUpdates,x,packageBottom+92,updateActionWidth,36);
    HubMove(window,kLaterUpdate,x+updateActionWidth+8,packageBottom+92,updateActionWidth,36);
    HubMove(window,kSkipUpdate,x+2*(updateActionWidth+8),packageBottom+92,updateActionWidth,36);
    HubMove(window,kUpdateDetails,x,packageBottom+140,total,94);
    HubMove(window,kDownloadUpdate,x,packageBottom+246,total,36);
    HubMove(window,kGuideText,x,140,total,height-202);
    HubMove(window,kImportUrl,x,148,total,36);
    HubMove(window,kImportLocation,x,228,total-90,36);
    HubMove(window,kImportBrowse,x+total-78,228,78,36);
    HubMove(window,kImportName,x,308,total,36);
    HubMove(window,kImportStart,x,364,(std::min)(total,280),40);
    HubMove(window,kImportStatus,x,426,total,height-474);
    const auto body = HubEngineBody(layout);
    CommitHubLayout(window,height>viewportHeight,body,false);
    CommitHubLayout(window,height>viewportHeight,body,true);
    if (engines) InvalidateRect(GetDlgItem(window,kHubEngineViewport),nullptr,FALSE);
    if (const HWND scrollbar = GetDlgItem(window,kHubPageScroll)) {
        SCROLLINFO scroll{sizeof(scroll), SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL};
        scroll.nMax = height-fixedHeight-1;
        scroll.nPage = static_cast<UINT>((std::max)(1,viewportHeight-fixedHeight));
        scroll.nPos = g_hubScroll;
        SetScrollInfo(scrollbar, SB_CTL, &scroll, FALSE);
        InvalidateRect(scrollbar,nullptr,FALSE);
    }
    EnableWindow(GetDlgItem(window,kCreateSdk),g_hubNative);EnableWindow(GetDlgItem(window,kBrowseSdk),g_hubNative);
    RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_NOCHILDREN|RDW_NOERASE);
}

void SetHubPageScroll(HWND window,int next)
{
    const auto layout = MeasureHub(window);
    next = std::clamp(next,0,layout.height-layout.viewportHeight);
    if (next == g_hubScroll) return;
    g_hubScroll = next;
    LayoutHub(window);
    if (g_hubPage==HubPage::Engines) {
        RECT repaint = HubEngineBody(layout);
        RECT client{};
        GetClientRect(window,&client);
        repaint.right = client.right;
        RedrawWindow(window,&repaint,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_NOERASE|RDW_UPDATENOW);
    }
}

void ScrollHub(HWND window, unsigned request)
{
    const auto layout = MeasureHub(window);
    int next = g_hubScroll;
    switch (request) {
    case SB_LINEUP: next -= 32; break;
    case SB_LINEDOWN: next += 32; break;
    case SB_PAGEUP: next -= layout.viewportHeight; break;
    case SB_PAGEDOWN: next += layout.viewportHeight; break;
    case SB_TOP: next = 0; break;
    case SB_BOTTOM: next = layout.height - layout.viewportHeight; break;
    default: return;
    }
    SetHubPageScroll(window,next);
}

void RevealHubFocus(HWND window)
{
    const HWND focus = GetFocus();
    if (!focus || !IsChild(window,focus)) return;
    RECT bounds{};
    if (!GetWindowRect(focus, &bounds)) return;
    MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&bounds), 2);
    const auto layout = MeasureHub(window);
    const auto control = std::find_if(g_hubControls.begin(),g_hubControls.end(),
        [focus](const HubControl& item) { return item.id==GetDlgCtrlID(focus); });
    const bool inEngineBody = g_hubPage==HubPage::Engines && control!=g_hubControls.end() && !control->shared;
    const int firstY = inEngineBody?kHubEngineBodyTop:0;
    const int lastY = layout.viewportHeight-(inEngineBody?kHubEngineBodyBottomInset:0);
    const int top = MulDiv(bounds.top, 96, static_cast<int>(g_hubDpi));
    const int bottom = MulDiv(bounds.bottom, 96, static_cast<int>(g_hubDpi));
    if (top < firstY || top >= lastY) g_hubScroll += top-firstY-8;
    else if (bottom > lastY && bottom-top < lastY-firstY)
        g_hubScroll += bottom-lastY+8;
    else return;
    LayoutHub(window);
}

// Advisory validation performs reads only. The staged service rechecks at commit.
bool HubCreationDestination(const std::wstring& name,const std::wstring& location,bool native,const std::wstring& sdk,std::filesystem::path& manifest,std::wstring& message)
{
    manifest.clear();
    if(name.empty()){message=L"プロジェクト名を入力してください。";return false;}
    const int utf8=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,name.data(),static_cast<int>(name.size()),nullptr,0,nullptr,nullptr);
    if(utf8<=0 || utf8>240 || name.find_first_of(L"\\/:<>\"|?*")!=std::wstring::npos || name.back()==L'.' || name.back()==L' ' || std::any_of(name.begin(),name.end(),[](wchar_t c){return c<32;})) {
        message=L"名前に使用できない文字、末尾の空白・ドット、または長すぎる名前です。";return false;
    }
    const auto base=name.substr(0,name.find(L'.'));
    if(HubSame(base,L"CON") || HubSame(base,L"PRN") || HubSame(base,L"AUX") || HubSame(base,L"NUL") ||
        (base.size()==4 && (HubSame(base.substr(0,3),L"COM") || HubSame(base.substr(0,3),L"LPT")) && base[3]>=L'1' && base[3]<=L'9')) {
        message=L"Windowsの予約名はプロジェクト名に使用できません。";return false;
    }
    try {
        const std::filesystem::path parent(location);
        if(!parent.is_absolute() || location.find(L'\0')!=std::wstring::npos){message=L"保存先には絶対パスを指定してください。";return false;}
        const auto root=(parent/name).lexically_normal();manifest=root/(name+L".kubo");
        if(std::filesystem::exists(root) && (!std::filesystem::is_directory(root) || !std::filesystem::is_empty(root))) {
            message=L"作成先は使用中です。別の名前または空のフォルダーを指定してください。";return false;
        }
        for(auto folder=root;!folder.empty();folder=folder.parent_path()) {
            const auto attr=GetFileAttributesW(folder.c_str());
            if(attr!=INVALID_FILE_ATTRIBUTES && ((attr&FILE_ATTRIBUTE_REPARSE_POINT) || !(attr&FILE_ATTRIBUTE_DIRECTORY))) {
                message=L"保存先には通常のフォルダーを指定してください（リンクは使用できません）。";return false;
            }
            if(folder==folder.root_path())break;
        }
        if(native && !std::filesystem::is_regular_file(std::filesystem::path(sdk)/L"sdk-package.json")) {
            message=L"C++プロジェクトにはRuntime SDKのフォルダーを指定してください。";return false;
        }
        message=L"この場所に作成できます。作成後にエディタを開きます。";return true;
    }catch(...){message=L"保存先を確認できません。パスとアクセス権を確認してください。";return false;}
}
void RefreshHubCreation(HWND window)
{
    std::filesystem::path manifest;std::wstring message;
    const bool valid=HubCreationDestination(HubText(window,kCreateName),HubText(window,kCreateLocation),g_hubNative,HubText(window,kCreateSdk),manifest,message);
    SetWindowTextW(GetDlgItem(window,kCreateStatus),message.c_str());
    const auto preview=L"作成先  /  "+manifest.wstring();SetWindowTextW(GetDlgItem(window,kCreatePreview),preview.c_str());
    EnableWindow(GetDlgItem(window,kCreateSubmit),valid);
}
void TickHubNavigation(HWND window);

void SwitchHubPage(HWND window,HubPage page)
{
    if (g_hubPage != page) {
        const auto now = GetTickCount64();
        g_hubNavFrom = HubNavPosition(now);
        BOOL animations = FALSE;
        SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animations,0);
        g_hubNavStarted = animations && SetTimer(window,kHubNavTimer,16,nullptr) ? now : 0;
        g_hubScroll = 0;
        g_hubWheelTarget = nullptr;
        g_hubWheelRemainder = 0;
    }
    g_hubPage=page;LayoutHub(window);
    TickHubNavigation(window);
    if(page==HubPage::Create)RefreshHubCreation(window);
    if(page==HubPage::Projects)RefreshHubActions(window);
    const auto focus=page==HubPage::Projects?kHubSearch:page==HubPage::Create?kCreateName:
        page==HubPage::Engines?kEngineList:page==HubPage::Import?kImportUrl:kGuideText;
    SetFocus(GetHubControl(window,focus));
    RevealHubFocus(window);
}
void HubBrowseFolder(HWND window,int target)
{
    const HRESULT initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    IFileDialog* dialog=nullptr;
    if(SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)))) {
        DWORD flags=0;dialog->GetOptions(&flags);dialog->SetOptions(flags|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST|FOS_NOCHANGEDIR);
        dialog->SetTitle(target==kCreateSdk?L"Runtime SDKのフォルダーを選択":L"プロジェクトを保存する親フォルダーを選択");
        if(SUCCEEDED(dialog->Show(window))) {
            IShellItem* item=nullptr;if(SUCCEEDED(dialog->GetResult(&item))) {
                PWSTR path=nullptr;if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&path))) {SetWindowTextW(GetDlgItem(window,target),path);CoTaskMemFree(path);}item->Release();
            }
        }dialog->Release();
    }
    if(SUCCEEDED(initialized))CoUninitialize();RefreshHubCreation(window);
}
void HubCopyPath(HWND window,const std::filesystem::path& path)
{
    const auto text=path.wstring();const auto bytes=(text.size()+1)*sizeof(wchar_t);
    HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,bytes);if(!memory)return;
    void* data=GlobalLock(memory);if(!data){GlobalFree(memory);return;}memcpy(data,text.c_str(),bytes);GlobalUnlock(memory);
    if(OpenClipboard(window)) {if(EmptyClipboard() && SetClipboardData(CF_UNICODETEXT,memory))memory=nullptr;CloseClipboard();}
    if(memory)GlobalFree(memory);
}
bool HandleHubCommand(HWND window,int id,int notification)
{
    if(id>=kNavProjects && id<=kNavGuide){SwitchHubPage(window,static_cast<HubPage>(id-kNavProjects));return true;}
    if(id==kNavImport){SwitchHubPage(window,HubPage::Import);return true;}
    if(id==kImportBrowse){HubBrowseFolder(window,kImportLocation);return true;}
    if(id==kHubSearch && notification==EN_CHANGE){if(!g_hubPopulating)PopulateRecent();return true;}
    if(id==kHubSort && notification==CBN_SELCHANGE){PopulateRecent();return true;}
    if(id==kHubRefresh){PopulateRecent();return true;}
    if (id == kToggleProjectView) {
        g_hubDenseProjects = !g_hubDenseProjects;
        SetWindowTextW(GetDlgItem(window,id),g_hubDenseProjects?L"カード表示":L"コンパクト表示");
        SendMessageW(g_recentList,LB_SETITEMHEIGHT,0,HubPx(g_hubDenseProjects?54:kHubCardHeight));
        LayoutHub(window);
        return true;
    }
    if (id == kUseEditorForProject || id == kClearProjectEditor) {
        std::filesystem::path project, editor;
        std::string error;
        if (!GetSelectedProject(project)) return true;
        const auto root = project.parent_path();
        if (!Engine::Base::PreviewDirectoriesAllowed(root,true) ||
            (id == kUseEditorForProject && !ResolveSelectedEditor(editor,error)) ||
            !Engine::Base::AtomicFile::Write(root/L"UserSettings/Hub/editor.txt",
                Engine::Base::PathToUtf8(editor),kRecentTextLimit,error)) {
            MessageBoxW(window,L"プロジェクトのエディタ指定を保存できませんでした。",L"KuboEngine Hub",MB_OK|MB_ICONERROR);
        }
        RefreshProjectDetails();
        return true;
    }
    if (id == kRelinkProject) {
        std::filesystem::path selected,replacement;
        Engine::Base::ProjectContext project;
        std::string error;
        if (!GetSelectedProject(selected) || !ChooseProjectFile(replacement,false)) return true;
        if (!project.Open(replacement,error)) {
            MessageBoxW(window,NativeCreationMessage(error).c_str(),L"移動先を指定",MB_OK|MB_ICONERROR);
            return true;
        }
        auto lock = LockHubHistory();
        if (!lock) {
            MessageBoxW(window,L"別のHubが履歴を更新しています。",L"KuboEngine Hub",MB_OK|MB_ICONERROR);
            return true;
        }
        auto entries = ReadRecent();
        auto pins = ReadHubPins();
        const auto next = project.GetProjectFile().wstring();
        const auto replace = [&](std::vector<std::wstring>& paths) {
            bool changed = false;
            for (auto& path : paths) {
                if (HubSame(path,selected.native())) { path = next; changed = true; }
            }
            if (!changed) return false;
            bool seen = false;
            std::erase_if(paths,[&](const auto& path) {
                if (!HubSame(path,next)) return false;
                return std::exchange(seen,true);
            });
            return true;
        };
        if (!replace(entries) || !SaveHubPaths(g_recentFile,entries)) {
            MessageBoxW(window,L"移動先を履歴に保存できませんでした。",L"KuboEngine Hub",MB_OK|MB_ICONERROR);
            return true;
        }
        if (replace(pins) && !SaveHubPaths(HubPinsFile(),pins))
            MessageBoxW(window,L"履歴を更新しました。ピン留めを設定し直してください。",L"KuboEngine Hub",MB_OK|MB_ICONINFORMATION);
        lock.reset();
        PopulateRecent();
        SelectHubProject(project.GetProjectFile());
        return true;
    }
    if(id==kTemplateBlank || id==kTemplateNative || id==kTemplateSprite){
        g_hubNative=id==kTemplateNative;
        g_hubSprite=id==kTemplateSprite;
        for (int templateId : {kTemplateBlank,kTemplateNative,kTemplateSprite})
            InvalidateRect(GetDlgItem(window,templateId),nullptr,FALSE);
        LayoutHub(window);RefreshHubCreation(window);return true;
    }
    if((id==kCreateName || id==kCreateLocation || id==kCreateSdk) && notification==EN_CHANGE){RefreshHubCreation(window);return true;}
    if(id==kBrowseLocation || id==kBrowseSdk){HubBrowseFolder(window,id==kBrowseSdk?kCreateSdk:kCreateLocation);return true;}
    if(id==kCreateSubmit) {
        std::filesystem::path manifest;std::wstring message;
        if(!HubCreationDestination(HubText(window,kCreateName),HubText(window,kCreateLocation),g_hubNative,HubText(window,kCreateSdk),manifest,message)){RefreshHubCreation(window);return true;}
        Engine::Base::ProjectContext context;std::string error;
        const bool created=g_hubNative?Engine::Services::NativeProjectCreate::Create(manifest,HubText(window,kCreateSdk),context,error):
            Engine::Services::ProjectCreate::Create(manifest,context,error,g_hubSprite
                ? Engine::Services::ProjectStarter::Sprite2D : Engine::Services::ProjectStarter::Empty);
        if(!created){SetWindowTextW(GetDlgItem(window,kCreateStatus),(L"作成できませんでした: "+NativeCreationMessage(error)).c_str());return true;}
        if(!error.empty())MessageBoxW(window,NativeCreationMessage(error).c_str(),L"プロジェクト作成のお知らせ",MB_OK|MB_ICONINFORMATION);
        AddRecent(context.GetProjectFile());SetWindowTextW(GetDlgItem(window,kHubSearch),L"");PopulateRecent();SelectHubProject(context.GetProjectFile());SwitchHubPage(window,HubPage::Projects);
        LaunchProject(context.GetProjectFile());return true;
    }
    if(id==kHubPin || id==kHubForget || id==kHubFolder || id==kHubCopy) {
        std::filesystem::path selected;if(!GetSelectedProject(selected))return true;
        if(id==kHubCopy){HubCopyPath(window,selected);return true;}
        if(id==kHubFolder){const auto result=reinterpret_cast<INT_PTR>(ShellExecuteW(window,L"explore",selected.parent_path().c_str(),nullptr,nullptr,SW_SHOWNORMAL));if(result<=32)MessageBoxW(window,L"フォルダーを開けませんでした。",L"KuboEngine Hub",MB_OK|MB_ICONERROR);return true;}
        if(id==kHubPin) {
            auto lock = LockHubHistory();
            if (!lock) return true;
            auto pins=ReadHubPins();const auto found=std::find_if(pins.begin(),pins.end(),[&](const auto& p){return HubSame(p,selected.wstring());});
            if(found==pins.end()){
                if(pins.size()==kHubPinLimit){
                    MessageBoxW(window,L"ピン留めは12件までです。別のピンを解除してから追加してください。",L"KuboEngine Hub",MB_OK|MB_ICONINFORMATION);
                    return true;
                }
                pins.insert(pins.begin(),selected.wstring());
            }else pins.erase(found);
            if(!SaveHubPaths(HubPinsFile(),pins))MessageBoxW(window,L"ピン留めを保存できませんでした。",L"KuboEngine Hub",MB_OK|MB_ICONERROR);
        }else {
            if(MessageBoxW(window,L"最近の一覧からこの項目を外しますか？\nプロジェクトのファイルは削除されません。",L"一覧から外す",MB_YESNO|MB_ICONQUESTION)!=IDYES)return true;
            auto lock = LockHubHistory();
            if (!lock) return true;
            auto entries=ReadRecent();std::erase_if(entries,[&](const auto& p){return HubSame(p,selected.wstring());});
            if(!SaveHubPaths(g_recentFile,entries))MessageBoxW(window,L"一覧を保存できませんでした。",L"KuboEngine Hub",MB_OK|MB_ICONERROR);
        }PopulateRecent();return true;
    }
    return false;
}

void HubFill(HDC dc,RECT rect,COLORREF color)
{
    const auto brush=CreateSolidBrush(color);FillRect(dc,&rect,brush);DeleteObject(brush);
}
Gdiplus::Color HubColor(COLORREF color, BYTE alpha = 255)
{
    return {alpha,GetRValue(color),GetGValue(color),GetBValue(color)};
}

void HubSurface(HDC dc, RECT bounds, int radius, COLORREF fill, COLORREF border)
{
    if (bounds.right <= bounds.left || bounds.bottom <= bounds.top) return;
    if (!g_hubGraphics) { HubFill(dc,bounds,fill); return; }
    using namespace Gdiplus;
    const auto x = static_cast<REAL>(bounds.left), y = static_cast<REAL>(bounds.top);
    const auto w = static_cast<REAL>(bounds.right-bounds.left-1), h = static_cast<REAL>(bounds.bottom-bounds.top-1);
    const REAL d = (std::min)({static_cast<REAL>(HubPx(radius*2)),w,h});
    if (d <= 0) return;
    GraphicsPath shape;
    shape.AddArc(x,y,d,d,180,90);
    shape.AddArc(x+w-d,y,d,d,270,90);
    shape.AddArc(x+w-d,y+h-d,d,d,0,90);
    shape.AddArc(x,y+h-d,d,d,90,90);
    shape.CloseFigure();
    Graphics graphics(dc);
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);
    SolidBrush brush(HubColor(fill));
    Pen line(HubColor(border),1.0f);
    graphics.FillPath(&brush,&shape);
    graphics.DrawPath(&line,&shape);
}

void DrawHubGeometry(HDC dc,RECT bounds,int sidebar)
{
    if (!g_hubGraphics || bounds.bottom <= 0 || bounds.right <= sidebar) return;
    using namespace Gdiplus;
    const int h = (std::min)(static_cast<int>(bounds.bottom),HubPx(540));
    Graphics graphics(dc);
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);
    const Rect area(sidebar,0,bounds.right-sidebar,h);
    LinearGradientBrush wash(area,HubColor(RGB(47,22,33)),HubColor(kHubBackground),LinearGradientModeVertical);
    graphics.FillRectangle(&wash,area);
    const auto centerX = static_cast<REAL>(bounds.right-HubPx(126)), centerY = static_cast<REAL>(HubPx(86));
    for (int i = 0; i < 5; ++i) {
        const auto r = static_cast<REAL>(HubPx(80+43*i));
        const PointF points[] = {
            {centerX,centerY-r*0.58f},{centerX+r,centerY},
            {centerX,centerY+r*0.58f},{centerX-r,centerY}
        };
        Pen line(HubColor(kHubAccent,static_cast<BYTE>(38-i*6)),1.0f);
        graphics.DrawPolygon(&line,points,4);
    }
    Pen divider(HubColor(kHubBorder),1.0f);
    graphics.DrawLine(&divider,sidebar,0,sidebar,bounds.bottom);
}

int HubNavIndex(int id)
{
    if (id == kNavImport) return 4;
    return id >= kNavProjects && id <= kNavGuide ? id-kNavProjects : -1;
}

void DrawHubNavFrame(HDC dc,int width,int xOffset,int yOffset)
{
    const int y = MulDiv(static_cast<int>((130+52*g_hubNavFrame)*100),static_cast<int>(g_hubDpi),9600);
    RECT frame{xOffset+HubPx(16),y+yOffset,xOffset+width-HubPx(16),y+yOffset+HubPx(44)};
    RECT halo = frame;
    InflateRect(&halo,HubPx(2),HubPx(2));
    HubSurface(dc,halo,12,RGB(43,23,32),RGB(62,31,42));
    HubSurface(dc,frame,10,kHubSelected,kHubSelectedBorder);
    HubSurface(dc,{frame.left+HubPx(3),frame.top+HubPx(11),frame.left+HubPx(6),frame.bottom-HubPx(11)},
        2,kHubAccent,kHubAccent);
}

void DrawHubNavGlyph(HDC dc,int index,int x,int y,COLORREF color)
{
    if (!g_hubGraphics) return;
    using namespace Gdiplus;
    Graphics graphics(dc);
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);
    graphics.TranslateTransform(static_cast<REAL>(x),static_cast<REAL>(y));
    const float scale = static_cast<float>(g_hubDpi)/96.0f;
    graphics.ScaleTransform(scale,scale);
    Pen line(HubColor(color),1.35f);
    switch (index) {
    case 0:
        for (int row = 0; row < 2; ++row)
            for (int col = 0; col < 2; ++col) graphics.DrawRectangle(&line,2+col*8,2+row*8,5,5);
        break;
    case 1:
        graphics.DrawRectangle(&line,2,5,15,11);
        graphics.DrawLine(&line,3,3,8,3);
        graphics.DrawLine(&line,6,10,13,10);
        graphics.DrawLine(&line,9,7,9,13);
        break;
    case 2: {
        const Point points[] = {{9,1},{17,6},{17,13},{9,18},{1,13},{1,6}};
        graphics.DrawPolygon(&line,points,6);
        graphics.DrawLine(&line,1,6,9,11);
        graphics.DrawLine(&line,17,6,9,11);
        graphics.DrawLine(&line,9,11,9,18);
        break;
    }
    case 3:
        graphics.DrawRectangle(&line,3,2,12,15);
        graphics.DrawLine(&line,6,6,12,6);
        graphics.DrawLine(&line,6,10,12,10);
        break;
    case 4:
        graphics.DrawLine(&line,5,6,5,13);
        graphics.DrawLine(&line,5,11,13,6);
        graphics.DrawEllipse(&line,3,2,4,4);
        graphics.DrawEllipse(&line,3,13,4,4);
        graphics.DrawEllipse(&line,11,2,4,4);
        break;
    }
}

void TickHubNavigation(HWND window)
{
    const auto now = GetTickCount64();
    if (!g_hubNavStarted || now-g_hubNavStarted >= 210 || IsIconic(window)) {
        KillTimer(window,kHubNavTimer);
        g_hubNavStarted = 0;
    }
    g_hubNavFrame = HubNavPosition(now);
    for (int id : {kNavProjects,kNavCreate,kNavEngines,kNavGuide,kNavImport})
        if (const auto button = GetDlgItem(window,id)) InvalidateRect(button,nullptr,FALSE);
}

void HubLabel(HDC dc,const std::wstring& text,RECT rect,HFONT font,COLORREF color,UINT flags=DT_LEFT|DT_TOP|DT_NOPREFIX|DT_WORDBREAK)
{
    const auto old=SelectObject(dc,font);SetTextColor(dc,color);SetBkMode(dc,TRANSPARENT);DrawTextW(dc,text.c_str(),static_cast<int>(text.size()),&rect,flags);SelectObject(dc,old);
}
void HubLabelAt(HDC dc,const std::wstring& text,int x,int y,int width,int height,HFONT font,COLORREF color)
{
    HubLabel(dc,text,{HubPx(x),HubPx(y),HubPx(x+width),HubPx(y+height)},font,color);
}

std::wstring HubComboText(HWND window,int index)
{
    const auto length = SendMessageW(window,CB_GETLBTEXTLEN,index,0);
    if (length == CB_ERR || length > 32768) return {};
    std::wstring text(static_cast<size_t>(length)+1,L'\0');
    SendMessageW(window,CB_GETLBTEXT,index,reinterpret_cast<LPARAM>(text.data()));
    text.resize(static_cast<size_t>(length));
    return text;
}

struct HubScrollGeometry {
    RECT track{};
    int arrow = 0, thumbTop = 0, thumbSize = 0, travel = 0;
    int minimum = 0, position = 0, range = 0, page = 0;
};

bool MeasureHubScrollbar(HWND window,bool pageBar,HubScrollGeometry& geometry)
{
    if (pageBar) {
        if (!GetClientRect(window,&geometry.track)) return false;
        geometry.arrow = HubPx(14);
    } else {
        SCROLLBARINFO bar{sizeof(bar)};
        RECT outer{};
        if (!GetScrollBarInfo(window,OBJID_VSCROLL,&bar) ||
            (bar.rgstate[0] & (STATE_SYSTEM_INVISIBLE|STATE_SYSTEM_OFFSCREEN)) ||
            !GetWindowRect(window,&outer)) return false;
        geometry.track = bar.rcScrollBar;
        OffsetRect(&geometry.track,-outer.left,-outer.top);
        geometry.arrow = bar.dxyLineButton;
        if (bar.rgstate[0] & STATE_SYSTEM_UNAVAILABLE) return true;
    }
    SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS};
    if (!GetScrollInfo(window,pageBar?SB_CTL:SB_VERT,&info)) return true;
    const int units = info.nMax-info.nMin+1;
    geometry.minimum = info.nMin;
    geometry.position = info.nPos;
    geometry.page = (std::max)(1,static_cast<int>(info.nPage));
    geometry.range = (std::max)(0,units-geometry.page);
    const int height = static_cast<int>(geometry.track.bottom-geometry.track.top);
    geometry.arrow = std::clamp(geometry.arrow,0,height/4);
    const int rail = height-2*geometry.arrow;
    if (!geometry.range || rail <= 0) return true;
    geometry.thumbSize = std::clamp(MulDiv(rail,geometry.page,units),(std::min)(rail,HubPx(20)),rail);
    geometry.travel = rail-geometry.thumbSize;
    geometry.thumbTop = geometry.track.top+geometry.arrow+
        MulDiv(geometry.travel,geometry.position-geometry.minimum,geometry.range);
    return true;
}

constexpr UINT_PTR kHubScrollRepeatTimer = 0x4B53;
struct HubScrollDrag {
    HWND window = nullptr;
    int offset = 0, initialY = 0, initialPosition = 0, repeatStep = 0;
};
HubScrollDrag g_hubScrollDrag;

int HubDragPosition(const HubScrollGeometry& bar,int y,int offset)
{
    if (bar.travel <= 0) return bar.position;
    const int pixel = std::clamp(y-offset-static_cast<int>(bar.track.top)-bar.arrow,0,bar.travel);
    return bar.minimum+MulDiv(pixel,bar.range,bar.travel);
}

bool DrawHubScrollbar(HWND window,HDC supplied = nullptr)
{
    HubScrollGeometry bar;
    if (!MeasureHubScrollbar(window,supplied!=nullptr,bar)) return false;
    const auto dc = supplied ? supplied : GetWindowDC(window);
    if (!dc) return false;
    const auto track = bar.track;
    HubFill(dc,track,kHubBackground);
    if (bar.range) {
        RECT thumb{track.left+HubPx(3),bar.thumbTop,track.right-HubPx(3),bar.thumbTop+bar.thumbSize};
        HubSurface(dc,thumb,4,g_hubScrollDrag.window==window?RGB(157,77,99):RGB(108,66,83),kHubSelectedBorder);
        const int x = (track.left+track.right)/2;
        const auto pen = CreatePen(PS_SOLID,1,kHubMuted);
        const auto old = SelectObject(dc,pen);
        for (int direction : {-1,1}) {
            const int y = direction < 0 ? track.top+bar.arrow/2 : track.bottom-bar.arrow/2;
            const POINT arrow[] = {{x-3,y-direction*2},{x,y+direction},{x+3,y-direction*2}};
            Polyline(dc,arrow,3);
        }
        SelectObject(dc,old);
        DeleteObject(pen);
    }
    if (!supplied) ReleaseDC(window,dc);
    return true;
}

void InvalidateHubScrollbar(HWND window,bool pageBar)
{
    if (pageBar) InvalidateRect(window,nullptr,FALSE);
    else DrawHubScrollbar(window);
}

void SetHubScrollbarPosition(HWND window,bool pageBar,const HubScrollGeometry& bar,int position)
{
    position = std::clamp(position,bar.minimum,bar.minimum+bar.range);
    if (position == bar.position) return;
    if (pageBar) SetHubPageScroll(GetParent(window),position);
    else if (window == g_recentList) SendMessageW(window,LB_SETTOPINDEX,position,0);
    else SendMessageW(window,EM_LINESCROLL,0,position-bar.position);
    InvalidateHubScrollbar(window,pageBar);
}

bool HandleHubEngineWheel(HWND window,WPARAM wParam,LPARAM lParam)
{
    if (g_hubPage!=HubPage::Engines || SendMessageW(g_engineList,CB_GETDROPPEDSTATE,0,0))
        return false;
    POINT point{GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};
    ScreenToClient(window,&point);
    RECT client{};
    GetClientRect(window,&client);
    if (!PtInRect(&client,point)) return false;
    UINT lines = 3;
    SystemParametersInfoW(SPI_GETWHEELSCROLLLINES,0,&lines,0);
    if (!lines) return true;
    const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
    if (!delta) return true;

    const auto layout = MeasureHub(window);
    const RECT body = HubEngineBody(layout);
    const HWND viewport = GetDlgItem(window,kHubEngineViewport);
    HWND child = nullptr;
    if (viewport && PtInRect(&body,point)) {
        MapWindowPoints(window,viewport,&point,1);
        child = ChildWindowFromPointEx(viewport,point,CWP_SKIPINVISIBLE);
    }
    HubScrollGeometry inner;
    if (!child || child==viewport || !(GetWindowLongPtrW(child,GWL_STYLE)&WS_VSCROLL) ||
        !MeasureHubScrollbar(child,false,inner) || !inner.range ||
        (delta>0 ? inner.position<=inner.minimum : inner.position>=inner.minimum+inner.range))
        child = nullptr;
    const HWND target = child ? child : window;
    if (g_hubWheelTarget!=target || (g_hubWheelRemainder>0 && delta<0) ||
        (g_hubWheelRemainder<0 && delta>0)) {
        g_hubWheelTarget = target;
        g_hubWheelRemainder = 0;
    }
    g_hubWheelRemainder += delta;
    const int detents = g_hubWheelRemainder/WHEEL_DELTA;
    g_hubWheelRemainder %= WHEEL_DELTA;
    if (!detents) return true;
    const int lineCount = static_cast<int>((std::min)(lines,1024u));
    if (child) {
        const int step = lines==WHEEL_PAGESCROLL ? inner.page : lineCount;
        SetHubScrollbarPosition(child,false,inner,inner.position-detents*step);
    } else {
        const int page = layout.viewportHeight-kHubEngineBodyTop-kHubEngineBodyBottomInset;
        const int step = lines==WHEEL_PAGESCROLL ? page : 24*lineCount;
        SetHubPageScroll(window,g_hubScroll-detents*step);
    }
    return true;
}

bool HandleHubScrollbarPointer(HWND window,bool pageBar,UINT message,WPARAM wParam,LPARAM lParam)
{
    const bool dragging = g_hubScrollDrag.window==window;
    if (message==WM_TIMER && wParam==kHubScrollRepeatTimer) {
        if (!dragging || !g_hubScrollDrag.repeatStep) return true;
        SetTimer(window,kHubScrollRepeatTimer,65,nullptr);
        HubScrollGeometry bar;
        RECT outer{};
        POINT point{};
        if (!MeasureHubScrollbar(window,pageBar,bar) || !bar.range ||
            !GetWindowRect(window,&outer) || !GetCursorPos(&point)) return true;
        point.y -= outer.top;
        const int step = g_hubScrollDrag.repeatStep;
        if (point.x>=outer.left+bar.track.left && point.x<outer.left+bar.track.right &&
            point.y>=bar.track.top && point.y<bar.track.bottom &&
            (step<0 ? point.y<bar.thumbTop : point.y>=bar.thumbTop+bar.thumbSize))
            SetHubScrollbarPosition(window,pageBar,bar,bar.position+step);
        return true;
    }
    if (dragging && (message==WM_CAPTURECHANGED || message==WM_CANCELMODE || message==WM_NCDESTROY ||
        (message==WM_KEYDOWN && wParam==VK_ESCAPE))) {
        KillTimer(window,kHubScrollRepeatTimer);
        g_hubScrollDrag = {};
        if (GetCapture()==window) ReleaseCapture();
        if (message!=WM_NCDESTROY) InvalidateHubScrollbar(window,pageBar);
        return message==WM_KEYDOWN;
    }
    const bool nonClient = !pageBar && wParam==HTVSCROLL &&
        (message==WM_NCLBUTTONDOWN || message==WM_NCLBUTTONDBLCLK ||
            message==WM_NCMOUSEMOVE || message==WM_NCLBUTTONUP);
    if (nonClient && (message==WM_NCMOUSEMOVE || message==WM_NCLBUTTONUP)) return true;
    if (pageBar && !dragging && (message==WM_MOUSEMOVE || message==WM_LBUTTONUP)) return true;
    const bool press = nonClient || (pageBar && (message==WM_LBUTTONDOWN || message==WM_LBUTTONDBLCLK));
    if (!press && !(dragging && (message==WM_MOUSEMOVE || message==WM_LBUTTONUP))) return false;

    HubScrollGeometry bar;
    if (MeasureHubScrollbar(window,pageBar,bar) && bar.range) {
        POINT point{GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};
        if (!pageBar) {
            RECT outer{};
            if (!nonClient) ClientToScreen(window,&point);
            GetWindowRect(window,&outer);
            point.y -= outer.top;
        }
        if (press) {
            if (pageBar) SetFocus(window);
            if (point.y>=bar.thumbTop && point.y<bar.thumbTop+bar.thumbSize) {
                SetCapture(window);
                if (GetCapture()==window) g_hubScrollDrag = {window,point.y-bar.thumbTop,point.y,bar.position};
                InvalidateHubScrollbar(window,pageBar);
            } else {
                const int magnitude = point.y < bar.track.top+bar.arrow || point.y >= bar.track.bottom-bar.arrow ?
                    (pageBar?32:1) : bar.page;
                const int step = point.y<bar.thumbTop ? -magnitude : magnitude;
                SetCapture(window);
                if (GetCapture()==window) {
                    g_hubScrollDrag = {window,0,0,0,step};
                    SetTimer(window,kHubScrollRepeatTimer,360,nullptr);
                }
                SetHubScrollbarPosition(window,pageBar,bar,bar.position+step);
            }
        } else if (!g_hubScrollDrag.repeatStep) {
            const int position = point.y==g_hubScrollDrag.initialY ? g_hubScrollDrag.initialPosition :
                HubDragPosition(bar,point.y,g_hubScrollDrag.offset);
            SetHubScrollbarPosition(window,pageBar,bar,position);
        }
    }
    if (message==WM_LBUTTONUP && dragging) {
        KillTimer(window,kHubScrollRepeatTimer);
        g_hubScrollDrag = {};
        if (GetCapture()==window) ReleaseCapture();
        InvalidateHubScrollbar(window,pageBar);
    }
    return true;
}

constexpr DWORD_PTR kHubComboControl = 1, kHubButtonControl = 2, kHubEntryControl = 4, kHubScrollControl = 8;
constexpr DWORD_PTR kHubPageScrollControl = 16;

void PaintHubEngineViewport(HWND viewport)
{
    PAINTSTRUCT paint{};
    const HDC target = BeginPaint(viewport,&paint);
    RECT bounds{};
    GetClientRect(viewport,&bounds);
    const auto dc = CreateCompatibleDC(target);
    const auto bitmap = CreateCompatibleBitmap(target,bounds.right,bounds.bottom);
    if (!dc || !bitmap) {
        if (bitmap) DeleteObject(bitmap);
        if (dc) DeleteDC(dc);
        HubFill(target,paint.rcPaint,kHubPanel);
        EndPaint(viewport,&paint);
        return;
    }
    const auto old = SelectObject(dc,bitmap);
    HubFill(dc,bounds,kHubBackground);
    HubSurface(dc,bounds,16,kHubPanel,kHubBorder);
    const auto layout = MeasureHub(GetParent(viewport));
    const auto body = HubEngineBody(layout);
    for (const auto& control : g_hubControls) {
        if (!control.entry || !IsHubEngineChild(control)) continue;
        RECT frame = control.bounds;
        OffsetRect(&frame,-body.left,-body.top);
        HubSurface(dc,frame,8,kHubPanel,GetFocus()==GetDlgItem(viewport,control.id)?kHubSelectedBorder:kHubBorder);
    }
    SetViewportOrgEx(dc,-body.left,-body.top-HubPx(g_hubScroll),nullptr);
    const int x = layout.x, total = layout.total, height = layout.height;
    HubLabelAt(dc,L"プロジェクトを開くエディタ",x,122,total,26,g_hubStrongFont,kHubText);
    HubLabelAt(dc,L"ローカルパッケージ",x,370,total,26,g_hubStrongFont,kHubText);
    HubLabelAt(dc,L"チェックサム検証 → インストール → 明示的に選択",x,height-334,total,24,g_hubSmallFont,kHubMuted);
    HubLabelAt(dc,L"エンジンの更新",x,height-310,total,24,g_hubStrongFont,kHubText);
    HubLabelAt(dc,L"更新元のHTTPS URL",x,height-286,total,24,g_hubSmallFont,kHubMuted);
    SetViewportOrgEx(dc,0,0,nullptr);
    BitBlt(target,0,0,bounds.right,bounds.bottom,dc,0,0,SRCCOPY);
    SelectObject(dc,old);
    DeleteObject(bitmap);
    DeleteDC(dc);
    EndPaint(viewport,&paint);
}

LRESULT CALLBACK HubEngineViewportProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam,UINT_PTR,DWORD_PTR)
{
    switch (message) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: PaintHubEngineViewport(window); return 0;
    case WM_COMMAND:
    case WM_DRAWITEM:
    case WM_MEASUREITEM:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORLISTBOX:
    case WM_MOUSEWHEEL:
        return SendMessageW(GetParent(window),message,wParam,lParam);
    }
    return DefSubclassProc(window,message,wParam,lParam);
}

LRESULT CALLBACK HubControlProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam,
    UINT_PTR, DWORD_PTR kind)
{
    if ((kind & (kHubPageScrollControl|kHubScrollControl)) &&
        HandleHubScrollbarPointer(window,(kind&kHubPageScrollControl)!=0,message,wParam,lParam)) return 0;
    if (kind & kHubPageScrollControl) {
        if (message == WM_ERASEBKGND) return 1;
        if (message == WM_PAINT) {
            PAINTSTRUCT paint{};
            const HDC dc = BeginPaint(window,&paint);
            HubFill(dc,paint.rcPaint,kHubBackground);
            DrawHubScrollbar(window,dc);
            EndPaint(window,&paint);
            return 0;
        }
    }
    if ((kind & (kHubComboControl|kHubButtonControl)) && message == WM_ERASEBKGND) return 1;
    if ((kind & kHubScrollControl) && message == WM_NCPAINT && DrawHubScrollbar(window)) return 0;
    if ((kind & (kHubComboControl|kHubButtonControl)) && message == WM_MOUSEMOVE && g_hubHoveredButton != window) {
        const HWND previous = g_hubHoveredButton;
        g_hubHoveredButton = window;
        TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,window,0};
        TrackMouseEvent(&track);
        if (previous) InvalidateRect(previous,nullptr,FALSE);
        InvalidateRect(window,nullptr,FALSE);
    } else if (message == WM_MOUSELEAVE && g_hubHoveredButton == window) {
        g_hubHoveredButton = nullptr;
        InvalidateRect(window,nullptr,FALSE);
    } else if (message == WM_NCDESTROY && g_hubHoveredButton == window) {
        g_hubHoveredButton = nullptr;
    }
    if ((kind & kHubComboControl) && message == WM_PAINT) {
        PAINTSTRUCT paint{};
        const HDC dc = BeginPaint(window,&paint);
        RECT bounds{};
        GetClientRect(window,&bounds);
        HubFill(dc,bounds,kHubBackground);
        HubSurface(dc,bounds,8,kHubPanel,GetFocus()==window ? kHubSelectedBorder : kHubBorder);
        RECT label = bounds;
        label.left += HubPx(12);
        label.right -= HubPx(28);
        HubLabel(dc,HubComboText(window,static_cast<int>(SendMessageW(window,CB_GETCURSEL,0,0))),
            label,g_hubFont,IsWindowEnabled(window)?kHubText:kHubMuted,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
        const int x = bounds.right-HubPx(16), y = bounds.bottom/2;
        const POINT arrow[] = {{x-HubPx(4),y-HubPx(2)},{x,y+HubPx(2)},{x+HubPx(4),y-HubPx(2)}};
        const auto pen = CreatePen(PS_SOLID,HubPx(1),kHubAccent);
        const auto old = SelectObject(dc,pen);
        Polyline(dc,arrow,3);
        SelectObject(dc,old);
        DeleteObject(pen);
        EndPaint(window,&paint);
        return 0;
    }
    const auto result = DefSubclassProc(window,message,wParam,lParam);
    if ((kind & kHubComboControl) && (message==CB_SETCURSEL || message==WM_SETFOCUS ||
        message==WM_KILLFOCUS || message==WM_ENABLE))
        InvalidateRect(window,nullptr,FALSE);
    if ((kind & kHubScrollControl) && (message == WM_PAINT || message == WM_VSCROLL || message == WM_MOUSEWHEEL))
        DrawHubScrollbar(window);
    if ((kind & kHubEntryControl) && (message == WM_SETFOCUS || message == WM_KILLFOCUS)) {
        const HWND parent = GetParent(window);
        const auto entry = std::find_if(g_hubControls.begin(),g_hubControls.end(),
            [window](const HubControl& control) { return control.id == GetDlgCtrlID(window); });
        if (entry != g_hubControls.end())
            InvalidateRect(parent,IsHubEngineChild(*entry)?nullptr:&entry->bounds,FALSE);
    }
    if ((kind & kHubEntryControl) && message == WM_PAINT && GetDlgCtrlID(window)==kHubSearch &&
        !GetWindowTextLengthW(window) && GetFocus()!=window) {
        RECT bounds{};
        GetClientRect(window,&bounds);
        const auto dc = GetDC(window);
        if (dc) {
            HubFill(dc,bounds,kHubPanel);
            HubLabel(dc,L"名前・パスを検索  (Ctrl+F)",bounds,g_hubFont,kHubMuted,DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);
            ReleaseDC(window,dc);
        }
    }
    return result;
}

void DrawHubPreview(HDC dc, const KuboHub::ProjectPreview& preview, int x, int y, int width, int height)
{
    if (!preview.bgra) return;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = KuboHub::PreviewWidth;
    info.bmiHeader.biHeight = -static_cast<LONG>(KuboHub::PreviewHeight);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    SetStretchBltMode(dc, HALFTONE);
    SetBrushOrgEx(dc, 0, 0, nullptr);
    const int saved = SaveDC(dc);
    POINT corners[] = {{x,y},{x+width,y+height}};
    LPtoDP(dc,corners,2);
    const auto clip = CreateRoundRectRgn(corners[0].x,corners[0].y,corners[1].x,corners[1].y,HubPx(14),HubPx(14));
    if (saved && clip) ExtSelectClipRgn(dc,clip,RGN_AND);
    StretchDIBits(dc, x, y, width, height, 0, 0, KuboHub::PreviewWidth, KuboHub::PreviewHeight,
        preview.bgra->data(), &info, DIB_RGB_COLORS, SRCCOPY);
    if (clip) DeleteObject(clip);
    if (saved) RestoreDC(dc,saved);
}
RECT HubLoadingTrack(HWND window)
{
    RECT bounds{};
    GetWindowRect(GetDlgItem(window,kEditorSessionStatus), &bounds);
    MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&bounds), 2);
    bounds.top = bounds.bottom + HubPx(1);
    bounds.bottom = bounds.top + HubPx(3);
    return bounds;
}

void PaintHub(HWND window)
{
    PAINTSTRUCT paint{};HDC target=BeginPaint(window,&paint);RECT bounds{};GetClientRect(window,&bounds);
    HDC dc=CreateCompatibleDC(target);HBITMAP bitmap=CreateCompatibleBitmap(target,(std::max)(1L,bounds.right),(std::max)(1L,bounds.bottom));
    if (!dc || !bitmap) {
        if (bitmap) DeleteObject(bitmap);
        if (dc) DeleteDC(dc);
        HubFill(target,paint.rcPaint,kHubBackground);
        EndPaint(window,&paint);
        return;
    }
    auto old=SelectObject(dc,bitmap);
    IntersectClipRect(dc,paint.rcPaint.left,paint.rcPaint.top,paint.rcPaint.right,paint.rcPaint.bottom);
    HubFill(dc,bounds,kHubBackground);
    const auto layout = MeasureHub(window);
    const auto [width,viewportHeight,height,sidebar,x,right,middle,total,compact] = layout;
    HubFill(dc,{0,0,HubPx(sidebar),bounds.bottom},kHubSidebar);
    DrawHubGeometry(dc,bounds,HubPx(sidebar));
    const bool fixedSidebar = viewportHeight >= kHubFixedSidebarHeight;
    const auto pending = std::find_if(g_editorSessions.begin(),g_editorSessions.end(),
        [](const auto& session) { return !session.confirmed; });
    if (pending != g_editorSessions.end()) {
        RECT track = HubLoadingTrack(window);
        HubFill(dc,track,kHubBorder);
        BOOL animations = FALSE;
        SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animations,0);
        const LONG segment = (std::max)(1L,(track.right-track.left)/3);
        const auto span = (std::max)(0L,track.right-track.left-segment);
        const auto phase = (GetTickCount64()-pending->started)%1600;
        const LONG offset = animations ? MulDiv(span,static_cast<int>((std::min)(phase,1600-phase)),800) : span/2;
        track.left += offset;
        track.right = track.left + segment;
        HubFill(dc,track,kHubAccent);
    }
    SetViewportOrgEx(dc,0,fixedSidebar ? 0 : -HubPx(g_hubScroll),nullptr);
    const int brandSize = HubPx(48);
    if (g_hubBrandIcon)
        DrawIconEx(dc,HubPx(18),HubPx(34),g_hubBrandIcon,brandSize,brandSize,0,nullptr,DI_NORMAL);
    HubLabelAt(dc,L"KUBO",76,28,sidebar-82,40,g_hubTitleFont,kHubText);
    HubLabelAt(dc,L"ENGINE HUB",77,67,sidebar-84,24,g_hubSmallFont,kHubAccent);
    HubLabelAt(dc,L"ワークスペース",20,107,150,20,g_hubSmallFont,kHubMuted);
    HubLabelAt(dc,g_hubReleaseLabel,24,(fixedSidebar ? viewportHeight : height)-102,sidebar-40,24,g_hubSmallFont,kHubMuted);
    SetViewportOrgEx(dc,0,0,nullptr);
    const int scroll = HubPx(g_hubScroll);
    const bool enginePage = g_hubPage==HubPage::Engines;
    const wchar_t* titles[]={L"プロジェクト",L"新規プロジェクト",L"エンジン管理",L"使い方と更新内容",L"Gitから取得"};
    const wchar_t* subtitles[]={L"作業を続けるプロジェクトを選んでください。",L"テンプレートと保存先を選んで、制作を始めましょう。",L"エディタの選択と、ローカルパッケージの管理。",L"Hubから制作を始めるためのガイド。",L"HTTPSのリポジトリからプロジェクトを取得します。"};
    const wchar_t* sections[]={L"WORKSPACE",L"START A PROJECT",L"ENGINE VERSIONS",L"GUIDE",L"VERSION CONTROL"};
    SetViewportOrgEx(dc,0,enginePage?0:-scroll,nullptr);
    HubLabelAt(dc,sections[static_cast<int>(g_hubPage)],x,16,total,20,g_hubSmallFont,kHubAccent);
    HubLabelAt(dc,titles[static_cast<int>(g_hubPage)],x,35,total,42,g_hubTitleFont,kHubText);
    HubLabelAt(dc,subtitles[static_cast<int>(g_hubPage)],x,79,total,30,g_hubFont,kHubMuted);
    SetViewportOrgEx(dc,0,0,nullptr);
    if ((g_hubPage==HubPage::Projects || g_hubPage==HubPage::Create) && !compact)
        HubSurface(dc,{HubPx(right),HubPx(122)-scroll,HubPx(width-16),HubPx(height-28)-scroll},16,kHubPanel,kHubBorder);
    if (g_hubPage==HubPage::Projects)
        HubSurface(dc,{HubPx(x-6),HubPx(188)-scroll,HubPx(x+middle+6),HubPx(compact?410:height-80)-scroll},13,kHubBackground,kHubBorder);
    if (g_hubPage==HubPage::Guide || g_hubPage==HubPage::Import)
        HubSurface(dc,{HubPx(x-8),HubPx(110)-scroll,HubPx(x+total+8),HubPx(height-16)-scroll},16,kHubPanel,kHubBorder);
    for (const auto& control : g_hubControls) {
        if (!control.entry || control.page != g_hubPage || IsHubEngineChild(control)) continue;
        const HWND child = GetDlgItem(window,control.id);
        HubSurface(dc,control.bounds,8,kHubPanel,GetFocus()==child ? kHubSelectedBorder : kHubBorder);
    }
    SetViewportOrgEx(dc,0,-HubPx(g_hubScroll),nullptr);
    if(g_hubPage==HubPage::Projects || g_hubPage==HubPage::Create) {
        if(g_hubPage==HubPage::Projects) {
            HubLabelAt(dc,L"選択したプロジェクト",compact?x:right+16,compact?462:132,256,26,g_hubStrongFont,kHubText);
            if (const auto* preview = SelectedHubPreview()) {
                const int previewX = compact ? x : right + 16;
                int previewY = compact ? 490 : 162;
                if (preview->bgra) {
                    const int previewWidth = compact ? 192 : 256;
                    DrawHubPreview(dc, *preview, HubPx(previewX), HubPx(previewY),
                        HubPx(previewWidth), HubPx(previewWidth * 9 / 16));
                    previewY += compact ? 120 : 152;
                }
                if (!preview->lastOpened.empty())
                    HubLabelAt(dc,L"最終オープン  " + preview->lastOpened,previewX,previewY,256,24,g_hubSmallFont,kHubMuted);
            }
        }else {
            HubLabelAt(dc,L"テンプレート",x,112,middle,24,g_hubSmallFont,kHubMuted);
            HubLabelAt(dc,L"プロジェクト名",x,264,middle,24,g_hubFont,kHubMuted);
            HubLabelAt(dc,L"保存先の親フォルダー",x,348,middle,24,g_hubFont,kHubMuted);
            HubLabelAt(dc,g_hubNative?L"Runtime SDKのフォルダー":L"Runtime SDK  /  このテンプレートでは不要",x,432,middle,24,g_hubFont,kHubMuted);
            if (!compact) {
            HubLabelAt(dc,g_hubNative?L"C++ / Runtime SDK":g_hubSprite?L"2D スプライト":L"空のシーン",right+16,144,256,48,g_hubStrongFont,kHubText);
            const wchar_t* description = g_hubNative
                ? L"C++ソースとSDKを参照するビルド設定を用意します。コンパイルには対応するMSVC Build ToolsとWindows SDKが必要です。"
                : g_hubSprite ? L"Canvasと色付きスプライトを配置した2Dシーンです。スプライトの位置、サイズ、色をエディタで編集できます。"
                : L"空のMain.sceneとAssets、ProjectSettingsを用意します。作成時にコンパイルは不要です。";
            HubLabelAt(dc,description,right+16,202,256,302,g_hubFont,kHubMuted);
            HubLabelAt(dc,L"選択中のエディタで開きます。\n変更は「エンジン管理」から。",right+16,height-192,256,64,g_hubSmallFont,kHubMuted);
            }
        }
    }else if(g_hubPage==HubPage::Import) {
        HubLabelAt(dc,L"リポジトリのHTTPS URL",x,120,total,24,g_hubFont,kHubMuted);
        HubLabelAt(dc,L"保存先の親フォルダー",x,200,total,24,g_hubFont,kHubMuted);
        HubLabelAt(dc,L"新しく作るフォルダー名",x,280,total,24,g_hubFont,kHubMuted);
    }
    SetViewportOrgEx(dc,0,0,nullptr);
    BitBlt(target,0,0,bounds.right,bounds.bottom,dc,0,0,SRCCOPY);SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);EndPaint(window,&paint);
}
void DrawHubButton(const DRAWITEMSTRUCT& item);

void DrawHubItem(const DRAWITEMSTRUCT& item)
{
    if (item.CtlType == ODT_COMBOBOX) {
        if (item.itemState & ODS_COMBOBOXEDIT) return;
        const bool selected = (item.itemState & ODS_SELECTED) != 0;
        HubFill(item.hDC,item.rcItem,selected ? kHubSelected : kHubPanel);
        RECT text = item.rcItem;
        text.left += HubPx(10);
        text.right -= HubPx(8);
        if (item.itemID != static_cast<UINT>(-1))
            HubLabel(item.hDC,HubComboText(item.hwndItem,static_cast<int>(item.itemID)),text,g_hubFont,kHubText,
                DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
        return;
    }
    if(item.CtlID==kRecentList) {
        if(item.itemID==static_cast<UINT>(-1) || item.itemID>=g_hubRows.size())return;
        const auto& row=g_hubRows[item.itemID];const bool selected=(item.itemState&ODS_SELECTED)!=0;
        RECT rect=item.rcItem;
        HubFill(item.hDC,rect,kHubBackground);
        InflateRect(&rect,-HubPx(4),-HubPx(4));
        HubSurface(item.hDC,rect,10,selected?kHubSelected:kHubPanel,selected?kHubSelectedBorder:kHubBorder);
        if (g_hubDenseProjects) {
            const int x = rect.left + HubPx(12), end = rect.right - HubPx(12);
            HubLabel(item.hDC,(row.pinned?L"[PIN]  ":L"")+row.name,
                {x,rect.top+HubPx(4),end,rect.top+HubPx(28)},g_hubStrongFont,kHubText,DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
            HubLabel(item.hDC,row.path,{x,rect.top+HubPx(29),end,rect.bottom},
                g_hubSmallFont,row.readable?kHubMuted:RGB(245,184,115),DT_SINGLELINE|DT_PATH_ELLIPSIS|DT_NOPREFIX);
            if(item.itemState&ODS_FOCUS){InflateRect(&rect,-HubPx(4),-HubPx(4));DrawFocusRect(item.hDC,&rect);}
            return;
        }
        const int x=rect.left+HubPx(14),y=rect.top+HubPx(11),end=rect.right-HubPx(14);
        const int previewWidth=std::clamp((end-x)/3,HubPx(84),HubPx(184));
        const int previewHeight=previewWidth*9/16;
        const int previewY=rect.top+(rect.bottom-rect.top-previewHeight)/2;
        const auto* preview = FindHubPreview(row.path);
        const bool hasImage = preview && preview->bgra;
        if (hasImage) DrawHubPreview(item.hDC,*preview,x,previewY,previewWidth,previewHeight);
        else {
            HubSurface(item.hDC,{x,previewY,x+previewWidth,previewY+previewHeight},7,RGB(45,27,38),kHubBorder);
            const auto initial=row.name.empty()?L"K":row.name.substr(0,1);
            HubLabel(item.hDC,initial,{x,previewY,x+previewWidth,previewY+previewHeight},g_hubTitleFont,kHubAccent,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        }
        const int nameX = x + previewWidth + HubPx(14);
        auto status = row.readable ? L"engineVersion  " + row.version : L"見つからない、または読み込めないプロジェクト";
        if (row.readable && preview && !preview->lastOpened.empty()) status += L"  /  " + preview->lastOpened;
        HubLabel(item.hDC,(row.pinned?L"[PIN]  ":L"")+row.name,{nameX,y,end,y+HubPx(27)},g_hubStrongFont,kHubText,DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        HubLabel(item.hDC,status,{nameX,y+HubPx(30),end,y+HubPx(51)},g_hubSmallFont,row.readable?kHubMuted:RGB(245,184,115),DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        HubLabel(item.hDC,row.path,{nameX,y+HubPx(63),end,y+HubPx(88)},g_hubSmallFont,kHubMuted,DT_SINGLELINE|DT_PATH_ELLIPSIS|DT_NOPREFIX);
        if(item.itemState&ODS_FOCUS){InflateRect(&rect,-HubPx(5),-HubPx(5));DrawFocusRect(item.hDC,&rect);}return;
    }
    const int width = item.rcItem.right-item.rcItem.left, height = item.rcItem.bottom-item.rcItem.top;
    if (width <= 0 || height <= 0) return;
    const auto buffer = CreateCompatibleDC(item.hDC);
    const auto bitmap = CreateCompatibleBitmap(item.hDC,width,height);
    if (!buffer || !bitmap) {
        if (bitmap) DeleteObject(bitmap);
        if (buffer) DeleteDC(buffer);
        DrawHubButton(item);
        return;
    }
    const auto old = SelectObject(buffer,bitmap);
    auto copy = item;
    copy.hDC = buffer;
    OffsetRect(&copy.rcItem,-item.rcItem.left,-item.rcItem.top);
    DrawHubButton(copy);
    BitBlt(item.hDC,item.rcItem.left,item.rcItem.top,width,height,buffer,0,0,SRCCOPY);
    SelectObject(buffer,old);
    DeleteObject(bitmap);
    DeleteDC(buffer);
}

void DrawHubButton(const DRAWITEMSTRUCT& item)
{
    const int id=static_cast<int>(item.CtlID);
    const int nav = HubNavIndex(id);
    const bool templ=id==kTemplateBlank || id==kTemplateNative || id==kTemplateSprite;
    const int selectedTemplate = g_hubNative ? kTemplateNative : g_hubSprite ? kTemplateSprite : kTemplateBlank;
    const bool selected=nav==static_cast<int>(g_hubPage) || (templ && id==selectedTemplate);
    const bool primary=id==kLaunchButton || id==kCreateSubmit || id==kImportStart;
    const bool disabled=(item.itemState&ODS_DISABLED)!=0,pressed=(item.itemState&ODS_SELECTED)!=0;
    const bool hovered=g_hubHoveredButton==item.hwndItem && !disabled;
    const bool sidebar=nav>=0 || id==kOpenButton || id==kExitButton || id==kUpdateBadge;
    HubFill(item.hDC,item.rcItem,sidebar?kHubSidebar:kHubBackground);
    RECT text=item.rcItem;InflateRect(&text,-HubPx(12),-HubPx(4));
    if (nav>=0) {
        if (hovered || pressed) {
            RECT hover = item.rcItem;
            InflateRect(&hover,-HubPx(2),-HubPx(4));
            HubSurface(item.hDC,hover,10,RGB(44,27,37),RGB(66,39,51));
        }
        const HWND parent = GetParent(item.hwndItem);
        const auto layout = MeasureHub(parent);
        RECT position{};
        GetWindowRect(item.hwndItem,&position);
        MapWindowPoints(nullptr,parent,reinterpret_cast<POINT*>(&position),2);
        const int shift=layout.viewportHeight>=kHubFixedSidebarHeight?0:HubPx(g_hubScroll);
        DrawHubNavFrame(item.hDC,HubPx(layout.sidebar),-position.left,-position.top-shift);
        DrawHubNavGlyph(item.hDC,nav,HubPx(15),(item.rcItem.bottom-HubPx(19))/2,selected?kHubAccent:kHubMuted);
        text.left = HubPx(42);
    } else {
        const COLORREF fill = primary && !disabled ? (pressed?RGB(149,40,62):hovered?RGB(193,56,79):RGB(169,45,69)) :
            selected?kHubSelected:hovered?RGB(52,33,44):kHubPanel;
        const COLORREF border = selected || (primary && !disabled) ? kHubSelectedBorder : kHubBorder;
        HubSurface(item.hDC,item.rcItem,templ?13:9,fill,border);
    }
    if(templ) {
        const bool native=id==kTemplateNative;
        const bool sprite=id==kTemplateSprite;
        if (sprite) {
            HubSurface(item.hDC,{text.left,text.top+HubPx(8),text.left+HubPx(72),text.top+HubPx(35)},5,RGB(45,24,38),kHubBorder);
            HubFill(item.hDC,{text.left+HubPx(12),text.top+HubPx(12),text.left+HubPx(27),text.top+HubPx(27)},kHubAccent);
            HubFill(item.hDC,{text.left+HubPx(43),text.top+HubPx(13),text.left+HubPx(55),text.top+HubPx(25)},RGB(255,176,64));
            HubFill(item.hDC,{text.left+HubPx(8),text.top+HubPx(30),text.left+HubPx(64),text.top+HubPx(33)},RGB(82,179,163));
        } else HubLabel(item.hDC,native?L"{ }  C++":L"◇  SCENE",{text.left,text.top+HubPx(6),text.right,text.top+HubPx(35)},g_hubStrongFont,kHubAccent);
        HubLabel(item.hDC,native?L"C++ SDK":sprite?L"2D スプライト":L"空のシーン",{text.left,text.top+HubPx(40),text.right,text.top+HubPx(65)},g_hubStrongFont,kHubText,DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        HubLabel(item.hDC,native?L"ソースコードから":sprite?L"Canvasと画像":L"最小構成",{text.left,text.top+HubPx(70),text.right,text.bottom},g_hubSmallFont,kHubMuted,DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
    }else HubLabel(item.hDC,HubText(item.hwndItem),text,selected?g_hubStrongFont:g_hubFont,
        disabled?RGB(145,124,133):kHubText,DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX|DT_END_ELLIPSIS|(nav>=0?DT_LEFT:DT_CENTER));
    if(item.itemState&ODS_FOCUS){RECT focus=item.rcItem;InflateRect(&focus,-HubPx(4),-HubPx(4));DrawFocusRect(item.hDC,&focus);}
}
void CreateHubControls(HWND window)
{
    g_hubControls.clear();g_hubRows.clear();g_hubPage=HubPage::Projects;g_hubNative=false;g_hubDpi=GetDpiForWindow(window);
    g_hubScroll = 0;
    g_hubDenseProjects = false;
    g_hubSprite = false;
    g_hubNextScan = 0;
    g_hubNavStarted = 0;
    g_hubNavFrame = 0;
    g_hubWheelTarget = nullptr;
    g_hubWheelRemainder = 0;
    g_hubHoveredButton = nullptr;
    Gdiplus::GdiplusStartupInput graphics;
    Gdiplus::GdiplusStartup(&g_hubGraphics,&graphics,nullptr);
    g_hubPreviewCancelled.store(false, std::memory_order_relaxed);
    Engine::Services::EnginePackageManifest release;
    std::string releaseError;
    g_hubReleaseLabel = L"Windows · DirectX 12";
    if (Engine::Services::EnginePackageManifest::Read(
        GetExecutableDirectory()/L"engine-package.json",release,releaseError) ==
        Engine::Services::EnginePackageManifest::ReadResult::Success && release.Versioned())
        g_hubReleaseLabel = L"Hub  " + NativeCreationMessage(release.engineVersion);
    g_hubPanelBrush=CreateSolidBrush(kHubPanel);
    const BOOL dark=TRUE;DwmSetWindowAttribute(window,20,&dark,sizeof(dark));
    const COLORREF caption = kHubSidebar, border = kHubBorder;
    DwmSetWindowAttribute(window,DWMWA_CAPTION_COLOR,&caption,sizeof(caption));
    DwmSetWindowAttribute(window,DWMWA_BORDER_COLOR,&border,sizeof(border));
    const HWND engineViewport = CreateWindowExW(WS_EX_CONTROLPARENT,L"STATIC",nullptr,
        WS_CHILD|WS_CLIPCHILDREN|WS_CLIPSIBLINGS,0,0,1,1,window,
        ControlId(kHubEngineViewport),GetModuleHandleW(nullptr),nullptr);
    g_hubControls.push_back({kHubEngineViewport,HubPage::Engines,false,false});
    if (engineViewport) SetWindowSubclass(engineViewport,HubEngineViewportProc,1,0);
    const auto control=[&](int id,HubPage page,const wchar_t* cls,const wchar_t* text,DWORD style,bool shared=false){
        const bool combo = wcscmp(cls,L"COMBOBOX")==0;
        const bool edit = wcscmp(cls,L"EDIT")==0;
        const bool button = wcscmp(cls,L"BUTTON")==0;
        if (combo) style |= CBS_OWNERDRAWFIXED|CBS_HASSTRINGS;
        if (edit) style &= ~WS_BORDER;
        const HWND parent = page==HubPage::Engines && !shared?engineViewport:window;
        const auto handle=CreateWindowExW(0,cls,text,WS_CHILD|(wcscmp(cls,L"STATIC")?WS_TABSTOP:0)|style,0,0,1,1,parent,ControlId(id),GetModuleHandleW(nullptr),nullptr);
        const bool entry = edit && !(style&ES_MULTILINE);
        g_hubControls.push_back({id,page,shared,entry});
        DWORD_PTR kind = button?kHubButtonControl:combo?kHubComboControl:entry?kHubEntryControl:0;
        if (!combo && style&WS_VSCROLL) kind |= kHubScrollControl;
        if (kind) SetWindowSubclass(handle,HubControlProc,1,kind);
        return handle;
    };
    const auto button=[&](int id,HubPage page,const wchar_t* text,bool shared=false,DWORD extra=0){return control(id,page,L"BUTTON",text,BS_OWNERDRAW|extra,shared);};
    button(kNavProjects,HubPage::Projects,L"プロジェクト",true);button(kNavCreate,HubPage::Create,L"新規プロジェクト",true);
    button(kNavEngines,HubPage::Engines,L"エンジン管理",true);button(kNavGuide,HubPage::Guide,L"使い方",true);
    button(kNavImport,HubPage::Import,L"Gitから取得",true);
    button(kOpenButton,HubPage::Projects,L"プロジェクトを開く…",true);button(kExitButton,HubPage::Projects,L"Hubを終了",true);
    control(kEditorSessionStatus,HubPage::Projects,L"STATIC",L"",0,true);
    button(kUpdateBadge,HubPage::Engines,L"更新情報",true);
    const auto pageScroll = control(kHubPageScroll,HubPage::Projects,L"SCROLLBAR",L"",SBS_VERT,true);
    SetWindowSubclass(pageScroll,HubControlProc,1,kHubPageScrollControl);
    control(kHubSearch,HubPage::Projects,L"EDIT",L"",ES_AUTOHSCROLL|WS_BORDER);
    SendMessageW(GetDlgItem(window,kHubSearch),EM_SETCUEBANNER,FALSE,reinterpret_cast<LPARAM>(L"名前・パスを検索  (Ctrl+F)"));
    SendMessageW(GetDlgItem(window,kHubSearch),EM_SETLIMITTEXT,512,0);
    const auto sort=control(kHubSort,HubPage::Projects,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL);
    SendMessageW(sort,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"最近の順"));SendMessageW(sort,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"名前の順"));SendMessageW(sort,CB_SETCURSEL,0,0);
    control(kProjectCount,HubPage::Projects,L"STATIC",L"",0);
    g_recentList=control(kRecentList,HubPage::Projects,L"LISTBOX",L"",LBS_NOTIFY|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS|LBS_NOINTEGRALHEIGHT|WS_VSCROLL);
    g_projectDetails=control(kProjectDetails,HubPage::Projects,L"EDIT",L"",ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL);
    button(kHubRefresh,HubPage::Projects,L"再読み込み");button(kNewButton,HubPage::Projects,L"新規プロジェクト");
    button(kToggleProjectView,HubPage::Projects,L"コンパクト表示");
    button(kHubFolder,HubPage::Projects,L"フォルダー");button(kHubCopy,HubPage::Projects,L"パスをコピー");
    button(kHubPin,HubPage::Projects,L"ピン留めする");button(kHubForget,HubPage::Projects,L"一覧から外す");button(kLaunchButton,HubPage::Projects,L"エディタで開く");
    button(kTemplateBlank,HubPage::Create,L"空のシーン");button(kTemplateNative,HubPage::Create,L"C++ / Runtime SDK");
    button(kTemplateSprite,HubPage::Create,L"2D スプライト");
    control(kCreateName,HubPage::Create,L"EDIT",L"MyGame",ES_AUTOHSCROLL|WS_BORDER);
    control(kCreateLocation,HubPage::Create,L"EDIT",L"",ES_AUTOHSCROLL|WS_BORDER);
    control(kCreateSdk,HubPage::Create,L"EDIT",L"",ES_AUTOHSCROLL|WS_BORDER);
    SendMessageW(GetDlgItem(window,kCreateName),EM_SETLIMITTEXT,240,0);
    for(auto id:{kCreateLocation,kCreateSdk})SendMessageW(GetDlgItem(window,id),EM_SETLIMITTEXT,30000,0);
    button(kBrowseLocation,HubPage::Create,L"参照…");button(kBrowseSdk,HubPage::Create,L"参照…");
    control(kCreatePreview,HubPage::Create,L"EDIT",L"",ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL);
    control(kCreateStatus,HubPage::Create,L"EDIT",L"",ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL);
    button(kCreateSubmit,HubPage::Create,L"作成してエディタで開く");
    g_engineList=control(kEngineList,HubPage::Engines,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL);
    button(kAddEngine,HubPage::Engines,L"エディタを登録・選択…");button(kRemoveEngine,HubPage::Engines,L"登録を解除");
    g_engineDetails=control(kEngineDetails,HubPage::Engines,L"EDIT",L"",ES_MULTILINE|ES_READONLY|WS_VSCROLL);
    button(kVerifyPackage,HubPage::Engines,L"パッケージを検証…");button(kInstallPackage,HubPage::Engines,L"インストール…");button(kRecoverInstallation,HubPage::Engines,L"中断した処理を復旧…");
    g_verificationDetails=control(kVerificationDetails,HubPage::Engines,L"EDIT",L"操作を選んでください。検証結果と進捗をここに表示します。",ES_MULTILINE|ES_READONLY|WS_VSCROLL);
    button(kCancelVerification,HubPage::Engines,L"処理をキャンセル",false,WS_DISABLED);button(kSelectInstalled,HubPage::Engines,L"インストール済みのエディタを選択",false,WS_DISABLED);
    control(kUpdateSource,HubPage::Engines,L"EDIT",L"",ES_AUTOHSCROLL|WS_BORDER);
    SendMessageW(GetHubControl(window,kUpdateSource),EM_SETLIMITTEXT,2048,0);
    button(kCheckUpdates,HubPage::Engines,L"設定して確認");
    button(kLaterUpdate,HubPage::Engines,L"後で",false,WS_DISABLED);
    button(kSkipUpdate,HubPage::Engines,L"この版を飛ばす",false,WS_DISABLED);
    control(kUpdateDetails,HubPage::Engines,L"EDIT",L"更新元を設定すると、起動時に新版を確認します。",
        ES_MULTILINE|ES_READONLY|WS_VSCROLL);
    button(kDownloadUpdate,HubPage::Engines,L"新版を取得して導入…",false,WS_DISABLED);
    control(kImportUrl,HubPage::Import,L"EDIT",L"",ES_AUTOHSCROLL|WS_BORDER);
    control(kImportLocation,HubPage::Import,L"EDIT",L"",ES_AUTOHSCROLL|WS_BORDER);
    control(kImportName,HubPage::Import,L"EDIT",L"",ES_AUTOHSCROLL|WS_BORDER);
    SendMessageW(GetDlgItem(window,kImportUrl),EM_SETLIMITTEXT,2048,0);
    SendMessageW(GetDlgItem(window,kImportLocation),EM_SETLIMITTEXT,30000,0);
    SendMessageW(GetDlgItem(window,kImportName),EM_SETLIMITTEXT,240,0);
    button(kImportBrowse,HubPage::Import,L"参照…");
    button(kImportStart,HubPage::Import,L"プロジェクトを取得");
    control(kImportStatus,HubPage::Import,L"EDIT",
        L"Gitが必要です。取得先は新しいフォルダーを指定してください。取得後、一覧から内容を確認して開けます。",
        ES_MULTILINE|ES_READONLY|WS_VSCROLL);
    // Retain the C++ entry command for existing integrations; the template cards are its visible entry.
    button(kNativeButton,HubPage::Create,L"C++プロジェクト");
    control(kGuideText,HubPage::Guide,L"EDIT",
        L"今回のHub更新\r\n\r\nシーン画像と最終オープン日時、起動時の更新確認、2Dスプライトのテンプレート、Gitからの取得を追加しました。\r\n\r\n"
        L"01  プロジェクトを開く\r\n\r\n左の「プロジェクトを開く」から .kubo ファイルを選びます。一覧では名前・パスで検索でき、ダブルクリックまたはEnterで開けます。\r\n\r\n"
        L"02  新規プロジェクト\r\n\r\n「空のシーン」はコンパイルせずに作成できます。「C++ / Runtime SDK」はSDKの指定が必要です。名前と保存先を入力すると最終的な作成先が表示されます。\r\n\r\n"
        L"03  ピン留めと履歴\r\n\r\nよく使うプロジェクトを先頭に固定できます。「一覧から外す」は履歴だけを変更し、ファイルを削除しません。見つからない項目も表示されるので、移動した場合は新しい .kubo を開いてください。\r\n\r\n"
        L"04  エンジンを切り替える\r\n\r\n「エンジン管理」で使用するエディタを選びます。パッケージは検証・インストール・選択を個別に行えます。バージョン情報とチェックサムは、互換性や発行者の証明ではありません。\r\n\r\n"
        L"キーボード\r\nCtrl+F: 検索  /  Ctrl+N: 新規  /  Ctrl+O: 開く\r\nF5: 一覧更新  /  Esc: 検索解除  /  Tab: 操作項目を移動\r\n\r\n"
        L"Hubおよびエディタの起動にVisual Studioの画面は不要です。C++のビルドには対応するMSVC Build ToolsとWindows SDKが必要です。\r\n\r\n"
        L"配布フォルダーの HUB-GUIDE.md に更新元と復旧の手順があります。",ES_MULTILINE|ES_READONLY|WS_VSCROLL);
    // Keep the empty-state label after the edit controls so native accessibility
    // does not mistake its (possibly hidden) text for the project-details label.
    control(kProjectEmpty,HubPage::Projects,L"STATIC",L"",0);
    UpdateHubFonts(window);
    wchar_t profile[32768]{};const auto count=GetEnvironmentVariableW(L"USERPROFILE",profile,32768);
    if(count && count<32768) {
        const auto parent = std::filesystem::path(profile)/L"KuboProjects";
        SetWindowTextW(GetDlgItem(window,kCreateLocation),parent.c_str());
        SetWindowTextW(GetDlgItem(window,kImportLocation),parent.c_str());
    }
    LayoutHub(window);
}
bool HandleHubKeyboard(HWND window,const MSG& message)
{
    if(message.message!=WM_KEYDOWN)return false;
    if(GetKeyState(VK_CONTROL)&0x8000) {
        if(message.wParam=='F'){SwitchHubPage(window,HubPage::Projects);SetFocus(GetDlgItem(window,kHubSearch));return true;}
        if(message.wParam=='N'){SwitchHubPage(window,HubPage::Create);return true;}
        if(message.wParam=='O'){SendMessageW(window,WM_COMMAND,kOpenButton,0);return true;}
    }
    if(message.wParam==VK_F5 && g_hubPage==HubPage::Projects){PopulateRecent();return true;}
    if(message.wParam==VK_ESCAPE && g_hubPage==HubPage::Projects){SetWindowTextW(GetDlgItem(window,kHubSearch),L"");return true;}
    if(message.wParam==VK_RETURN && message.hwnd==g_recentList && IsWindowEnabled(GetDlgItem(window,kLaunchButton))){SendMessageW(window,WM_COMMAND,kLaunchButton,0);return true;}
    return false;
}
void HubProjectMenu(HWND window,LPARAM position)
{
    POINT point{GET_X_LPARAM(position),GET_Y_LPARAM(position)};
    if(point.x==-1 && point.y==-1) {
        const auto selected=SendMessageW(g_recentList,LB_GETCURSEL,0,0);if(selected==LB_ERR)return;
        RECT row{};SendMessageW(g_recentList,LB_GETITEMRECT,selected,reinterpret_cast<LPARAM>(&row));
        point={row.left+HubPx(24),row.top+HubPx(24)};ClientToScreen(g_recentList,&point);
    }else {
        POINT local=point;ScreenToClient(g_recentList,&local);
        const auto hit=SendMessageW(g_recentList,LB_ITEMFROMPOINT,0,MAKELPARAM(local.x,local.y));
        if(HIWORD(hit) || static_cast<size_t>(LOWORD(hit))>=g_hubRows.size())return;
        SendMessageW(g_recentList,LB_SETCURSEL,LOWORD(hit),0);RefreshProjectDetails();RefreshHubActions(window);
    }
    const auto menu=CreatePopupMenu();if(!menu)return;
    for(const auto id:{kLaunchButton,kHubPin,kHubFolder,kHubCopy,kHubForget}) {
        const auto control=GetDlgItem(window,id);const auto label=HubText(control);
        AppendMenuW(menu,MF_STRING|(IsWindowEnabled(control)?MF_ENABLED:MF_GRAYED),id,label.c_str());
    }
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
    AppendMenuW(menu,MF_STRING,kRelinkProject,L"移動先のプロジェクトを指定…");
    const UINT projectActions = IsWindowEnabled(GetDlgItem(window,kLaunchButton)) ? MF_ENABLED : MF_GRAYED;
    AppendMenuW(menu,MF_STRING|projectActions,kUseEditorForProject,L"このプロジェクトで現在のエディタを使う");
    AppendMenuW(menu,MF_STRING|projectActions,kClearProjectEditor,L"プロジェクトのエディタ指定を解除");
    const auto command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,window,nullptr);DestroyMenu(menu);
    if(command)SendMessageW(window,WM_COMMAND,command,0);
}
void DestroyHubPresentation()
{
    g_hubPreviewCancelled.store(true, std::memory_order_relaxed);
    if (g_hubPreviewWorker.valid()) {
        try { g_hubPreviewWorker.get(); } catch (...) {}
    }
    g_hubPreviews.clear();
    for(auto font:{g_hubFont,g_hubTitleFont,g_hubStrongFont,g_hubSmallFont})if(font)DeleteObject(font);
    g_hubFont=g_hubTitleFont=g_hubStrongFont=g_hubSmallFont=nullptr;
    if(g_hubPanelBrush)DeleteObject(g_hubPanelBrush);g_hubPanelBrush=nullptr;g_hubControls.clear();g_hubRows.clear();
    if (g_hubBrandIcon) DestroyIcon(g_hubBrandIcon);
    g_hubBrandIcon = nullptr;
    if (g_hubGraphics) Gdiplus::GdiplusShutdown(g_hubGraphics);
    g_hubGraphics = 0;
}
