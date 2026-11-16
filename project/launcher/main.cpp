#include "ProjectContext.h"
#include "externals/imgui_node_editor/crude_json.h"
#include "engine/services/NativeProjectCreate.h"
#include "engine/services/ProjectCreate.h"
#include "EngineCatalog.h"
#include "engine/services/EnginePackageManifest.h"
#include "PackageVerificationJob.h"
#include "PackageInstallationJob.h"

#include <Windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <commctrl.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <shlwapi.h>
#include <algorithm>
#include <shellscalingapi.h>
#include "HubWindowPlacement.h"
#include "HubEditorSession.h"
#include "HubProjectPreview.h"
#include "HubUpdateFeed.h"
#include "HubUpdateInstall.h"
#include "branding/EngineIcon.h"
#include "engine/services/GitWorkspace.h"
#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#pragma comment(lib, "Comdlg32.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Uuid.lib")
#pragma comment(lib, "Gdi32.lib")
#pragma comment(lib, "Gdiplus.lib")
#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Uxtheme.lib")
#pragma comment(lib, "Dwmapi.lib")
#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Shcore.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace {

constexpr int kRecentList = 100;
constexpr int kOpenButton = 101;
constexpr int kNewButton = 102;
constexpr int kLaunchButton = 103;
constexpr int kExitButton = 104;
constexpr int kNativeButton = 105;
constexpr int kProjectDetails = 106;
constexpr int kAddEngine = 107;
constexpr int kEngineList = 108;
constexpr int kRemoveEngine = 109;
constexpr int kEngineDetails = 110;
constexpr UINT kRefreshEngineDetails = WM_APP+110;
constexpr int kVerifyPackage = 111;
constexpr int kCancelVerification = 112;
constexpr int kVerificationDetails = 113;
constexpr UINT_PTR kVerificationTimer = 114;
constexpr UINT_PTR kEditorSessionTimer = 118;
constexpr UINT_PTR kProjectPreviewTimer = 119;
constexpr int kInstallPackage = 115, kRecoverInstallation = 116, kSelectInstalled = 117;

HMENU ControlId(int id)
{
    return reinterpret_cast<HMENU>(static_cast<INT_PTR>(id));
}

HWND GetHubControl(HWND window,int id);

std::wstring NativeCreationMessage(const std::string& text)
{
    const auto count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0);
    if(!count) return L"Native project creation could not complete; inspect the output and debug diagnostics.";
    std::wstring result(static_cast<size_t>(count),L'\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),result.data(),count);
    return result;
}

HWND g_recentList = nullptr;
HWND g_projectDetails = nullptr;
HWND g_engineList = nullptr;
HWND g_engineDetails = nullptr;
HWND g_verificationDetails = nullptr;
KuboHub::PackageVerificationJob g_verificationJob;
KuboHub::PackageInstallationJob g_installationJob;
std::filesystem::path g_lastInstalledEditor;
bool g_closeAfterVerification = false;
KuboHub::EngineCatalog g_engineCatalog;
KuboHub::CatalogSnapshot g_engineSnapshot;
std::filesystem::path g_engineFile;
std::string g_engineError;
std::filesystem::path g_recentFile;
std::vector<KuboHub::EditorSession> g_editorSessions;
void SetEditorSessionStatus(HWND owner, const std::wstring& text);
void RefreshProjectDetails() noexcept;
constexpr size_t kRecentTextLimit = 1024 * 1024;
constexpr size_t kRecentPathLimit = 32767;
constexpr size_t kRecentUnpinnedLimit = 24;
constexpr size_t kHubPinLimit = 12;
constexpr size_t kRecentLimit = kRecentUnpinnedLimit + kHubPinLimit;
std::vector<std::wstring> ReadHubPins();
bool SaveHubPaths(const std::filesystem::path& file,const std::vector<std::wstring>& paths);

KuboHub::UniqueWinHandle LockHubHistory()
{
    const auto folder = g_recentFile.parent_path();
    std::error_code error;
    std::filesystem::create_directories(folder,error);
    if (error) return {};
    const auto path = folder / L"hub_workspace.lock";
    for (unsigned attempt=0; attempt<5; ++attempt) {
        const HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
            0, nullptr, OPEN_ALWAYS, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle != INVALID_HANDLE_VALUE) {
            BY_HANDLE_FILE_INFORMATION info{};
            KuboHub::UniqueWinHandle owned(handle);
            if (GetFileInformationByHandle(handle,&info) &&
                !(info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) &&
                info.nNumberOfLinks == 1 && !info.nFileSizeHigh && !info.nFileSizeLow)
                return owned;
            return {};
        }
        if (GetLastError() != ERROR_SHARING_VIOLATION) return {};
        if (attempt < 4) Sleep(10);
    }
    return {};
}

// Enforce the bound while reading, not just via a pre-read file_size check.
bool ReadBoundedRecentText(std::istream& input, std::string& text)
{
    text.clear();
    std::array<char, 4096> chunk{};
    while (input) {
        input.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        const auto count = static_cast<size_t>(input.gcount());
        if (count > kRecentTextLimit - text.size()) { text.clear(); return false; }
        text.append(chunk.data(), count);
    }
    if (input.bad() || !input.eof()) { text.clear(); return false; }
    return true;
}

