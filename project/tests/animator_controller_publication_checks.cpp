#include "engine/editor/AnimatorControllerPublication.h"
#include "engine/editor/AnimatorControllerRecovery.h"
#include "engine/editor/AnimatorControllerAcknowledgement.h"
#include "engine/editor/AnimatorControllerEdits.h"
#include <fstream>
#include <iostream>
static void Need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
static void Write(const std::filesystem::path& file,const std::string& bytes){std::ofstream stream(file,std::ios::binary);stream<<bytes;stream.close();Need(!stream.fail(),"Publication fixture write failed");}
static std::string Read(const std::filesystem::path& file){std::ifstream stream(file,std::ios::binary);return {std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};}
static int InterruptedChild(const std::filesystem::path& root,const std::wstring& phase){
    using namespace Engine::Editor;using namespace Engine::Assets;
    Engine::Base::ProjectContext project;std::string error;Need(project.Open(root/"entry.kubo",error),error.c_str());
    AssetRegistry registry;Need(registry.Scan(root/"Assets",error,AssetRegistry::MetadataPolicy::ReadOnly),error.c_str());
    const auto guid=registry.FindByPath(root/L"Assets/移動.animfsm")->guid;AnimatorControllerRevision expected;
    {HeldAnimatorControllerAsset read;Need(read.Open(project,registry,guid,error),error.c_str());expected=*read.Snapshot();}
    AnimatorControllerDocument document;Need(document.Load(expected.bytes,error),error.c_str());
    auto layer=document.Definition().layers[0];layer.weight=0.5f;Need(AnimatorControllerEdits(document).ReplaceLayer(0,layer,error),error.c_str());
    AnimatorControllerPublication publication;Need(publication.Prepare(project,registry,expected,document.Text(),error),error.c_str());
    if(phase==L"prepared")ExitProcess(91);
    Need(publication.Publish(error,phase==L"parked"?std::function<void()>([]{ExitProcess(91);}):std::function<void()>{}),error.c_str());
    ExitProcess(91);return 2;
}
struct OwnedChild {
    PROCESS_INFORMATION info{};
    OwnedChild(const std::filesystem::path& root,const wchar_t* phase){
        wchar_t executable[32768]{};Need(GetModuleFileNameW(nullptr,executable,32768)!=0,"Cannot locate interruption child");
        auto command=L"\""+std::wstring(executable)+L"\" --interrupt \""+root.wstring()+L"\" "+phase;
        STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
        Need(CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&info)!=0,"Cannot start owned interruption child");
    }
    void Wait(){Need(WaitForSingleObject(info.hProcess,5000)==WAIT_OBJECT_0,"Interruption child did not finish");DWORD code=0;Need(GetExitCodeProcess(info.hProcess,&code) && code==91,"Child failed before intended interruption");}
    ~OwnedChild(){if(info.hProcess){if(WaitForSingleObject(info.hProcess,0)==WAIT_TIMEOUT){TerminateProcess(info.hProcess,92);WaitForSingleObject(info.hProcess,5000);}CloseHandle(info.hThread);CloseHandle(info.hProcess);}}
};
int wmain(int argc,wchar_t** argv){namespace fs=std::filesystem;using namespace Engine::Editor;using namespace Engine::Assets;
    const auto fixture=fs::temp_directory_path()/(L"kubo-controller-publication-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try {
        if(argc==4 && std::wstring(argv[1])==L"--interrupt")return InterruptedChild(argv[2],argv[3]);
        for(const char* scenario:{"success","rollback","foreign","backup","invalid","move","reentrant","changed-source","changed-meta","bad-intent","duplicate-intent","deep-intent","replaced-stage","reader"}) {
            const auto root=fixture/scenario;fs::create_directories(root/"Assets");Write(root/"entry.kubo",R"({"name":"Controller publication","assetDirectory":"Assets","startupScene":"entry.scene"})");
            Engine::Base::ProjectContext project;std::string error;Need(project.Open(root/"entry.kubo",error),error.c_str());
            AnimatorControllerDocument document;Need(AnimatorControllerEdits(document).New(error),"Fixture document failed");const auto file=root/L"Assets/移動.animfsm";Write(file,document.Text());
            AssetRegistry registry;Need(registry.Scan(root/"Assets",error),error.c_str());const auto guid=registry.FindByPath(file)->guid;AnimatorControllerRevision expected;
            {HeldAnimatorControllerAsset read;Need(read.Open(project,registry,guid,error),error.c_str());expected=*read.Snapshot();}
            auto layer=document.Definition().layers[0];layer.weight=0.5f;Need(AnimatorControllerEdits(document).ReplaceLayer(0,layer,error),"Fixture edit failed");const auto nextBytes=document.Text();fs::path directory;
            const std::string mode=scenario;
            {
                AnimatorControllerPublication publication;
                if(mode=="invalid") {Need(!publication.Prepare(project,registry,expected,"{}",error) && publication.Directory().empty() && Read(file)==expected.bytes,"Invalid graph created transaction");continue;}
                Need(publication.Prepare(project,registry,expected,nextBytes,error) && !publication.IsPublished() && !publication.PublishedRevision(),error.c_str());directory=publication.Directory();
                ControllerRecoveryReport busy;Need(!AnimatorControllerRecovery::Inspect(project,guid,busy,error),"Recovery bypassed active writer");
                Need(Read(directory/"original.source").empty(),"Prepare moved source before Publish");
                const auto stageWriter=CreateFileW((directory/"next.source").c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
                if(stageWriter!=INVALID_HANDLE_VALUE){CloseHandle(stageWriter);throw std::runtime_error("Held stage permitted external mutation");}
                if(mode=="success" || mode=="reentrant") {
                    const bool success=publication.Publish(error,mode=="reentrant"?std::function<void()>([&]{std::string nested;Need(!publication.Publish(nested) && publication.IsParked(),"Reentry rolled back active publisher");}):std::function<void()>{});
                    Need(success && publication.IsPublished() && publication.PublishedRevision()->bytes==nextBytes && publication.PublishedRevision()->guid==expected.guid && publication.PublishedRevision()->contentHash.size()==64,"Published revision missing");
                }else if(mode=="rollback")Need(!publication.Publish(error,[]{throw std::runtime_error("after park failure");}) && !publication.IsParked(),"Failed park did not roll back");
                else if(mode=="foreign")Need(!publication.Publish(error,[&]{Write(file,"foreign owner data");}) && publication.IsParked(),"Foreign destination overwritten/backup lost");
                else if(mode=="backup"){Write(directory/"original.source","foreign backup");Need(!publication.Publish(error) && !publication.IsParked(),"Occupied backup overwritten");}
                else if(mode=="move"){Write(root/"Assets/.kubo/asset-move.pending.json","{}");Need(!publication.Publish(error) && !publication.IsParked(),"Asset move pending ignored");}
                else Need(!publication.Publish(error,[]{throw std::runtime_error("prepared recovery fixture");}),"Recovery fixture unexpectedly published");
                if(mode=="foreign") {
                    fs::rename(file,root/"foreign-retained.txt");
                    Need(!publication.Publish(error) && publication.IsParked() && !fs::exists(file),"Rejected repeat attempted filesystem rollback");
                    fs::rename(root/"foreign-retained.txt",file);
                }else Need(!publication.Publish(error),"Publication repeated without recovery");
            }
            Need(fs::exists(directory/"pending.json") && Read(expected.metadata)==expected.metadataBytes,"Intent/meta lost");
            const auto intent=crude_json::value::parse(Read(directory/"pending.json"));
            Need(intent["format"].get<crude_json::string>()=="kubo-controller-source-publication-1" && intent["originalBytes"].get<crude_json::string>()==expected.bytes && intent["nextBytes"].get<crude_json::string>()==nextBytes && intent["metadataBytes"].get<crude_json::string>()==expected.metadataBytes,"Intent did not preserve exact recovery bytes");
            HeldAnimatorControllerWriter nextWriter;Need(!nextWriter.Open(project,registry,expected,error),"Retained transaction bypassed");
            if(mode=="success" || mode=="reentrant") {
                Need(Read(file)==nextBytes && Read(directory/"original.source")==expected.bytes && !fs::exists(directory/"next.source"),"Success source/backup incorrect");
                HeldAnimatorControllerAsset read;Need(!read.Open(project,registry,guid,error),"Pending published graph admitted normally");
            }else if(mode=="foreign")Need(Read(file)=="foreign owner data" && Read(directory/"original.source")==expected.bytes,"Foreign data/retained backup lost");
            else Need(Read(file)==expected.bytes && Read(directory/"next.source")==nextBytes,"Failure changed original or stage");
            ControllerRecoveryReport report;
            if(mode=="move") {Need(!AnimatorControllerRecovery::Inspect(project,guid,report,error),"Recovery ignored asset move");continue;}
            Need(AnimatorControllerRecovery::Inspect(project,guid,report,error),error.c_str());
            if(mode=="changed-source") {Write(file,"foreign owner data");Need(!AnimatorControllerRecovery::RestoreOriginal(project,guid,report,error) && Read(file)=="foreign owner data","Changed source overwritten");continue;}
            if(mode=="changed-meta") {Write(expected.metadata,"{}");Need(!AnimatorControllerRecovery::RestoreOriginal(project,guid,report,error) && Read(expected.metadata)=="{}","Changed metadata overwritten");continue;}
            if(mode=="bad-intent" || mode=="duplicate-intent" || mode=="deep-intent") {
                const auto malformed=mode=="bad-intent"?std::string("{}"):mode=="duplicate-intent"?std::string("{\"guid\":\"a\",\"guid\":\"b\"}"):std::string(1000,'[')+"0"+std::string(1000,']');
                Write(directory/"pending.json",malformed);
                Need(!AnimatorControllerRecovery::Inspect(project,guid,report,error) && Read(file)==expected.bytes && Read(directory/"pending.json")==malformed,"Malformed intent admitted/modified");continue;
            }
            if(mode=="replaced-stage") {fs::rename(directory/"next.source",root/"retained-stage");Write(directory/"next.source",nextBytes);Need(!AnimatorControllerRecovery::RestoreOriginal(project,guid,report,error) && Read(file)==expected.bytes,"Same-byte foreign stage admitted");continue;}
            if(mode=="reader") {
                const auto reader=CreateFileW(file.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);Need(reader!=INVALID_HANDLE_VALUE,"Native foreign reader failed");
                const bool restored=AnimatorControllerRecovery::RestoreOriginal(project,guid,report,error);CloseHandle(reader);Need(!restored,"Recovery bypassed reader DELETE exclusion");
            }
            if(mode=="foreign" || mode=="backup") {
                Need(report.state==ControllerRecoveryState::Conflict && !AnimatorControllerRecovery::RestoreOriginal(project,guid,report,error),"Recovery overwrote conflicting file");
                if(mode=="backup")continue;
                fs::rename(file,root/"foreign-retained.txt");
                Need(!AnimatorControllerRecovery::RestoreOriginal(project,guid,report,error),"Stale conflict report accepted");
                Need(AnimatorControllerRecovery::Inspect(project,guid,report,error) && report.state==ControllerRecoveryState::Parked,"Parked recovery not recognized");
            } else Need(report.state==((mode=="success" || mode=="reentrant")?ControllerRecoveryState::Published:ControllerRecoveryState::Prepared),"Wrong recovery state");
            auto altered=report;altered.source=root/"other.animfsm";
            Need(!AnimatorControllerRecovery::RestoreOriginal(project,guid,altered,error),"Altered recovery report accepted");
            Need(AnimatorControllerRecovery::RestoreOriginal(project,guid,report,error),error.c_str());
            Need(Read(file)==expected.bytes && Read(directory/"next.source")==nextBytes && !fs::exists(directory/"original.source") && Read(expected.metadata)==expected.metadataBytes && fs::exists(directory/"pending.json"),"Restore lost original/stage/meta/intent");
            if(report.state!=ControllerRecoveryState::Prepared)Need(!AnimatorControllerRecovery::RestoreOriginal(project,guid,report,error),"Stale pre-restore state accepted");
            Need(AnimatorControllerRecovery::Inspect(project,guid,report,error) && report.state==ControllerRecoveryState::Prepared && AnimatorControllerRecovery::RestoreOriginal(project,guid,report,error),"Prepared recovery not idempotent");
            Need(!nextWriter.Open(project,registry,expected,error),"Restore silently acknowledged transaction");
            {HeldAnimatorControllerAsset read;Need(!read.Open(project,registry,guid,error,&expected),"Restored but pending source admitted normally");}
            fs::path archive;Need(AnimatorControllerAcknowledgement::ArchiveResolved(project,guid,report,archive,error),error.c_str());
            {HeldAnimatorControllerAsset read;Need(read.Open(project,registry,guid,error,&expected),"Restore did not retain original native identity");}
        }
        for(const auto* phase:{L"prepared",L"parked",L"published"}) {
            const auto root=fixture/(L"interrupted-"+std::wstring(phase));fs::create_directories(root/"Assets");Write(root/"entry.kubo",R"({"name":"Interrupted Controller","assetDirectory":"Assets","startupScene":"entry.scene"})");
            AnimatorControllerDocument document;std::string error;Need(AnimatorControllerEdits(document).New(error),error.c_str());const auto file=root/L"Assets/移動.animfsm";Write(file,document.Text());
            Engine::Base::ProjectContext project;Need(project.Open(root/"entry.kubo",error),error.c_str());AssetRegistry registry;Need(registry.Scan(root/"Assets",error),error.c_str());const auto guid=registry.FindByPath(file)->guid;
            AnimatorControllerRevision expected;{HeldAnimatorControllerAsset read;Need(read.Open(project,registry,guid,error),error.c_str());expected=*read.Snapshot();}
            {OwnedChild child(root,phase);child.Wait();}
            ControllerRecoveryReport report;Need(AnimatorControllerRecovery::Inspect(project,guid,report,error),error.c_str());
            const auto state=std::wstring(phase)==L"prepared"?ControllerRecoveryState::Prepared:std::wstring(phase)==L"parked"?ControllerRecoveryState::Parked:ControllerRecoveryState::Published;
            Need(report.state==state && AnimatorControllerRecovery::RestoreOriginal(project,guid,report,error),"Abrupt child interruption not recoverable");
            Need(fs::exists(report.directory/"pending.json") && fs::exists(report.directory/"next.source"),"Abrupt recovery lost retained records");
            Need(AnimatorControllerRecovery::Inspect(project,guid,report,error),error.c_str());fs::path archive;Need(AnimatorControllerAcknowledgement::ArchiveResolved(project,guid,report,archive,error),error.c_str());
            {HeldAnimatorControllerAsset read;Need(read.Open(project,registry,guid,error,&expected),"Abrupt recovery changed original identity");}
        }
        std::cout<<"PUBLICATION_FIXTURE="<<Engine::Base::PathToUtf8(fixture)<<'\n';
        std::cout<<"PASS: Controller publication and source recovery Prepared/Parked/Published/conflict/stale+altered report/no-replace/native identity/preserved stage+intent/actual loader admission (ack+GUI Save pending)\n";return 0;
    }catch(const std::exception& exception){std::cerr<<exception.what()<<"\nRetained publication fixture: "<<fixture<<'\n';return 1;}
}
