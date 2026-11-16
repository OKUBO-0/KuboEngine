#define UNICODE
#define _UNICODE
#include "../launcher/main.cpp"
#include <iostream>
#include <thread>
namespace {
void Need(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void Write(const std::filesystem::path& path,const std::string& bytes){std::ofstream file(path,std::ios::binary);file<<bytes;Need(bool(file),"Hub install fixture write failed");}
std::string Read(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};}
std::wstring Text(HWND control){const auto size=GetWindowTextLengthW(control);std::wstring text(static_cast<size_t>(size)+1,L'\0');GetWindowTextW(control,text.data(),size+1);text.resize(size);return text;}
void Complete(HWND window){const auto deadline=GetTickCount64()+15000;while((g_installationJob.Active() || g_verificationJob.Active()) && GetTickCount64()<deadline){MSG m{};while(PeekMessageW(&m,window,0,0,PM_REMOVE))DispatchMessageW(&m);std::this_thread::sleep_for(std::chrono::milliseconds(1));}Need(!g_installationJob.Active() && !g_verificationJob.Active(),"Hub package worker did not retire");}

std::filesystem::path updateFixture;
bool FetchFixture(const KuboHub::UpdateUrl& url, uint64_t limit, const std::atomic_bool& cancelled,
    KuboHub::UpdateByteSink sink, std::wstring&, ULONGLONG)
{
    const auto name = url.target.substr(std::wstring_view(L"/release/").size());
    Need(url.host == L"updates.example.invalid" && url.target.starts_with(L"/release/") &&
        name.find(L'/') == std::wstring::npos, "Unexpected update request");
    const auto bytes = Read(updateFixture / name);
    return !cancelled && !bytes.empty() && bytes.size() <= limit &&
        sink(bytes.data(), static_cast<DWORD>(bytes.size()));
}
}
int main(){HWND window=nullptr;try{
    namespace fs=std::filesystem;using V=crude_json::value;
    const auto root=fs::temp_directory_path()/(L"kubo-hub-install-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    Need(fs::create_directories(root/"Source"),"Fresh Hub installation fixture required");fs::create_directory(root/"Old");Write(root/"Old/KuboEditor.exe","inert old Editor");
    auto declaration=V::parse(R"({"format":"kubo-portable-engine-2","engineVersion":"1.2.3","platform":"Windows-x64","launcher":"KuboLauncher.exe","editor":"KuboEditor.exe","player":"KuboPlayer.exe","configuration":"Release","files":[]})");
    for(const auto* role:{"KuboLauncher.exe","KuboEditor.exe","KuboPlayer.exe"}){Write(root/"Source"/role,"abc");declaration["files"].push_back(V(crude_json::object{{"path",role},{"sha256","ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"}}));}
    const auto manifest=root/"Source/engine-package.json",store=root/"Store",oldEditor=root/"Old/KuboEditor.exe";Write(manifest,declaration.dump());
    g_recentFile=root/"recent.json";g_engineFile=root/"catalog.json";g_engineError.clear();std::string error;
    Need(KuboHub::EngineCatalogFile::Load(g_engineFile,g_engineCatalog,g_engineSnapshot,error),"Private catalog load failed");
    WNDCLASSW wc{};wc.lpfnWndProc=WindowProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"KuboHubInstallationChecks";Need(RegisterClassW(&wc)!=0,"Hub class failed");
    window=CreateWindowW(wc.lpszClassName,L"Owned Hub install",WS_OVERLAPPED,0,0,580,920,nullptr,nullptr,wc.hInstance,nullptr);Need(window && GetHubControl(window,kInstallPackage) && GetHubControl(window,kRecoverInstallation),"Production installation controls missing");
    KuboHub::EngineCatalog old{{oldEditor},oldEditor};Need(CommitEngineSelection(old,error),"Initial private selection failed");const auto oldBytes=Read(g_engineFile);
    Need(BeginPackageInstallation(window,manifest,store),"Hub install failed to start");Need(!BeginPackageVerification(window,manifest) && !BeginPackageInstallation(window,manifest,store),"Hub admitted overlapping package jobs");
    Complete(window);Need(Text(g_verificationDetails).find(L"Package available at:")!=std::wstring::npos && Text(g_verificationDetails).find(L"Selection unchanged")!=std::wstring::npos && !g_lastInstalledEditor.empty(),"Hub install outcome missing or falsely selected");
    const auto installed=g_lastInstalledEditor;Need(fs::is_regular_file(installed) && Read(g_engineFile)==oldBytes && g_engineCatalog.active==oldEditor,"Install changed active engine or failed to publish");
    Need(IsWindowEnabled(GetHubControl(window,kSelectInstalled)) && SelectLastInstalled(error),"Explicit installed selection failed");Need(g_engineCatalog.active==installed && g_engineCatalog.editors.size()==2,"Installed selection lost old registration");
    // Production combo event is the explicit rollback path; no executable runs.
    SendMessageW(g_engineList,CB_SETCURSEL,1,0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(kEngineList,CBN_SELCHANGE),0);
    Need(g_engineCatalog.active==oldEditor && fs::is_regular_file(installed),"Rollback removed version or failed to restore old selection");
    KuboHub::EngineCatalog reloaded;KuboHub::CatalogSnapshot snapshot;Need(KuboHub::EngineCatalogFile::Load(g_engineFile,reloaded,snapshot,error) && reloaded.active==oldEditor && reloaded.editors.size()==2,"Selection/rollback did not persist");
    const auto selectedBytes=Read(g_engineFile);Write(root/"Source/KuboEditor.exe","tampered");Need(BeginPackageInstallation(window,manifest,store),"Hub failure job could not start");Complete(window);
    Need(Text(g_verificationDetails).find(L"Installation not published:")!=std::wstring::npos && g_lastInstalledEditor.empty() && !IsWindowEnabled(GetHubControl(window,kSelectInstalled)) && Read(g_engineFile)==selectedBytes,"Failed install retained stale select button or changed catalog");Write(root/"Source/KuboEditor.exe","abc");
    const auto package=installed.parent_path();const auto hash=package.filename().string().substr(8);const std::string token(32,'b');
    fs::rename(package,store/(".pending-"+token));V journal(crude_json::object{});journal["format"]="kubo-engine-install-1";journal["token"]=token;journal["manifestSha256"]=hash;Write(store/("pending-"+token+".json"),journal.dump());
    Need(BeginPackageInstallation(window,{},store,token),"Hub recovery job failed to start");Complete(window);Need(g_lastInstalledEditor==installed && fs::is_regular_file(installed) && Read(g_engineFile)==selectedBytes,"Hub recovery selected engine or failed publication");
    Need(BeginPackageInstallation(window,manifest,store),"Hub close job failed to start");SendMessageW(window,WM_CLOSE,0,0);Need(g_closeAfterVerification && IsWindow(window),"Close did not defer for owned install worker");Complete(window);Need(!IsWindow(window),"Deferred close did not destroy window");window=nullptr;
    Need(Read(manifest)==declaration.dump() && Read(root/"Source/KuboEditor.exe")=="abc" && Read(oldEditor)=="inert old Editor" && Read(g_engineFile)==selectedBytes && !fs::exists(g_recentFile),"Hub installation/selection/rollback changed source, old engine, catalog unexpectedly, or history");
    updateFixture = manifest.parent_path();
    KuboHub::UpdateFeedResult release;
    release.state = KuboHub::UpdateFeedResult::State::Available;
    release.version = "1.2.3";
    release.manifestUrl = "https://updates.example.invalid/release/engine-package.json";
    Need(Engine::Services::UpdateService::ComputeSha256(manifest, release.manifestHash, error),
        "Update fixture manifest hashing failed");
    const auto updateStore = root / "DownloadedStore";
    std::atomic_bool cancelled{false};
    std::atomic<uint64_t> progress{0};
    const auto downloaded = KuboHub::DownloadEngineUpdate(release, updateStore, cancelled, progress, FetchFixture);
    Need(downloaded.result == KuboHub::EnginePackageInstallation::Result::Installed && progress == 9,
        "Verified remote payloads were not published");
    Write(updateFixture / "KuboEditor.exe", "tampered");
    const auto rejectedStore = root / "RejectedStore";
    const auto rejected = KuboHub::DownloadEngineUpdate(release, rejectedStore, cancelled, progress, FetchFixture);
    Need(rejected.result == KuboHub::EnginePackageInstallation::Result::Invalid &&
        !rejected.error.empty() && !fs::exists(rejectedStore) && fs::exists(installed),
        "Tampered payload was not isolated from installed engines");
    cancelled = true;
    const auto stopped = KuboHub::DownloadEngineUpdate(release, rejectedStore, cancelled, progress, FetchFixture);
    Need(stopped.staged.empty() && !fs::exists(rejectedStore) && Read(g_engineFile) == selectedBytes,
        "Cancelled download changed the engine selection or published data");
    std::cout << "PASS: fixture transport, download, payload digest, install publication, tamper and cancellation isolation\n";
    UnregisterClassW(wc.lpszClassName,wc.hInstance);std::cout<<"PASS: production owned Hub async install/recover, explicit selection and persisted combo rollback, mutual exclusion/stale-clear/close retirement, old version/source/catalog isolation; inert EXEs never executed\n";return 0;
}catch(const std::exception& e){if(window)DestroyWindow(window);std::cerr<<e.what()<<'\n';return 1;}}