std::filesystem::path GetExecutableDirectory()
{
    wchar_t buffer[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    return length == 0 ? std::filesystem::current_path() : std::filesystem::path(buffer, buffer + length).parent_path();
}

std::filesystem::path GetRecentFile()
{
    wchar_t appData[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return std::filesystem::current_path() / L"KuboLauncher.recent";
    }
    return std::filesystem::path(appData) / L"KuboEngine" / L"recent_projects.json";
}

std::vector<std::wstring> ReadRecent()
{
    std::vector<std::wstring> entries;
    try {
        if (std::filesystem::is_regular_file(g_recentFile)) {
            if (std::filesystem::file_size(g_recentFile) > kRecentTextLimit) return entries;
            std::ifstream input(g_recentFile, std::ios::binary);
            std::string text;
            if (!ReadBoundedRecentText(input, text)) return entries;
            const auto document = crude_json::value::parse(text);
            if (!document.is_array()) return entries;
            for (const auto& value : document.get<crude_json::array>()) {
                if (!value.is_string()) continue;
                const auto& utf8 = value.get<crude_json::string>();
                if (utf8.empty() || utf8.find('\0') != std::string::npos) continue;
                const auto path = std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
                if (path.native().size() > kRecentPathLimit) continue;
                entries.push_back(path.wstring());
                if (entries.size() == kRecentLimit) break;
            }
        } else {
            auto legacy = g_recentFile;
            legacy.replace_extension(L".txt");
            if (!std::filesystem::is_regular_file(legacy) || std::filesystem::file_size(legacy) > kRecentTextLimit) return entries;
            std::wifstream input(legacy);
            std::vector<wchar_t> line(kRecentPathLimit + 2);
            size_t consumed = 0;
            while (entries.size() < 12) {
                input.getline(line.data(), static_cast<std::streamsize>(line.size()));
                const auto count = static_cast<size_t>(input.gcount());
                if (count > kRecentTextLimit - consumed || input.bad() || (input.fail() && !input.eof())) return {};
                consumed += count;
                const std::wstring value(line.data());
                // gcount includes the delimiter; inspect embedded NUL separately.
                const auto characters = count - ((!input.eof() && count) ? 1 : 0);
                if (characters != value.size() || value.size() > kRecentPathLimit) return {};
                if (!value.empty()) entries.push_back(value);
                if (input.eof()) break;
            }
        }
    } catch (const std::exception&) {
        // A corrupt optional recent list must not prevent opening projects.
    }
    return entries;
}

void AddRecent(const std::filesystem::path& project)
{
    auto lock = LockHubHistory();
    if (!lock) return;
    std::vector<std::wstring> entries;
    const std::wstring value = project.lexically_normal().wstring();
    const auto pins = ReadHubPins();
    size_t unpinned = 0;
    const auto append = [&](const std::wstring& path) {
        const bool pinned = std::any_of(pins.begin(),pins.end(),[&](const auto& pin) {
            return CompareStringOrdinal(pin.c_str(),-1,path.c_str(),-1,TRUE)==CSTR_EQUAL;
        });
        if (!pinned && unpinned == kRecentUnpinnedLimit) return;
        if (std::any_of(entries.begin(),entries.end(),[&](const auto& entry) {
            return CompareStringOrdinal(entry.c_str(),-1,path.c_str(),-1,TRUE)==CSTR_EQUAL;
        })) return;
        entries.push_back(path);
        if (!pinned) ++unpinned;
    };
    append(value);
    for (const auto& line : ReadRecent()) {
        if (!line.empty()) append(line);
    }
    SaveHubPaths(g_recentFile,entries);
}

void PopulateRecent();
void SelectHubProject(const std::filesystem::path& project);

bool ChooseProjectFile(std::filesystem::path& result, bool creating)
{
    wchar_t fileName[32768] = L"NewProject.kubo";
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = GetParent(g_recentList);
    dialog.lpstrFile = fileName;
    dialog.nMaxFile = 32768;
    dialog.lpstrTitle=creating?L"新規プロジェクトの保存先":L"KuboEngineのプロジェクトを開く";
    dialog.lpstrFilter = L"KuboEngine Project (*.kubo)\0*.kubo\0All Files (*.*)\0*.*\0\0";
    dialog.lpstrDefExt = L"kubo";
    dialog.nFilterIndex = 1;
    dialog.Flags = OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | (creating ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    if ((creating ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog)) == FALSE) {
        return false;
    }
    result = fileName;
    if (_wcsicmp(result.extension().c_str(), L".kubo") != 0) {
        MessageBoxW(g_recentList, L"Please select a .kubo project file.", L"KuboLauncher", MB_OK | MB_ICONERROR);
        return false;
    }
    return true;
}

bool ResolveSelectedEditor(std::filesystem::path& engine,std::string& error)
{
    if(!g_engineError.empty()){error=g_engineError;return false;}
    engine=g_engineCatalog.active;
    if(engine.empty()) {
        engine=GetExecutableDirectory()/L"KuboEditor.exe";
        std::error_code status;
        if(!std::filesystem::is_regular_file(engine,status))engine=GetExecutableDirectory()/L"KuboEngine.exe";
    }
    std::error_code status;
    if(!std::filesystem::is_regular_file(engine,status)){error="Selected Editor is missing or inaccessible; explicitly select another engine. No automatic fallback.";return false;}
    error.clear();return true;
}

bool ResolveProjectEditor(const std::filesystem::path& project, std::filesystem::path& editor,
    std::string& error)
{
    if (!Engine::Base::PreviewDirectoriesAllowed(project.parent_path(),false))
        return ResolveSelectedEditor(editor,error);
    std::ifstream input(project.parent_path()/L"UserSettings/Hub/editor.txt",std::ios::binary);
    if (!input) return ResolveSelectedEditor(editor,error);
    std::string value;
    if (!ReadBoundedRecentText(input,value) || value.find('\0') != std::string::npos) {
        error = "Project Editor preference is invalid. Clear the project preference from its Hub menu.";
        return false;
    }
    if (value.empty()) return ResolveSelectedEditor(editor,error);
    try {
        if (!g_engineError.empty()) { error = g_engineError; return false; }
        const auto requested = Engine::Base::PathFromUtf8(value);
        const auto bundled = GetExecutableDirectory()/L"KuboEditor.exe";
        const auto registered = std::find_if(g_engineCatalog.editors.begin(),g_engineCatalog.editors.end(),
            [&](const auto& path) { return KuboHub::EngineCatalogFile::Same(path,requested); });
        std::filesystem::path trusted;
        if (KuboHub::EngineCatalogFile::Same(bundled,requested)) trusted = bundled;
        else if (registered != g_engineCatalog.editors.end()) trusted = *registered;
        std::error_code status;
        if (!trusted.empty() && std::filesystem::is_regular_file(trusted,status)) {
            editor = std::move(trusted);
            error.clear();
            return true;
        }
    } catch (...) {}
    error = "Project Editor is unavailable or is not registered. Choose or clear its Editor preference in the Hub.";
    return false;
}

// Read-only declarations. Payload hashes/signatures/ABI are NOT checked here.
void RefreshEngineDetails() noexcept
{
    if(!g_engineDetails)return;
    try{
        std::filesystem::path editor;std::string error;
        if(!ResolveSelectedEditor(editor,error)){const auto message=L"Selected Editor unavailable: "+NativeCreationMessage(error);SetWindowTextW(g_engineDetails,message.c_str());return;}
        Engine::Services::EnginePackageManifest package;
        const auto result=Engine::Services::EnginePackageManifest::Read(editor.parent_path()/L"engine-package.json",package,error);
        if(result==Engine::Services::EnginePackageManifest::ReadResult::Missing){SetWindowTextW(g_engineDetails,L"Unversioned/manual Editor: no package declaration.\r\nIntegrity, signature and ABI compatibility are not verified.");return;}
        if(result!=Engine::Services::EnginePackageManifest::ReadResult::Success){SetWindowTextW(g_engineDetails,L"Package declaration is invalid or unreadable; version is unknown.\r\nSelection was not changed. Integrity/signature/ABI are not verified.");return;}
        if(CompareStringOrdinal(editor.filename().c_str(),-1,L"KuboEditor.exe",-1,TRUE)!=CSTR_EQUAL){SetWindowTextW(g_engineDetails,L"Manual Editor is not the declared package entry point; version is unknown.\r\nIntegrity, signature and ABI compatibility are not verified.");return;}
        if(!package.Versioned()){SetWindowTextW(g_engineDetails,L"Legacy unversioned package (format 1).\r\nIntegrity, signature and ABI compatibility are not verified.");return;}
        const auto message=L"Package-declared version: "+NativeCreationMessage(package.engineVersion)+L" / Windows-x64\r\nMetadata only: integrity, signature and ABI compatibility are not verified.";
        SetWindowTextW(g_engineDetails,message.c_str());
    }catch(...){SetWindowTextW(g_engineDetails,L"Engine details unavailable. Selection was not changed.");}
}

void QueueEngineDetails() noexcept
{
    if(!g_engineDetails || !g_engineList)return;
    SetWindowTextW(g_engineDetails,L"Reading package declaration (not an integrity or compatibility check)...");
    if(!PostMessageW(GetAncestor(g_engineList,GA_ROOT),kRefreshEngineDetails,0,0))SetWindowTextW(g_engineDetails,L"Engine details unavailable; restart Hub to refresh.");
}

size_t SelectedEngineIndex(const KuboHub::EngineCatalog& catalog)
{
    for(size_t i=0;i<catalog.editors.size();++i)
        if(KuboHub::EngineCatalogFile::Same(catalog.active,catalog.editors[i]))return i+1;
    return 0;
}

void DisableEngineList() noexcept
{
    if(!g_engineList)return;
    if(g_engineDetails)SetWindowTextW(g_engineDetails,L"Engine list unavailable; restart Hub. No version information is shown.");
    SendMessageW(g_engineList,CB_RESETCONTENT,0,0);
    SendMessageW(g_engineList,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Engine list unavailable; restart Hub"));
    SendMessageW(g_engineList,CB_SETCURSEL,0,0);
    EnableWindow(g_engineList,FALSE);
    EnableWindow(GetDlgItem(GetParent(g_engineList),kRemoveEngine),FALSE);
}

// The index and all owning catalog data are prepared before disk publication.
// Borrow native path text; no wstring copies/lexical normalization after commit.
// Win32 control allocation refusal disables the list without rolling back data.
void PopulateEngines(size_t selected) noexcept
{
    if(!g_engineList)return;
    SendMessageW(g_engineList,CB_RESETCONTENT,0,0);
    const auto bundled=SendMessageW(g_engineList,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Bundled Editor (beside this Hub)"));
    if(bundled==CB_ERR || bundled==CB_ERRSPACE){DisableEngineList();return;}
    for(size_t i=0;i<g_engineCatalog.editors.size();++i) {
        const auto added=SendMessageW(g_engineList,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(g_engineCatalog.editors[i].c_str()));
        if(added==CB_ERR || added==CB_ERRSPACE){DisableEngineList();return;}
    }
    SendMessageW(g_engineList,CB_SETCURSEL,selected,0);
    EnableWindow(g_engineList,g_engineError.empty());
    EnableWindow(GetDlgItem(GetParent(g_engineList),kRemoveEngine),g_engineError.empty() && !g_engineCatalog.active.empty());
    QueueEngineDetails();
}

void PopulateEngines() noexcept
{
    try{PopulateEngines(SelectedEngineIndex(g_engineCatalog));}
    catch(...){DisableEngineList();}
}

bool CommitEngineSelection(const KuboHub::EngineCatalog& candidate,std::string& error)
{
    try{
        if(!g_engineError.empty()){error=g_engineError;return false;}
        auto next=candidate;
        const auto selected=SelectedEngineIndex(next);
        if(!KuboHub::EngineCatalogFile::SaveChecked(g_engineFile,next,g_engineSnapshot,error))return false;
        g_engineCatalog.Swap(next);PopulateEngines(selected);return true;
    }catch(const std::exception& exception){error=exception.what();return false;}
}

bool RemoveActiveEngineRegistration(std::string& error)
{
    if(g_engineCatalog.active.empty()){error="The bundled Editor has no external registration to remove";return false;}
    auto candidate=g_engineCatalog;
    for(auto entry=candidate.editors.begin();entry!=candidate.editors.end();++entry) {
        if(KuboHub::EngineCatalogFile::Same(*entry,candidate.active)){candidate.editors.erase(entry);break;}
    }
    candidate.active.clear();return CommitEngineSelection(candidate,error);
}

void ChooseEngine(HWND owner)
{
    if(!g_engineError.empty()){MessageBoxW(owner,NativeCreationMessage(g_engineError).c_str(),L"Engine catalog error",MB_OK|MB_ICONERROR);return;}
    std::vector<wchar_t> file(32768);
    OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=owner;
    dialog.lpstrFile=file.data();dialog.nMaxFile=static_cast<DWORD>(file.size());
    dialog.lpstrTitle=L"Register an installed, trusted Kubo Editor";
    dialog.lpstrFilter=L"Editor executable (*.exe)\0*.exe\0\0";
    dialog.Flags=OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|OFN_FILEMUSTEXIST;
    if(!GetOpenFileNameW(&dialog))return;
    const auto editor=std::filesystem::path(file.data()).lexically_normal();
    if(!KuboHub::EngineCatalogFile::IsEditorPath(editor)){MessageBoxW(owner,L"Select KuboEditor.exe or KuboEngine.exe.",L"Register Engine",MB_OK|MB_ICONERROR);return;}
    if(MessageBoxW(owner,L"Register and select this trusted executable?\nOpening a project will execute it. Version compatibility and signatures are not verified.",L"Register Engine",MB_YESNO|MB_ICONWARNING)!=IDYES)return;
    auto candidate=g_engineCatalog;bool found=false;
    for(const auto& path:candidate.editors)if(KuboHub::EngineCatalogFile::Same(path,editor))found=true;
    if(!found)candidate.editors.push_back(editor);
    candidate.active=editor;std::string error;
    if(!CommitEngineSelection(candidate,error))MessageBoxW(owner,NativeCreationMessage(error).c_str(),L"Engine selection failed",MB_OK|MB_ICONERROR);
}

bool LaunchProject(const std::filesystem::path& project)
{
    const HWND owner = GetParent(g_recentList);
    Engine::Base::ProjectContext context;
    std::string error;
    if (!context.Open(project, error)) {
        MessageBoxA(g_recentList, error.c_str(), "KuboLauncher project error", MB_OK | MB_ICONERROR);
        return false;
    }
    std::filesystem::path engine;
    if (!ResolveProjectEditor(context.GetProjectFile(),engine,error)) {
        MessageBoxW(g_recentList,NativeCreationMessage(error).c_str(),L"KuboLauncher engine error",MB_OK|MB_ICONERROR);
        return false;
    }
    for (const auto& current : g_editorSessions) {
        if (KuboHub::EngineCatalogFile::Same(current.project, context.GetProjectFile()) &&
            WaitForSingleObject(current.process.get(), 0) == WAIT_TIMEOUT) {
            SetEditorSessionStatus(owner, current.name + L"\r\nエディタを起動済みです。");
            return false;
        }
    }
    KuboHub::EditorSession session;
    session.project = context.GetProjectFile();
    session.name = NativeCreationMessage(context.GetSettings().name);
    if (!SetTimer(owner, kEditorSessionTimer, 100, nullptr)) {
        SetEditorSessionStatus(owner, L"エディタの起動状況を監視できません。");
        return false;
    }
    const DWORD launchError = KuboHub::StartEditor(engine, IsWindowVisible(owner) != FALSE, session);
    if (launchError != ERROR_SUCCESS) {
        if (g_editorSessions.empty()) KillTimer(owner, kEditorSessionTimer);
        SetEditorSessionStatus(owner, L"エディタを起動できませんでした。");
        const auto message=L"選択したエディタを起動できませんでした。\n"+engine.wstring()+L"\nWindows error: "+std::to_wstring(launchError);
        MessageBoxW(owner,message.c_str(),L"KuboEngine Hub",MB_OK|MB_ICONERROR);
        return false;
    }
    SetEditorSessionStatus(owner, session.name + L"\r\nエディタを準備しています…");
    g_editorSessions.push_back(std::move(session));
    AddRecent(context.GetProjectFile());
    PopulateRecent();
    SelectHubProject(context.GetProjectFile());
    return true;
}

bool GetSelectedProject(std::filesystem::path& result)
{
    if(!g_recentList) return false;
    const LRESULT index = SendMessageW(g_recentList, LB_GETCURSEL, 0, 0);
    if (index == LB_ERR) {
        return false;
    }
    const LRESULT length = SendMessageW(g_recentList, LB_GETTEXTLEN, index, 0);
    if (length == LB_ERR || length < 0) return false;
    std::vector<wchar_t> buffer(static_cast<size_t>(length) + 1);
    if (SendMessageW(g_recentList, LB_GETTEXT, index, reinterpret_cast<LPARAM>(buffer.data())) == LB_ERR) return false;
    result = buffer.data();
    return true;
}

// Reading a selection never launches, migrates or rewrites its manifest/history.
// engineVersion is author-supplied metadata, not a verified ABI capability.
std::wstring HubDisplayPath(const std::filesystem::path& path)
{
    wchar_t compact[96]{};
    return PathCompactPathExW(compact,path.c_str(),96,0)?std::wstring(compact):path.wstring();
}
void RefreshProjectDetails() noexcept
{
    if(!g_projectDetails) return;
    try {
        std::filesystem::path project;
        if(!GetSelectedProject(project)) {
            SetWindowTextW(g_projectDetails,L"プロジェクトを選択してください。\r\n\r\n名前、保存場所、開始シーンと、起動するエディタをここに表示します。");
            return;
        }
        Engine::Base::ProjectContext context;std::string error;
        if(!context.Open(project,error)) {
            const auto text=L"読み込めないプロジェクト\r\n"+project.wstring()+L"\r\n\r\nProject cannot be read: "+NativeCreationMessage(error);
            SetWindowTextW(g_projectDetails,text.c_str());
            return;
        }
        const auto& settings=context.GetSettings();
        std::filesystem::path editor;std::string editorError;
        const auto target=ResolveProjectEditor(project,editor,editorError)?HubDisplayPath(editor):L"エディタを利用できません。右クリックからプロジェクトのエディタ指定を変更できます。";
        const auto text=NativeCreationMessage(settings.name)+L"\r\n\r\n保存場所\r\n"+HubDisplayPath(project)+
            L"\r\n\r\n作成時のバージョン情報\r\nengineVersion: "+NativeCreationMessage(settings.engineVersion)+
            L"\r\n\r\n開始シーン\r\n"+NativeCreationMessage(settings.startupScene)+L"\r\n\r\n起動するエディタ\r\n"+target+
            L"\r\n\r\nバージョンは申告情報です。互換性は未検証です。\r\ncompatibility is not verified.";
        SetWindowTextW(g_projectDetails,text.c_str());
    } catch(...) {
        SetWindowTextW(g_projectDetails,L"Project inspection failed. No project changes were made.");
    }
}

bool ChooseNativeSdk(std::filesystem::path& sdk)
{
    wchar_t filename[32768]{};
    OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=g_recentList;
    dialog.lpstrFile=filename;dialog.nMaxFile=32768;
    dialog.lpstrTitle=L"Select sdk-package.json from a trusted Runtime SDK";
    dialog.lpstrFilter=L"Runtime SDK manifest (sdk-package.json)\0sdk-package.json\0\0";
    dialog.Flags=OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|OFN_FILEMUSTEXIST;
    if(!GetOpenFileNameW(&dialog)) return false;
    const auto path=std::filesystem::path(filename);
    if(_wcsicmp(path.filename().c_str(),L"sdk-package.json")!=0) return false;
    sdk=path.parent_path();return true;
}

std::filesystem::path ManagedEngineStore()
{
    wchar_t local[32768]{};const auto length=GetEnvironmentVariableW(L"LOCALAPPDATA",local,32768);
    if(!length || length>=32768)throw std::runtime_error("LOCALAPPDATA is unavailable; no engine store was selected");
    return std::filesystem::path(local)/L"KuboEngine/Engines";
}

bool SelectLastInstalled(std::string& error)
{
    if(g_lastInstalledEditor.empty() || !std::filesystem::is_regular_file(g_lastInstalledEditor)){error="Installed Editor is missing; no selection change";return false;}
    auto candidate=g_engineCatalog;bool found=false;
    for(const auto& editor:candidate.editors)if(KuboHub::EngineCatalogFile::Same(editor,g_lastInstalledEditor))found=true;
    if(!found)candidate.editors.push_back(g_lastInstalledEditor);
    candidate.active=g_lastInstalledEditor;return CommitEngineSelection(candidate,error);
}

void FinishPackageInstallation(HWND window) noexcept
{
    try {
        if(!g_installationJob.Ready()) {
            const auto text=L"Installing / recovering without selection or launch: "+std::to_wstring(g_installationJob.Progress())+L" bytes processed.\r\nCancel is cooperative; any pending journal/candidate will be kept.";
            SetWindowTextW(g_verificationDetails,text.c_str());return;
        }
        auto outcome=g_installationJob.Take();
        using R=KuboHub::EnginePackageInstallation::Result;
        if(outcome.result==R::Installed || outcome.result==R::AlreadyPresent){
            auto editor=outcome.report.directory/L"KuboEditor.exe";
            const auto text=L"Package available at: "+outcome.report.directory.native()+L"\r\nSelection unchanged. Use Select Installed explicitly; old versions are kept.\r\nPublisher/signature/ABI NOT verified."+
                std::wstring(outcome.report.journalRetained?L" Recovery journal cleanup remains.":L"");
            g_lastInstalledEditor.swap(editor);SetWindowTextW(g_verificationDetails,text.c_str());EnableWindow(GetHubControl(window,kSelectInstalled),TRUE);
        }else {
            const auto text=L"Installation not published: "+NativeCreationMessage(outcome.error)+L"\r\nExisting versions/selection unchanged; pending files, if any, are retained.";
            SetWindowTextW(g_verificationDetails,text.c_str());
        }
    }catch(...) {SetWindowTextW(g_verificationDetails,L"Operation ended but result display is unavailable. Inspect the managed store; selection was not changed.");}
    if(!g_installationJob.Active()){
        KillTimer(window,kVerificationTimer);for(const auto id:{kVerifyPackage,kInstallPackage,kRecoverInstallation})EnableWindow(GetHubControl(window,id),TRUE);
        EnableWindow(GetHubControl(window,kCancelVerification),FALSE);if(g_closeAfterVerification)DestroyWindow(window);
    }
}

bool BeginPackageInstallation(HWND window,const std::filesystem::path& manifest,const std::filesystem::path& store,const std::string& recoveryToken={}) noexcept
{
    if(g_installationJob.Active() || g_verificationJob.Active() || g_closeAfterVerification)return false;
    try {
        if(!SetTimer(window,kVerificationTimer,100,nullptr))throw std::runtime_error("Cannot schedule installation result");
        const bool started=recoveryToken.empty()?g_installationJob.StartInstall(manifest,store):g_installationJob.StartRecovery(store,recoveryToken);
        if(!started){KillTimer(window,kVerificationTimer);return false;}
        g_lastInstalledEditor.clear();EnableWindow(GetHubControl(window,kSelectInstalled),FALSE);
        for(const auto id:{kVerifyPackage,kInstallPackage,kRecoverInstallation})EnableWindow(GetHubControl(window,id),FALSE);
        EnableWindow(GetHubControl(window,kCancelVerification),TRUE);
        SetWindowTextW(g_verificationDetails,L"Installing/recovering confirmed local package. No selection change or launch.");return true;
    }catch(...) {KillTimer(window,kVerificationTimer);SetWindowTextW(g_verificationDetails,L"Installation could not start. No selection change.");return false;}
}

void ChoosePackageInstallation(HWND window,bool recover) noexcept
{
    try {
        const auto store=ManagedEngineStore();wchar_t filename[32768]{};OPENFILENAMEW dialog{};
        dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=window;dialog.lpstrFile=filename;dialog.nMaxFile=32768;
        dialog.lpstrTitle=recover?L"Select a pending installation journal from the managed store":L"Install a local Portable package without replacing old versions";
        dialog.lpstrFilter=recover?L"Installation journal\0pending-*.json\0\0":L"Engine package manifest\0engine-package.json\0\0";
        if(recover)dialog.lpstrInitialDir=store.c_str();dialog.Flags=OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|OFN_FILEMUSTEXIST;
        if(!GetOpenFileNameW(&dialog))return;const std::filesystem::path selected(filename);
        std::string token;
        if(recover){
            if(!KuboHub::EngineCatalogFile::Same(selected.parent_path(),store))throw std::runtime_error("Select a journal inside the managed engine store");
            const auto name=selected.filename().wstring();if(name.size()!=45 || !name.starts_with(L"pending-") || !name.ends_with(L".json"))throw std::runtime_error("Unsupported recovery journal name");
            for(auto c:name.substr(8,32)){if(c>127)throw std::runtime_error("Unsupported recovery token");token+=static_cast<char>(c);}
        }
        if(MessageBoxW(window,L"Proceed only if you trust this local package source.\nChecksums do NOT authenticate its publisher/signature or project compatibility.\nNo engine will be launched or selected; existing versions/projects are kept.",L"Confirm local package install / recovery",MB_YESNO|MB_ICONWARNING)!=IDYES)return;
        BeginPackageInstallation(window,recover?std::filesystem::path{}:selected,store,token);
    }catch(const std::exception& e){try{SetWindowTextW(g_verificationDetails,NativeCreationMessage(e.what()).c_str());}catch(...){SetWindowTextW(g_verificationDetails,L"Cannot inspect installation request.");}}
}

void FinishPackageVerification(HWND window) noexcept
{
    if(g_installationJob.Active()){FinishPackageInstallation(window);return;}
    try {
        if(!g_verificationJob.Active())return;
        if(!g_verificationJob.Ready()) {
            const auto text=L"Verifying package (no install/launch): "+std::to_wstring(g_verificationJob.Progress())+L" bytes read.\r\nCancel is cooperative between Windows reads.";
            SetWindowTextW(g_verificationDetails,text.c_str());return;
        }
        auto outcome=g_verificationJob.Take();
        if(outcome.result==KuboHub::EnginePackageVerification::Result::Success) {
            const auto text=L"Manifest checksums match: "+std::to_wstring(outcome.report.payloadFiles)+L" files / "+std::to_wstring(outcome.report.totalBytes)+L" bytes.\r\nDeclared version: "+
                (outcome.report.declaredVersion.empty()?std::wstring(L"legacy / unversioned"):NativeCreationMessage(outcome.report.declaredVersion))+
                L".\r\nNOT publisher/signature/ABI verification. Not installed or selected.";
            SetWindowTextW(g_verificationDetails,text.c_str());
        }else {
            const auto text=L"Package not verified: "+NativeCreationMessage(outcome.error)+L"\r\nNo install, selection change or project write was performed.";
            SetWindowTextW(g_verificationDetails,text.c_str());
        }
    }catch(...) {SetWindowTextW(g_verificationDetails,L"Package inspection failed. No install or selection change was performed.");}
    if(!g_verificationJob.Active()) {
        KillTimer(window,kVerificationTimer);
        EnableWindow(GetHubControl(window,kVerifyPackage),TRUE);
        EnableWindow(GetHubControl(window,kInstallPackage),TRUE);EnableWindow(GetHubControl(window,kRecoverInstallation),TRUE);
        EnableWindow(GetHubControl(window,kCancelVerification),FALSE);
        if(g_closeAfterVerification)DestroyWindow(window);
    }
}

bool BeginPackageVerification(HWND window,const std::filesystem::path& manifest) noexcept
{
    if(g_verificationJob.Active() || g_installationJob.Active() || g_closeAfterVerification)return false;
    try {
        if(!SetTimer(window,kVerificationTimer,100,nullptr))throw std::runtime_error("Cannot schedule package verification completion");
        if(!g_verificationJob.Start(manifest)){KillTimer(window,kVerificationTimer);return false;}
        EnableWindow(GetHubControl(window,kVerifyPackage),FALSE);
        EnableWindow(GetHubControl(window,kInstallPackage),FALSE);EnableWindow(GetHubControl(window,kRecoverInstallation),FALSE);
        EnableWindow(GetHubControl(window,kCancelVerification),TRUE);
        SetWindowTextW(g_verificationDetails,L"Verifying selected package only; no install, selection change or launch.");return true;
    }catch(...) {
        KillTimer(window,kVerificationTimer);
        SetWindowTextW(g_verificationDetails,L"Could not start package verification. No changes were made.");return false;
    }
}

void ChoosePackageForVerification(HWND window) noexcept
{
    try {
        wchar_t filename[32768]{};OPENFILENAMEW dialog{};
        dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=window;dialog.lpstrFile=filename;dialog.nMaxFile=32768;
        dialog.lpstrTitle=L"Verify a Portable package (does not install or run it)";
        dialog.lpstrFilter=L"Engine package manifest\0engine-package.json\0\0";
        dialog.Flags=OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|OFN_FILEMUSTEXIST;
        if(GetOpenFileNameW(&dialog))BeginPackageVerification(window,std::filesystem::path(filename));
    }catch(...) {SetWindowTextW(g_verificationDetails,L"Could not inspect the selected package.");}
}

#include "HubPresentation.h"
#include "HubUpdatePresentation.h"
#include "HubImport.h"

void SetEditorSessionStatus(HWND owner, const std::wstring& text)
{
    SetWindowTextW(GetDlgItem(owner, kEditorSessionStatus), text.c_str());
}

void PollEditorSessions(HWND window)
{
    for (auto session = g_editorSessions.begin(); session != g_editorSessions.end();) {
        const DWORD processState = WaitForSingleObject(session->process.get(), 0);
        if (processState == WAIT_OBJECT_0 || processState == WAIT_FAILED) {
            DWORD exitCode = 0;
            const bool known = GetExitCodeProcess(session->process.get(), &exitCode) != FALSE;
            const bool normal = known && exitCode == 0 && session->confirmed;
            auto status = session->name + (normal ? L"\r\nエディタを閉じました。"
                : session->confirmed ? L"\r\nエディタが終了しました。"
                : L"\r\n準備完了の通知前にエディタが終了しました。");
            if (known && !normal) status += L"\r\n終了コード " + std::to_wstring(exitCode);
            SetEditorSessionStatus(window, status);
            if (session->minimizedHub && IsIconic(window)) {
                ShowWindow(window, SW_RESTORE);
                SelectHubProject(session->project);
            }
            session = g_editorSessions.erase(session);
            continue;
        }
        if (!session->confirmed && WaitForSingleObject(session->ready.get(), 0) == WAIT_OBJECT_0) {
            session->confirmed = true;
            session->ready.reset();
            KuboHub::RecordProjectOpened(session->project);
            SetEditorSessionStatus(window, session->name + L"\r\nエディタを開きました。");
            const HWND foreground = GetForegroundWindow();
            DWORD foregroundProcess = 0;
            GetWindowThreadProcessId(foreground, &foregroundProcess);
            if (IsWindowVisible(window) && !IsIconic(window) &&
                (foreground == window || foregroundProcess == GetProcessId(session->process.get()))) {
                session->minimizedHub = true;
                ShowWindow(window, SW_MINIMIZE);
            }
        } else if (!session->confirmed && !session->delayed && GetTickCount64() - session->started >= 20000) {
            session->delayed = true;
            SetEditorSessionStatus(window, session->name +
                L"\r\n準備通知を待っています。時間がかかる場合や旧版では、エディタの画面を確認してください。");
        }
        ++session;
    }
    if (g_editorSessions.empty()) KillTimer(window, kEditorSessionTimer);
}

KuboHub::WindowPlacement g_hubWindowPlacement;
bool g_saveHubPlacement = false;

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_CREATE:
        g_closeAfterVerification=false;
        CreateHubControls(window);
        PopulateEngines();
        PopulateRecent();
        return 0;
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) LayoutHub(window);
        return 0;
    case WM_WINDOWPOSCHANGED:
        if (g_recentList && !IsIconic(window)) {
            g_hubWindowPlacement.maximized = IsZoomed(window) != FALSE;
            if (!g_hubWindowPlacement.maximized) {
                GetWindowRect(window, &g_hubWindowPlacement.normal);
                g_hubWindowPlacement.dpi = GetDpiForWindow(window);
            }
        }
        break;
    case WM_GETMINMAXINFO: {
        auto info=reinterpret_cast<MINMAXINFO*>(lParam);
        MONITORINFO monitor{sizeof(monitor)};
        const auto nearest = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
        const UINT dpi = GetDpiForWindow(window);
        const auto pixels = [&](int value) { return MulDiv(value, dpi ? dpi : g_hubDpi, 96); };
        info->ptMinTrackSize = {pixels(560), pixels(360)};
        if (GetMonitorInfoW(nearest, &monitor)) {
            info->ptMinTrackSize.x = (std::min)(info->ptMinTrackSize.x, monitor.rcWork.right - monitor.rcWork.left);
            info->ptMinTrackSize.y = (std::min)(info->ptMinTrackSize.y, monitor.rcWork.bottom - monitor.rcWork.top);
        }
        return 0;
    }
    case WM_DPICHANGED: {
        g_hubDpi=HIWORD(wParam);UpdateHubFonts(window);
        const auto bounds=reinterpret_cast<RECT*>(lParam);
        if (!IsZoomed(window) && !IsIconic(window))
            SetWindowPos(window,nullptr,bounds->left,bounds->top,bounds->right-bounds->left,bounds->bottom-bounds->top,SWP_NOZORDER|SWP_NOACTIVATE);
        LayoutHub(window);return 0;
    }
    case WM_VSCROLL:
        if (!lParam || reinterpret_cast<HWND>(lParam)==GetDlgItem(window,kHubPageScroll)) {
            ScrollHub(window, LOWORD(wParam)); return 0;
        }
        break;
    case WM_MOUSEWHEEL:
        if (HandleHubEngineWheel(window,wParam,lParam)) return 0;
        break;
    case WM_PAINT: PaintHub(window);return 0;
    case WM_ERASEBKGND: return 1;
    case WM_CONTEXTMENU:
        if(reinterpret_cast<HWND>(wParam)==g_recentList){HubProjectMenu(window,lParam);return 0;}break;
    case WM_DROPFILES: {
        const auto drop = reinterpret_cast<HDROP>(wParam);
        const UINT count = DragQueryFileW(drop,0xFFFFFFFF,nullptr,0);
        std::filesystem::path project;
        if (count == 1) {
            const UINT size = DragQueryFileW(drop,0,nullptr,0);
            if (size && size <= kRecentPathLimit) {
                std::wstring path(static_cast<size_t>(size)+1,L'\0');
                if (DragQueryFileW(drop,0,path.data(),size+1) == size) {
                    path.resize(size);
                    project = path;
                }
            }
        }
        DragFinish(drop);
        if (!project.empty() && LaunchProject(project)) SwitchHubPage(window,HubPage::Projects);
        return 0;
    }
    case WM_DRAWITEM: DrawHubItem(*reinterpret_cast<DRAWITEMSTRUCT*>(lParam));return TRUE;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORLISTBOX: {
        const auto child = reinterpret_cast<HWND>(lParam);
        auto dc=reinterpret_cast<HDC>(wParam);SetTextColor(dc,kHubText);SetBkColor(dc,kHubPanel);
        if (child == g_recentList) {
            SetBkColor(dc,kHubBackground);
            SetDCBrushColor(dc,kHubBackground);
            return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
        }
        if (child == GetDlgItem(window,kProjectCount) || child == GetDlgItem(window,kProjectEmpty) ||
            child == GetDlgItem(window,kEditorSessionStatus)) {
            const auto color = child == GetDlgItem(window,kEditorSessionStatus) ? kHubSidebar : kHubBackground;
            SetBkColor(dc,color);
            SetDCBrushColor(dc,color);
            return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
        }
        return reinterpret_cast<LRESULT>(g_hubPanelBrush);
    }
    case WM_COMMAND:
        if (LOWORD(wParam)==kImportStart && HIWORD(wParam)==BN_CLICKED) {BeginHubImport(window);return 0;}
        if (HIWORD(wParam) == BN_CLICKED && HandleHubUpdateCommand(window, LOWORD(wParam))) return 0;
        if(HandleHubCommand(window,LOWORD(wParam),HIWORD(wParam)))return 0;
        if (g_updateInstallation.valid() && (LOWORD(wParam)==kVerifyPackage ||
            LOWORD(wParam)==kInstallPackage || LOWORD(wParam)==kRecoverInstallation)) return 0;
        if(LOWORD(wParam)==kVerifyPackage){ChoosePackageForVerification(window);return 0;}
        if(LOWORD(wParam)==kCancelVerification){g_verificationJob.Cancel();g_installationJob.Cancel();return 0;}
        if(LOWORD(wParam)==kInstallPackage || LOWORD(wParam)==kRecoverInstallation){ChoosePackageInstallation(window,LOWORD(wParam)==kRecoverInstallation);return 0;}
        if(LOWORD(wParam)==kSelectInstalled){
            if(MessageBoxW(window,L"Select this installed Editor for subsequent launches?\nCompatibility/signatures are not verified. No engine starts now.\nOld registrations stay available for explicit rollback.",L"Select installed Editor",MB_YESNO|MB_ICONWARNING)!=IDYES)return 0;
            try{std::string error;if(!SelectLastInstalled(error))MessageBoxW(window,NativeCreationMessage(error).c_str(),L"Selection failed; installed files kept",MB_OK|MB_ICONERROR);}catch(...){MessageBoxW(window,L"Selection could not be saved. Installed files are kept.",L"Select Engine",MB_OK|MB_ICONERROR);}return 0;
        }
        if(LOWORD(wParam)==kRemoveEngine) {
            if(g_engineCatalog.active.empty() || !g_engineError.empty())return 0;
            if(MessageBoxW(window,L"Remove the selected registration and use the bundled Editor?\nNo engine files or projects will be deleted.",L"Remove Registration",MB_YESNO|MB_ICONQUESTION)!=IDYES)return 0;
            std::string error;
            if(!RemoveActiveEngineRegistration(error))MessageBoxW(window,NativeCreationMessage(error).c_str(),L"Engine selection failed",MB_OK|MB_ICONERROR);
            return 0;
        }
        if(LOWORD(wParam)==kAddEngine){ChooseEngine(window);return 0;}
        if(LOWORD(wParam)==kEngineList && HIWORD(wParam)==CBN_SELCHANGE) {
            const auto index=SendMessageW(g_engineList,CB_GETCURSEL,0,0);
            if(index!=CB_ERR && index>=0 && static_cast<size_t>(index)<=g_engineCatalog.editors.size()) {
                auto candidate=g_engineCatalog;
                candidate.active=index ? candidate.editors[static_cast<size_t>(index)-1] : std::filesystem::path{};
                std::string error;
                if(!CommitEngineSelection(candidate,error)){PopulateEngines();MessageBoxW(window,NativeCreationMessage(error).c_str(),L"Engine selection failed",MB_OK|MB_ICONERROR);}
            }
            return 0;
        }
        if(LOWORD(wParam)==kRecentList && HIWORD(wParam)==LBN_SELCHANGE) {
            RefreshProjectDetails();
            RefreshHubActions(window);
            LayoutHub(window);
            return 0;
        }
        if (LOWORD(wParam) == kNativeButton) {
            g_hubNative=true;g_hubSprite=false;SwitchHubPage(window,HubPage::Create);
            return 0;
        }
        if (LOWORD(wParam) == kOpenButton) {
            std::filesystem::path project;
            if (ChooseProjectFile(project, false)) {
                if(LaunchProject(project))SwitchHubPage(window,HubPage::Projects);
            }
            return 0;
        }
        if (LOWORD(wParam) == kNewButton) {
            SwitchHubPage(window,HubPage::Create);
            return 0;
        }
        if (LOWORD(wParam) == kLaunchButton ||
            (LOWORD(wParam) == kRecentList && HIWORD(wParam) == LBN_DBLCLK)) {
            std::filesystem::path project;
            if (GetSelectedProject(project)) {
                LaunchProject(project);
            }
            return 0;
        }
        if (LOWORD(wParam) == kExitButton) {
            SendMessageW(window,WM_CLOSE,0,0);
            return 0;
        }
        break;
    case kRefreshEngineDetails:
        RefreshEngineDetails();RefreshProjectDetails();RefreshHubUpdateSelection(window);return 0;
    case WM_TIMER:
        if(wParam==kHubNavTimer){TickHubNavigation(window);return 0;}
        if(wParam==kProjectPreviewTimer){PollHubPreviews(window);PollHubUpdate(window);PollHubImport(window);return 0;}
        if(wParam==kEditorSessionTimer){
            const bool wasLoading = std::any_of(g_editorSessions.begin(),g_editorSessions.end(),
                [](const auto& session) { return !session.confirmed; });
            PollEditorSessions(window);
            if (wasLoading && !IsIconic(window)) {
                const auto track = HubLoadingTrack(window);
                InvalidateRect(window,&track,FALSE);
            }
            return 0;
        }
        if(wParam==kVerificationTimer){FinishPackageVerification(window);return 0;}break;
    case WM_CLOSE:
        if(g_verificationJob.Active() || g_installationJob.Active()) {
            g_closeAfterVerification=true;g_verificationJob.Cancel();g_installationJob.Cancel();
            SetWindowTextW(g_verificationDetails,L"Waiting for the owned package worker to cancel before closing.");return 0;
        }
        DestroyWindow(window);return 0;
    case WM_DESTROY:
        KillTimer(window,kHubNavTimer);
        RetireHubImport();
        RetireHubUpdate();
        KillTimer(window,kProjectPreviewTimer);
        KillTimer(window,kEditorSessionTimer);
        g_editorSessions.clear();
        if (g_saveHubPlacement && g_hubWindowPlacement.Valid())
            KuboHub::SaveWindowPlacement(g_recentFile.parent_path() / L"hub_window.txt", g_hubWindowPlacement);
        g_verificationJob.Cancel();g_installationJob.Cancel();KillTimer(window,kVerificationTimer);g_verificationDetails=nullptr;
        g_recentList=nullptr;g_projectDetails=nullptr;g_engineList=nullptr;g_engineDetails=nullptr;
        DestroyHubPresentation();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int showCommand)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    g_recentFile = GetRecentFile();
    // Headless creation uses the same service as the Hub button; never builds.
    int createCount=0;auto createArgs=CommandLineToArgvW(GetCommandLineW(),&createCount);
    if(createArgs) {
        if(createCount>1 && (std::wstring(createArgs[1])==L"--install-package" || std::wstring(createArgs[1])==L"--recover-installation")) {
            const bool recover=std::wstring(createArgs[1])==L"--recover-installation";
            const bool valid=createCount==7 && std::wstring(createArgs[3])==L"--store" && std::wstring(createArgs[5])==L"--accept-local-package" && std::wstring(createArgs[6])==L"--no-launch";
            KuboHub::InstalledPackageReport report;std::string error;auto result=KuboHub::EnginePackageInstallation::Result::Invalid;
            if(valid){if(recover){const std::wstring wide=createArgs[2];std::string token;for(auto c:wide)token+=c<=127?static_cast<char>(c):'?';result=KuboHub::EnginePackageInstallation::Recover(createArgs[4],token,report,error);}else result=KuboHub::EnginePackageInstallation::Install(createArgs[2],createArgs[4],report,error);}
            LocalFree(createArgs);const bool success=result==KuboHub::EnginePackageInstallation::Result::Installed || result==KuboHub::EnginePackageInstallation::Result::AlreadyPresent;
            if(!success)OutputDebugStringA(valid?error.c_str():"Use --install-package <manifest> or --recover-installation <token>, followed by --store <directory> --accept-local-package --no-launch");return success?0:(valid?1:2);
        }
        if(createCount>1 && std::wstring(createArgs[1])==L"--verify-package") {
            const bool valid=createCount==4 && std::wstring(createArgs[3])==L"--no-launch";
            KuboHub::PackageVerificationReport report;std::string error;
            const bool verified=valid && KuboHub::EnginePackageVerification::Verify(createArgs[2],report,error)==KuboHub::EnginePackageVerification::Result::Success;
            LocalFree(createArgs);
            const auto text=verified?std::string("Package checksums match manifest; NOT publisher/signature/ABI verification. No install or launch.\n"):
                (valid?error+"\n":std::string("Use --verify-package <absolute engine-package.json> --no-launch\n"));
            DWORD written=0;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),text.data(),static_cast<DWORD>(text.size()),&written,nullptr);OutputDebugStringA(text.c_str());
            return verified?0:(valid?1:2);
        }
        if(createCount>1 && std::wstring(createArgs[1])==L"--create-project") {
            const bool blank=createCount==4 && std::wstring(createArgs[3])==L"--no-launch";
            const bool sprite=createCount==6 && std::wstring(createArgs[3])==L"--template" &&
                std::wstring(createArgs[4])==L"sprite2d" && std::wstring(createArgs[5])==L"--no-launch";
            const bool valid=blank || sprite;
            Engine::Base::ProjectContext context;std::string error;
            const bool created=valid && Engine::Services::ProjectCreate::Create(createArgs[2],context,error,
                sprite ? Engine::Services::ProjectStarter::Sprite2D : Engine::Services::ProjectStarter::Empty);
            LocalFree(createArgs);
            if(!valid)OutputDebugStringA("Use --create-project <new.kubo> [--template sprite2d] --no-launch");
            else if(!error.empty())OutputDebugStringW(NativeCreationMessage(error).c_str());
            return created ? 0 : 1;
        }
        const bool createOnly=createCount==6 && std::wstring(createArgs[1])==L"--create-native" &&
            std::wstring(createArgs[3])==L"--sdk" && std::wstring(createArgs[5])==L"--no-launch";
        if(createOnly) {
            Engine::Base::ProjectContext context;std::string error;
            const bool created=Engine::Services::NativeProjectCreate::Create(createArgs[2],createArgs[4],context,error);
            LocalFree(createArgs);if(!error.empty()) OutputDebugStringW(NativeCreationMessage(error).c_str());return created ? 0 : 1;
        }
        if(createCount>1 && std::wstring(createArgs[1])==L"--create-native") {
            LocalFree(createArgs);OutputDebugStringA("Use --create-native <new.kubo> --sdk <SDK directory> --no-launch");return 1;
        }
        LocalFree(createArgs);
    }
    g_engineFile=GetRecentFile().parent_path()/L"engine_catalog.json";
    KuboHub::EngineCatalogFile::Load(g_engineFile,g_engineCatalog,g_engineSnapshot,g_engineError);
    const wchar_t* className = L"KuboLauncherWindow";
    WNDCLASSW windowClass{};
    windowClass.hInstance = instance;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.lpszClassName = className;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = Engine::Branding::LoadEngineIcon(instance);
    RegisterClassW(&windowClass);

    const auto saved = KuboHub::ReadWindowPlacement(g_recentFile.parent_path() / L"hub_window.txt");
    POINT pointer{};
    GetCursorPos(&pointer);
    const auto monitor = saved.Valid() ? MonitorFromRect(&saved.normal, MONITOR_DEFAULTTONEAREST)
        : MonitorFromPoint(pointer, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO monitorInfo{sizeof(monitorInfo)};
    RECT workArea{};
    if (GetMonitorInfoW(monitor, &monitorInfo)) workArea = monitorInfo.rcWork;
    else SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    UINT dpiX = 96, dpiY = 96;
    if (FAILED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY))) dpiX = GetDpiForSystem();
    g_hubDpi = dpiX;
    const RECT initial = KuboHub::FitWindowToWorkArea(saved, workArea, dpiX);
    g_hubWindowPlacement = {initial, dpiX, saved.maximized};
    HWND window = CreateWindowW(className, L"KuboEngine Hub",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        initial.left, initial.top, initial.right-initial.left, initial.bottom-initial.top,
        nullptr, nullptr, instance, nullptr);
    if (!window) {
        return 1;
    }
    g_saveHubPlacement = showCommand != SW_HIDE;
    DragAcceptFiles(window,TRUE);
    const bool restoreMaximized = saved.maximized &&
        (showCommand == SW_SHOWNORMAL || showCommand == SW_SHOWDEFAULT || showCommand == SW_SHOW);
    ShowWindow(window, restoreMaximized ? SW_SHOWMAXIMIZED : showCommand);
    // The first ShowWindow may use the parent's STARTUPINFO instead.
    if (restoreMaximized && !IsZoomed(window)) ShowWindow(window, SW_SHOWMAXIMIZED);
    UpdateWindow(window);
    if (showCommand != SW_HIDE && SetTimer(window, kProjectPreviewTimer, 250, nullptr))
        InitializeHubUpdate(window);

    int argumentCount=0;
    auto arguments=CommandLineToArgvW(GetCommandLineW(),&argumentCount);
    if(arguments) {
        if(argumentCount==3 && std::wstring(arguments[1])==L"--project") LaunchProject(arguments[2]);
        if(argumentCount==4 && std::wstring(arguments[1])==L"--inspect-package" && std::wstring(arguments[3])==L"--no-launch") {SwitchHubPage(window,HubPage::Engines);BeginPackageVerification(window,arguments[2]);}
        LocalFree(arguments);
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (message.message==WM_MOUSEWHEEL && HandleHubEngineWheel(window,message.wParam,message.lParam)) continue;
        if(HandleHubKeyboard(window,message))continue;
        const bool tab = message.message == WM_KEYDOWN && message.wParam == VK_TAB;
        if(IsDialogMessageW(window,&message)) { if (tab) RevealHubFocus(window); continue; }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
