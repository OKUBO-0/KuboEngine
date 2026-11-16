// Exercise the production recent-list functions without driving a window.
#define UNICODE
#define _UNICODE
#include "../launcher/main.cpp"
#include <iostream>
#include <stdexcept>
#include <sstream>

int main()
{
    HWND detailsWindow=nullptr;
    const auto directory = std::filesystem::temp_directory_path() /
        ("kubo-launcher-recent-" + std::to_string(GetCurrentProcessId()));
    try {
        std::filesystem::create_directories(directory);
        g_recentFile = directory / "recent_projects.json";
        const auto japanese = directory / L"日本語プロジェクト" / L"テスト.kubo";
        AddRecent(japanese);
        AddRecent(directory / "Second.kubo");
        AddRecent(japanese);
        const auto entries = ReadRecent();
        const auto escaped = crude_json::value::parse(R"("\u65e5\u672c\u8a9e")");
        if (!escaped.is_string() || escaped.get<crude_json::string>() != "日本語") throw std::runtime_error("Unicode JSON escape decoding failed");
        if (entries.size() != 2 || entries.front() != japanese.wstring()) throw std::runtime_error("Unicode recent path or deduplication failed");
        if (!SaveHubPaths(HubPinsFile(),{japanese.wstring()})) throw std::runtime_error("Pin fixture failed");
        for (int index = 0; index < 30; ++index) AddRecent(directory / (std::to_string(index) + ".kubo"));
        const auto retained = ReadRecent();
        if (retained.size() != kRecentUnpinnedLimit + 1 ||
            std::find(retained.begin(),retained.end(),japanese.wstring()) == retained.end())
            throw std::runtime_error("Recent history evicted a pin or exceeded its bound");
        { std::ofstream corrupt(g_recentFile); corrupt << "invalid JSON"; }
        if (!ReadRecent().empty()) throw std::runtime_error("Corrupt history was not isolated");
        {
            std::string bounded;
            std::istringstream exact(std::string(kRecentTextLimit, ' '));
            if (!ReadBoundedRecentText(exact, bounded) || bounded.size() != kRecentTextLimit) throw std::runtime_error("Exact recent read bound rejected");
            std::istringstream oversized(std::string(kRecentTextLimit + 1, ' '));
            if (ReadBoundedRecentText(oversized, bounded) || !bounded.empty()) throw std::runtime_error("Streaming recent read exceeded bound or retained partial data");
            std::istringstream failed("[]"); failed.setstate(std::ios::badbit);
            if (ReadBoundedRecentText(failed, bounded)) throw std::runtime_error("Recent stream I/O failure accepted");
        }
        {std::ofstream out(g_recentFile,std::ios::binary);out<<std::string(kRecentTextLimit + 1,' ');}
        if (!ReadRecent().empty()) throw std::runtime_error("Oversized JSON history accepted");
        crude_json::value boundedPaths(crude_json::array{});
        boundedPaths.push_back("");boundedPaths.push_back(std::string("bad\0path",8));
        boundedPaths.push_back(std::string(kRecentPathLimit+1,'x'));boundedPaths.push_back("Valid.kubo");
        {std::ofstream out(g_recentFile,std::ios::binary);out<<boundedPaths.dump();}
        if(ReadRecent()!=std::vector<std::wstring>{L"Valid.kubo"}) throw std::runtime_error("Invalid JSON history path not skipped");
        std::filesystem::remove(g_recentFile); // Only this test's private fixture.
        auto legacy=g_recentFile;legacy.replace_extension(L".txt");
        {std::ofstream out(legacy,std::ios::binary);out<<"First.kubo\n\nLast.kubo";}
        if(ReadRecent()!=std::vector<std::wstring>{L"First.kubo",L"Last.kubo"}) throw std::runtime_error("Legacy last line/blank line support changed");
        {std::ofstream out(legacy,std::ios::binary);out<<std::string(kRecentPathLimit,'x')<<'\n';}
        if(ReadRecent().size()!=1 || ReadRecent().front().size()!=kRecentPathLimit) throw std::runtime_error("Exact legacy path bound rejected");
        {std::ofstream out(legacy,std::ios::binary);out<<std::string(kRecentPathLimit+1,'x');}
        if(!ReadRecent().empty()) throw std::runtime_error("Oversized legacy line accepted");
        {std::ofstream out(legacy,std::ios::binary);out<<std::string(kRecentTextLimit+1,'\n');}
        if(!ReadRecent().empty()) throw std::runtime_error("Oversized legacy file accepted");
        {std::ofstream out(legacy,std::ios::binary);out.write("bad\0path\n",9);}
        if(!ReadRecent().empty()) throw std::runtime_error("Embedded NUL legacy path accepted");
        {std::ofstream out(legacy,std::ios::binary);for(int n=0;n<20;++n)out<<n<<".kubo\n";}
        if(ReadRecent().size()!=12) throw std::runtime_error("Legacy recent entry count exceeded bound");
        {std::ofstream out(g_recentFile,std::ios::binary);out<<"invalid JSON";}
        if(!ReadRecent().empty()) throw std::runtime_error("Corrupt JSON fell back to stale legacy history");
        const auto selected=directory/L"新規プロジェクト"/L"日本語ゲーム.kubo";
        Engine::Base::ProjectContext created;std::string error;
        if(!Engine::Services::ProjectCreate::Create(selected,created,error)) throw std::runtime_error(error);
        if(created.GetProjectFile()!=selected || created.GetSettings().name!="日本語ゲーム") throw std::runtime_error("Launcher creation corrupted Unicode project name/path");
        Engine::Base::ProjectContext reopened;
        if(!reopened.Open(selected,error) || reopened.GetSettings().name!="日本語ゲーム") throw std::runtime_error("Launcher-created Unicode project did not reopen");
        const auto read=[](const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);return std::string(std::istreambuf_iterator<char>(in),{});};
        g_engineFile=directory/L"engine_catalog.json";
        const auto firstEditor=directory/L"日本語エンジン"/L"KuboEditor.exe";
        const auto secondEditor=directory/L"Second Engine"/L"KuboEngine.exe";
        for(const auto& executable:{firstEditor,secondEditor}) {
            std::filesystem::create_directories(executable.parent_path());std::ofstream out(executable);out<<"not executed";
        }
        if(!KuboHub::EngineCatalogFile::Load(g_engineFile,g_engineCatalog,g_engineSnapshot,error) || !g_engineCatalog.active.empty()) throw std::runtime_error("Absent catalog did not choose bundled Editor");
        KuboHub::EngineCatalog candidate{{firstEditor,secondEditor},firstEditor};
        if(!CommitEngineSelection(candidate,error))throw std::runtime_error(error);
        KuboHub::EngineCatalog reloaded;KuboHub::CatalogSnapshot loaded;
        if(!KuboHub::EngineCatalogFile::Load(g_engineFile,reloaded,loaded,error) || reloaded.editors!=candidate.editors || reloaded.active!=firstEditor)throw std::runtime_error("Unicode engine catalog did not round trip");
        std::filesystem::path resolved;
        if(!ResolveSelectedEditor(resolved,error) || resolved!=firstEditor)throw std::runtime_error("Explicit Editor was not resolved");
        std::filesystem::remove(firstEditor);
        if(ResolveSelectedEditor(resolved,error))throw std::runtime_error("Missing explicit Editor silently fell back");
        {std::ofstream out(firstEditor);out<<"not executed";}
        const auto catalogBytes=read(g_engineFile);
        auto catalogLock=g_engineFile;catalogLock+=L".lock";
        {
            struct HeldCatalogLock {HANDLE value=INVALID_HANDLE_VALUE;~HeldCatalogLock(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}} held;
            held.value=CreateFileW(catalogLock.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(held.value==INVALID_HANDLE_VALUE || CommitEngineSelection(candidate,error) || read(g_engineFile)!=catalogBytes)throw std::runtime_error("Busy catalog save did not preserve settings");
        }
        {std::ofstream out(catalogLock,std::ios::binary);out<<'x';}
        if(CommitEngineSelection(candidate,error) || read(g_engineFile)!=catalogBytes || read(catalogLock)!="x")throw std::runtime_error("Nonempty catalog lock accepted or overwritten");
        {std::ofstream out(catalogLock,std::ios::binary|std::ios::trunc);}
        auto invalid=candidate;invalid.active=directory/L"Unknown"/L"KuboEditor.exe";
        if(CommitEngineSelection(invalid,error) || read(g_engineFile)!=catalogBytes || g_engineCatalog.active!=firstEditor)throw std::runtime_error("Unregistered active Editor changed settings");
        invalid=candidate;invalid.editors.push_back(firstEditor.parent_path()/L"KUBOEDITOR.EXE");
        if(CommitEngineSelection(invalid,error) || read(g_engineFile)!=catalogBytes)throw std::runtime_error("Case-insensitive duplicate Editor accepted");
        invalid=candidate;invalid.editors[0]=L"relative/KuboEditor.exe";
        if(CommitEngineSelection(invalid,error))throw std::runtime_error("Relative Editor accepted");
        invalid=candidate;
        for(size_t i=2;i<=KuboHub::EngineCatalogFile::Entries;++i)invalid.editors.push_back(directory/std::to_string(i)/L"KuboEditor.exe");
        if(CommitEngineSelection(invalid,error) || read(g_engineFile)!=catalogBytes)throw std::runtime_error("Catalog entry limit exceeded");
        KuboHub::CatalogSnapshot wrongSnapshot;
        KuboHub::EngineCatalog wrongCatalog;
        if(!KuboHub::EngineCatalogFile::Load(directory/L"Other.json",wrongCatalog,wrongSnapshot,error) || KuboHub::EngineCatalogFile::SaveChecked(g_engineFile,candidate,wrongSnapshot,error))throw std::runtime_error("Snapshot from another catalog accepted");
        {std::ofstream out(g_engineFile,std::ios::binary);out<<"changed by another writer";}
        if(CommitEngineSelection(candidate,error) || read(g_engineFile)!="changed by another writer")throw std::runtime_error("External catalog change overwritten");
        const std::vector<std::string> malformed={"[]","[\"unknown\",\"\"]","[\"kubo-engine-catalog-1\",{}]","[[[[[[[[[[[[[]]]]]]]]]]]]]",std::string(KuboHub::EngineCatalogFile::Limit+1,' '),"[\"kubo-engine-catalog-1\",\"\\u0000\"]",std::string(1,static_cast<char>(0xff))};
        for(const auto& bytes:malformed) {
            {std::ofstream out(g_engineFile,std::ios::binary);out<<bytes;}
            if(KuboHub::EngineCatalogFile::Load(g_engineFile,reloaded,loaded,error) || reloaded.active!=firstEditor || read(g_engineFile)!=bytes)throw std::runtime_error("Malformed catalog changed loaded choices or original evidence");
        }
        {std::ofstream out(g_engineFile,std::ios::binary);out<<catalogBytes;}
        if(!KuboHub::EngineCatalogFile::Load(g_engineFile,g_engineCatalog,g_engineSnapshot,error))throw std::runtime_error(error);
        const auto originalManifest=read(selected);
        AddRecent(selected);const auto originalRecent=read(g_recentFile);
        const wchar_t* detailsClass=L"KuboLauncherDetailsChecks";
        WNDCLASSW detailsRegistration{};detailsRegistration.hInstance=GetModuleHandleW(nullptr);
        detailsRegistration.lpfnWndProc=WindowProc;detailsRegistration.lpszClassName=detailsClass;
        if(!RegisterClassW(&detailsRegistration)) throw std::runtime_error("Cannot register owned Hub details fixture");
        detailsWindow=CreateWindowW(detailsClass,L"Owned Hub details fixture",WS_OVERLAPPEDWINDOW,0,0,580,460,nullptr,nullptr,detailsRegistration.hInstance,nullptr);
        if(!detailsWindow || !g_projectDetails || GetDlgItem(detailsWindow,kProjectDetails)!=g_projectDetails) throw std::runtime_error("Production Hub details control absent");
        if(!g_engineList || SendMessageW(g_engineList,CB_GETCOUNT,0,0)!=3 || SendMessageW(g_engineList,CB_GETCURSEL,0,0)!=1)throw std::runtime_error("Production engine choices absent");
        SendMessageW(g_engineList,CB_SETCURSEL,2,0);
        SendMessageW(detailsWindow,WM_COMMAND,MAKEWPARAM(kEngineList,CBN_SELCHANGE),reinterpret_cast<LPARAM>(g_engineList));
        if(g_engineCatalog.active!=secondEditor || !ResolveSelectedEditor(resolved,error) || resolved!=secondEditor)throw std::runtime_error("Production engine selection did not commit");
        if(!IsWindowEnabled(GetDlgItem(detailsWindow,kRemoveEngine)) || !RemoveActiveEngineRegistration(error) || !g_engineCatalog.active.empty() || g_engineCatalog.editors.size()!=1 || !std::filesystem::exists(secondEditor))throw std::runtime_error("Registration removal changed executable or retained selection");
        if(IsWindowEnabled(GetDlgItem(detailsWindow,kRemoveEngine)))throw std::runtime_error("Bundled Editor registration removal remains enabled");
        if(!CommitEngineSelection(candidate,error))throw std::runtime_error(error);
        SendMessageW(g_engineList,CB_SETCURSEL,0,0);
        SendMessageW(detailsWindow,WM_COMMAND,MAKEWPARAM(kEngineList,CBN_SELCHANGE),reinterpret_cast<LPARAM>(g_engineList));
        if(!g_engineCatalog.active.empty() || !KuboHub::EngineCatalogFile::Load(g_engineFile,reloaded,loaded,error) || !reloaded.active.empty() || reloaded.editors.size()!=2)throw std::runtime_error("Return to bundled Editor did not preserve registered choices");
        if((GetWindowLongPtrW(g_projectDetails,GWL_STYLE)&ES_READONLY)==0) throw std::runtime_error("Hub details are editable");
        const auto detailsText=[](){const auto length=GetWindowTextLengthW(g_projectDetails);std::vector<wchar_t> text(static_cast<size_t>(length)+1);GetWindowTextW(g_projectDetails,text.data(),length+1);return std::wstring(text.data());};
        if(detailsText().find(L"日本語ゲーム")==std::wstring::npos || detailsText().find(L"engineVersion: 0.1.0")==std::wstring::npos) throw std::runtime_error("Selected project UTF-8 name/version not displayed");
        auto future=crude_json::value::parse(originalManifest);future["engineVersion"]="9.8.7";
        {std::ofstream out(selected,std::ios::binary);out<<future.dump();}
        SendMessageW(detailsWindow,WM_COMMAND,MAKEWPARAM(kRecentList,LBN_SELCHANGE),reinterpret_cast<LPARAM>(g_recentList));
        if(detailsText().find(L"9.8.7")==std::wstring::npos || detailsText().find(L"compatibility is not verified")==std::wstring::npos) throw std::runtime_error("Opaque future version hidden or claimed compatible");
        if(read(selected)!=future.dump() || read(g_recentFile)!=originalRecent) throw std::runtime_error("Selection inspection changed project/history");
        {std::ofstream out(selected,std::ios::binary);out<<"invalid JSON";}
        SendMessageW(detailsWindow,WM_COMMAND,MAKEWPARAM(kRecentList,LBN_SELCHANGE),reinterpret_cast<LPARAM>(g_recentList));
        if(detailsText().find(L"Project cannot be read:")==std::wstring::npos || read(selected)!="invalid JSON" || read(g_recentFile)!=originalRecent) throw std::runtime_error("Invalid project inspection did not preserve input/history");
        SendMessageW(g_recentList,LB_SETCURSEL,static_cast<WPARAM>(-1),0);RefreshProjectDetails();
        if(detailsText().find(L"プロジェクトを選択してください")==std::wstring::npos) throw std::runtime_error("No-selection details remained stale");
        DestroyWindow(detailsWindow);detailsWindow=nullptr;
        if(g_recentList || g_projectDetails || g_engineList) throw std::runtime_error("Retired Hub controls retained stale handles");
        UnregisterClassW(detailsClass,detailsRegistration.hInstance);
        std::filesystem::remove_all(directory);
        std::cout << "PASS: Launcher bounded history, explicit persisted engine catalog/selection/conflict and missing-Editor refusal, production owned read-only project details/input preservation (not physical/DPI/ABI/signature verification)\n";
        return 0;
    } catch (const std::exception& exception) {
        if(detailsWindow)DestroyWindow(detailsWindow);
        std::cerr << exception.what() << '\n';
        std::filesystem::remove_all(directory);
        return 1;
    }
}
