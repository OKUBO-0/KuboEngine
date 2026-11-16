#define UNICODE
#define _UNICODE
#include "../launcher/main.cpp"
#include <iostream>
#include <thread>
namespace {
void Need(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void Write(const std::filesystem::path& path,const std::string& bytes){std::ofstream file(path,std::ios::binary);file<<bytes;Need(bool(file),"Hub fixture write failed");}
std::string Read(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};}
std::wstring Text(HWND control){const auto size=GetWindowTextLengthW(control);std::wstring text(static_cast<size_t>(size)+1,L'\0');GetWindowTextW(control,text.data(),size+1);text.resize(size);return text;}
void Complete(HWND window){
    const auto deadline=GetTickCount64()+10000;
    while(g_verificationJob.Active() && GetTickCount64()<deadline){
        MSG message{};while(PeekMessageW(&message,window,0,0,PM_REMOVE))DispatchMessageW(&message);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Need(!g_verificationJob.Active(),"Hub did not retire verification worker");
}
}
int main(){HWND window=nullptr;try{
    namespace fs=std::filesystem;using V=crude_json::value;
    const auto root=fs::temp_directory_path()/(L"kubo-hub-package-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    Need(fs::create_directories(root/"Package"),"Fresh Hub fixture required");
    auto declaration=V::parse(R"({"format":"kubo-portable-engine-2","engineVersion":"1.2.3","platform":"Windows-x64","launcher":"KuboLauncher.exe","editor":"KuboEditor.exe","player":"KuboPlayer.exe","configuration":"Release","files":[]})");
    for(const auto* role:{"KuboLauncher.exe","KuboEditor.exe","KuboPlayer.exe"}){
        Write(root/"Package"/role,"abc");declaration["files"].push_back(V(crude_json::object{{"path",role},{"sha256","ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"}}));
    }
    const auto manifest=root/"Package/engine-package.json";Write(manifest,declaration.dump());Write(root/"project.kubo","project sentinel");
    g_recentFile=root/"recent.json";g_engineFile=root/"catalog.json";g_engineError.clear();std::string error;
    Need(KuboHub::EngineCatalogFile::Load(g_engineFile,g_engineCatalog,g_engineSnapshot,error),"Cannot load private catalog");
    WNDCLASSW wc{};wc.lpfnWndProc=WindowProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"KuboHubPackageChecks";
    Need(RegisterClassW(&wc)!=0,"Cannot register Hub fixture");
    window=CreateWindowW(wc.lpszClassName,L"Owned Hub verification checks",WS_OVERLAPPED,0,0,580,840,nullptr,nullptr,wc.hInstance,nullptr);
    Need(window && g_verificationDetails && GetHubControl(window,kVerifyPackage),"Production verification controls missing");
    Need((GetWindowLongPtrW(g_verificationDetails,GWL_STYLE)&ES_READONLY)!=0,"Verification result is editable");
    Need(BeginPackageVerification(window,manifest),"Hub could not start package verification");
    Need(!BeginPackageVerification(window,manifest) && !IsWindowEnabled(GetHubControl(window,kVerifyPackage)) && IsWindowEnabled(GetHubControl(window,kCancelVerification)),"Hub allowed overlapping jobs");
    Complete(window);const auto verified=Text(g_verificationDetails);
    Need(verified.find(L"Manifest checksums match: 3 files")!=verified.npos && verified.find(L"Declared version: 1.2.3")!=verified.npos && verified.find(L"NOT publisher/signature/ABI")!=verified.npos && verified.find(L"Not installed or selected")!=verified.npos,"Hub overclaimed verification or lost package identity");
    Need(IsWindowEnabled(GetHubControl(window,kVerifyPackage)) && !IsWindowEnabled(GetHubControl(window,kCancelVerification)),"Hub did not restore controls");
    Write(root/"Package/KuboEditor.exe","tampered");Need(BeginPackageVerification(window,manifest),"Hub could not restart job");Complete(window);
    Need(Text(g_verificationDetails).find(L"Package not verified:")!=std::wstring::npos && Text(g_verificationDetails).find(L"checksums match")==std::wstring::npos,"Hub retained stale successful verification");
    Write(root/"Package/KuboEditor.exe","abc");Need(BeginPackageVerification(window,manifest),"Hub cancellation job failed to start");
    SendMessageW(window,WM_COMMAND,kCancelVerification,0);Complete(window);
    // The worker may complete before cancellation: either truthful terminal result
    // is permitted, but cancellation must never install/select or abandon work.
    Need(!g_verificationJob.Active(),"Cancel abandoned owned worker");
    Need(BeginPackageVerification(window,manifest),"Hub close job failed to start");
    SendMessageW(window,WM_CLOSE,0,0);Need(g_closeAfterVerification && IsWindow(window),"Close did not defer until owned worker retirement");
    Complete(window);Need(!IsWindow(window) && !g_verificationDetails,"Deferred close retained window or HWND");window=nullptr;
    Need(!fs::exists(g_engineFile) && !fs::exists(g_recentFile) && g_engineCatalog.active.empty() && g_engineCatalog.editors.empty(),"Verification wrote engine selection or history");
    Need(Read(manifest)==declaration.dump() && Read(root/"project.kubo")=="project sentinel" && Read(root/"Package/KuboEditor.exe")=="abc","Verification changed package/project bytes");
    UnregisterClassW(wc.lpszClassName,wc.hInstance);
    std::cout<<"PASS: production owned Hub async verification/result trust labels/failure stale-clear/cancel/close retirement/no catalog, recent, project or package writes; inert EXEs never executed\n";return 0;
}catch(const std::exception& e){if(window)DestroyWindow(window);std::cerr<<e.what()<<'\n';return 1;}}
