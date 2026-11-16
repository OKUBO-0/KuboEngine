#define UNICODE
#define _UNICODE
#include "../launcher/main.cpp"
#include <iostream>
namespace {
void Need(bool v,const char* why){if(!v)throw std::runtime_error(why);}
void Write(const std::filesystem::path& p,const std::string& text){std::ofstream out(p,std::ios::binary);out<<text;out.close();Need(!out.fail(),"Hub identity fixture write failed");}
std::wstring Text(HWND window){const auto count=GetWindowTextLengthW(window);std::wstring value(static_cast<size_t>(count)+1,L'\0');GetWindowTextW(window,value.data(),count+1);value.resize(static_cast<size_t>(count));return value;}
void Pump(HWND window){MSG m{};while(PeekMessageW(&m,window,kRefreshEngineDetails,kRefreshEngineDetails,PM_REMOVE))DispatchMessageW(&m);}
}
int main(){HWND window=nullptr;try{
 namespace fs=std::filesystem;using M=Engine::Services::EnginePackageManifest;using V=crude_json::value;
 const auto root=fs::temp_directory_path()/(L"kubo-hub-identity-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));Need(!fs::exists(root),"Fresh Hub identity fixture required");fs::create_directories(root/L"Versioned");fs::create_directories(root/L"Manual");
 const auto editor=root/L"Versioned/KuboEditor.exe",manual=root/L"Manual/KuboEditor.exe",manifest=root/L"Versioned/engine-package.json";
 // Inert bytes: these paths are never executed. Deliberately not hash-verified.
 Write(editor,"inert selected file");Write(manual,"inert manual file");
 auto data=V::parse(R"({"format":"kubo-portable-engine-2","engineVersion":"1.2.3","platform":"Windows-x64","launcher":"KuboLauncher.exe","editor":"KuboEditor.exe","player":"KuboPlayer.exe","configuration":"Release","files":[]})");for(const auto* role:{"KuboLauncher.exe","KuboEditor.exe","KuboPlayer.exe"})data["files"].push_back(V(crude_json::object{{"path",role},{"sha256",std::string(64,'a')}}));Write(manifest,data.dump());
 g_recentFile=root/L"recent.json";g_engineFile=root/L"catalog.json";g_engineError.clear();std::string error;if(!KuboHub::EngineCatalogFile::Load(g_engineFile,g_engineCatalog,g_engineSnapshot,error))throw std::runtime_error(error);
 WNDCLASSW wc{};wc.lpfnWndProc=WindowProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"KuboHubIdentityChecks";Need(RegisterClassW(&wc)!=0,"Hub identity class failed");window=CreateWindowW(wc.lpszClassName,L"Hub identity checks",WS_OVERLAPPED,0,0,580,720,nullptr,nullptr,wc.hInstance,nullptr);Need(window && g_engineDetails,"Production Hub identity control missing");
 KuboHub::EngineCatalog candidate{{editor,manual},editor};if(!CommitEngineSelection(candidate,error))throw std::runtime_error(error);
 Need(Text(g_engineDetails).find(L"Reading package declaration")!=std::wstring::npos,"New selection retained stale version before deferred read");Pump(window);const auto declared=Text(g_engineDetails);
 Need(declared.find(L"Package-declared version: 1.2.3")!=std::wstring::npos && declared.find(L"Windows-x64")!=std::wstring::npos && declared.find(L"not verified")!=std::wstring::npos && (GetWindowLongPtrW(g_engineDetails,GWL_STYLE)&ES_READONLY),"Declared package version or explicit trust boundary missing");
 Engine::Base::ProjectContext project;if(!project.Create(root/L"Project","Independent project",error))throw std::runtime_error(error);
 {std::ifstream in(project.GetProjectFile(),std::ios::binary);const std::string bytes(std::istreambuf_iterator<char>(in),{});auto authored=V::parse(bytes);authored["engineVersion"]="9.8.7";Write(project.GetProjectFile(),authored.dump());}
 AddRecent(project.GetProjectFile());PopulateRecent();SendMessageW(g_recentList,LB_SETCURSEL,0,0);RefreshProjectDetails();
 Need(Text(g_projectDetails).find(L"engineVersion: 9.8.7")!=std::wstring::npos && Text(g_projectDetails).find(L"compatibility is not verified")!=std::wstring::npos && Text(g_engineDetails).find(L"Package-declared version: 1.2.3")!=std::wstring::npos,"Authored project metadata confused with selected Editor release");
 const auto catalogBytes=[](const fs::path& p){std::ifstream in(p,std::ios::binary);return std::string(std::istreambuf_iterator<char>(in),{});};const auto before=catalogBytes(g_engineFile);RefreshEngineDetails();Need(catalogBytes(g_engineFile)==before && catalogBytes(manifest)==data.dump(),"Identity read wrote catalog/package");
 const auto alternate=root/L"Versioned/KuboEngine.exe";Write(alternate,"inert, not declared as package entry point");auto nonEntry=candidate;nonEntry.editors.push_back(alternate);nonEntry.active=alternate;if(!CommitEngineSelection(nonEntry,error))throw std::runtime_error(error);Pump(window);Need(Text(g_engineDetails).find(L"not the declared package entry point")!=std::wstring::npos && Text(g_engineDetails).find(L"1.2.3")==std::wstring::npos,"Neighbor manifest version attributed to another executable");if(!CommitEngineSelection(candidate,error))throw std::runtime_error(error);Pump(window);
 auto legacy=data;legacy["format"]="kubo-portable-engine-1";legacy.erase("engineVersion");legacy.erase("platform");Write(manifest,legacy.dump());RefreshEngineDetails();Need(Text(g_engineDetails).find(L"Legacy unversioned")!=std::wstring::npos && Text(g_engineDetails).find(L"1.2.3")==std::wstring::npos,"Legacy package inherited stale release");
 Write(manifest,"broken JSON");RefreshEngineDetails();Need(Text(g_engineDetails).find(L"invalid or unreadable")!=std::wstring::npos && Text(g_engineDetails).find(L"1.2.3")==std::wstring::npos && g_engineCatalog.active==editor,"Bad metadata blocked/replaced selection or retained old version");
 candidate.active=manual;if(!CommitEngineSelection(candidate,error))throw std::runtime_error(error);Pump(window);Need(Text(g_engineDetails).find(L"Unversioned/manual")!=std::wstring::npos && !fs::exists(root/L"Manual/engine-package.json"),"Manual editor failed or invented sidecar");
 fs::remove(manual);RefreshEngineDetails();Need(Text(g_engineDetails).find(L"Selected Editor unavailable")!=std::wstring::npos,"Missing selected Editor retained previous metadata");
 Write(manifest,data.dump());candidate.active=editor;if(!CommitEngineSelection(candidate,error))throw std::runtime_error(error);Pump(window);Need(Text(g_engineDetails).find(L"1.2.3")!=std::wstring::npos,"Restored package metadata did not recover");
 DestroyWindow(window);window=nullptr;Need(!g_engineDetails && !g_engineList,"Destroyed Hub retained identity HWND");UnregisterClassW(wc.lpszClassName,wc.hInstance);
 std::cout<<"PASS: production owned Hub selected-editor package declaration/deferred refresh, explicit unverified label, legacy/manual/malformed/missing isolation and no package/catalog writes from read (no fake executable launched, no install/signature/ABI/physical proof) retained="<<root<<'\n';return 0;
}catch(const std::exception& e){if(window)DestroyWindow(window);std::cerr<<e.what()<<'\n';return 1;}}
