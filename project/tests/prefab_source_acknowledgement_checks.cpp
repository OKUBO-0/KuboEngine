#include "engine/editor/PrefabSourceAcknowledgement.h"
#include <fstream>
#include <iostream>
#include <map>
namespace {
namespace fs=std::filesystem;using namespace Engine::Editor;
void Require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void Write(const fs::path& file,const std::string& bytes){std::ofstream out(file,std::ios::binary);out<<bytes;out.close();Require(!out.fail(),"Fixture write failed");}
std::string Read(const fs::path& file){std::ifstream in(file,std::ios::binary);Require(in.good(),"Fixture read failed");return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};}
std::map<fs::path,std::string> Snapshot(const fs::path& root){std::map<fs::path,std::string> result;for(const auto& entry:fs::recursive_directory_iterator(root))if(entry.is_regular_file())result.emplace(entry.path().lexically_relative(root),Read(entry.path()));return result;}
PrefabFileIdentity Identity(const fs::path& path){const auto handle=CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);Require(handle!=INVALID_HANDLE_VALUE,"Fixture identity open failed");BY_HANDLE_FILE_INFORMATION info{};const bool read=GetFileInformationByHandle(handle,&info)!=0;CloseHandle(handle);Require(read,"Fixture identity read failed");return {info.dwVolumeSerialNumber,info.nFileIndexHigh,info.nFileIndexLow};}
struct Child {
    PROCESS_INFORMATION info{};
    Child(const fs::path& root,const std::wstring& mode){wchar_t executable[32768];Require(GetModuleFileNameW(nullptr,executable,32768)!=0,"Missing child executable");std::wstring command=L"\""+std::wstring(executable)+L"\" --child \""+root.wstring()+L"\" "+mode;STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;Require(CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&info)!=0,"Owned child start failed");}
    void Kill(){Require(TerminateProcess(info.hProcess,91)!=0 && WaitForSingleObject(info.hProcess,5000)==WAIT_OBJECT_0,"Owned child termination failed");DWORD code=0;Require(GetExitCodeProcess(info.hProcess,&code) && code==91,"Owned child exit mismatch");}
    ~Child(){if(info.hProcess){if(WaitForSingleObject(info.hProcess,0)==WAIT_TIMEOUT){TerminateProcess(info.hProcess,92);WaitForSingleObject(info.hProcess,5000);}CloseHandle(info.hThread);CloseHandle(info.hProcess);}}
};
int ChildMain(const fs::path& root,const std::wstring& mode){
    Engine::Base::ProjectContext project;std::string error;Require(project.Open(root/"Game.kubo",error),error.c_str());Engine::Assets::AssetRegistry registry;Require(registry.Scan(root/"Assets",error),error.c_str());
    const auto file=root/L"Assets/ソース.prefab";ProjectPrefabRevision revision;Require(ReadProjectPrefabRevision(project,registry,registry.FindByPath(file)->guid,revision,error),error.c_str());auto next=revision.definition;next["world"]["entities"][size_t(0)]["name"]="After";
    const auto stop=[&](const std::string& marker){Write(root/"ready.pending",marker);fs::rename(root/"ready.pending",root/"ready");Sleep(INFINITE);};
    {PrefabSourcePublication publication;Require(publication.Prepare(project,registry,revision,next,error),error.c_str());Require(publication.Publish(error,mode==L"child-parked"?std::function<void()>([&]{stop("parked");}):std::function<void()>()),error.c_str());}
    PrefabSourceRecoveryReport report;Require(PrefabSourceRecovery::Inspect(project,revision.guid,report,error),error.c_str());fs::path archive;
    Require(PrefabSourceAcknowledgement::ArchiveResolved(project,revision.guid,report,archive,error,mode==L"child-before"?std::function<void(const fs::path&)>([&](const fs::path&){stop("before");}):std::function<void(const fs::path&)>()),error.c_str());
    stop(Engine::Base::PathToUtf8(archive));return 2;
}
}
int wmain(int argc,wchar_t** argv){
    const auto fixture=fs::temp_directory_path()/(L"kubo-prefab-ack-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try {
        if(argc==4 && std::wstring(argv[1])==L"--child")return ChildMain(argv[2],argv[3]);
        for(const auto& scenario:{"prepared","published","restored","conflict","incomplete","destination-conflict","callback-failure","stale-source","stale-report","foreign-retained","stale-metadata","held-reader","held-directory","write-lock","foreign-late","child-before","child-after","child-parked"}){
            const std::string mode=scenario;const auto root=fixture/scenario;fs::create_directories(root/"Assets");Write(root/"Game.kubo",R"({"name":"Acknowledgement","startupScene":"Assets/Main.scene"})");
            Engine::Base::ProjectContext project;std::string error;Require(project.Open(root/"Game.kubo",error),error.c_str());
            Engine::Scene::EntityWorld world;world.CreateEntity("Before");PrefabValue definition;Require(CapturePrefabSubtree(world,1,definition,error),error.c_str());
            const auto file=root/L"Assets/ソース.prefab";Require(SavePrefabDocument(file,definition,error),error.c_str());
            Engine::Assets::AssetRegistry registry;Require(registry.Scan(root/"Assets",error),error.c_str());ProjectPrefabRevision revision;
            Require(ReadProjectPrefabRevision(project,registry,registry.FindByPath(file)->guid,revision,error),error.c_str());
            auto next=definition;next["world"]["entities"][size_t(0)]["name"]="After";
            if(mode.starts_with("child-")){
                {Child child(root,Engine::Base::PathFromUtf8(mode).wstring());const auto deadline=GetTickCount64()+5000;while(!fs::exists(root/"ready") && GetTickCount64()<deadline && WaitForSingleObject(child.info.hProcess,0)==WAIT_TIMEOUT)Sleep(10);Require(fs::exists(root/"ready"),"Owned child failed before interruption boundary");child.Kill();}
                const auto pending=root/"Assets/.kubo/prefab-transactions"/revision.guid;fs::path archive;
                if(mode=="child-after"){archive=Engine::Base::PathFromUtf8(Read(root/"ready"));Require(!fs::exists(pending) && ProjectPrefabDetail::Within(fs::canonical(archive),fs::canonical(root/"Assets")),"Acknowledged child archive missing/unsafe");}
                else {PrefabSourceRecoveryReport report;Require(PrefabSourceRecovery::Inspect(project,revision.guid,report,error),error.c_str());const auto before=Snapshot(root);
                    if(mode=="child-parked"){fs::path unchanged=L"unchanged";Require(report.state==PrefabSourceRecoveryState::Parked && !PrefabSourceAcknowledgement::ArchiveResolved(project,revision.guid,report,unchanged,error) && unchanged==L"unchanged" && Snapshot(root)==before,"Parked source incorrectly acknowledged");continue;}
                    Require(report.state==PrefabSourceRecoveryState::Published && PrefabSourceAcknowledgement::ArchiveResolved(project,revision.guid,report,archive,error),error.c_str());}
                Require(Read(file)==next.dump() && Read(revision.metadata)==revision.metadataBytes && Read(archive/"original.prefab")==revision.bytes && Identity(archive/"original.prefab")==revision.fileIdentity && fs::exists(archive/"pending.json"),"Child interruption lost source/metadata/archive original identity");
                ProjectPrefabRevision current;Require(ReadProjectPrefabRevision(project,registry,revision.guid,current,error),error.c_str());{HeldProjectPrefabWriter writer;Require(writer.Open(project,registry,current,error),error.c_str());}continue;
            }
            {
                PrefabSourcePublication publication;Require(publication.Prepare(project,registry,revision,next,error),error.c_str());
                fs::path busy=L"unchanged";PrefabSourceRecoveryReport invalid;
                Require(!PrefabSourceAcknowledgement::ArchiveResolved(project,revision.guid,invalid,busy,error) && busy==L"unchanged","Ack bypassed live writer");
                if(mode=="conflict")Require(!publication.Publish(error,[&]{Write(file,"external owner data");}),"Foreign source fixture overwritten");
                else if(mode!="prepared")Require(publication.Publish(error),error.c_str());
            }
            if(mode=="incomplete"){
                const auto directory=root/"Assets/.kubo/prefab-transactions"/revision.guid;fs::rename(directory/"pending.json",directory/"retained-intent");const auto before=Snapshot(root);fs::path unchanged=L"unchanged";PrefabSourceRecoveryReport invalid;
                Require(!PrefabSourceAcknowledgement::ArchiveResolved(project,revision.guid,invalid,unchanged,error) && unchanged==L"unchanged" && Snapshot(root)==before,"Incomplete intent acknowledged/changed");continue;
            }
            PrefabSourceRecoveryReport report;Require(PrefabSourceRecovery::Inspect(project,revision.guid,report,error),error.c_str());
            if(mode=="restored"){Require(PrefabSourceRecovery::RestoreOriginal(project,revision.guid,report,error),error.c_str());Require(PrefabSourceRecovery::Inspect(project,revision.guid,report,error),error.c_str());}
            if(mode=="stale-source")Write(file,Read(file)+"\n");
            if(mode=="stale-metadata")Write(revision.metadata,revision.metadataBytes+"\n");
            if(mode=="stale-report")report.nextBytes+="\n";
            if(mode=="foreign-retained")Write(report.directory/"foreign-not-owned","retain me");
            const auto before=Snapshot(root);auto transactionFiles=Snapshot(report.directory);
            const auto journalIdentity=Identity(report.directory/"pending.json");
            const auto sourceBytes=Read(file),metadataBytes=Read(revision.metadata);fs::path archived=L"unchanged",occupied;
            HANDLE external=INVALID_HANDLE_VALUE;
            if(mode=="held-reader")external=CreateFileW((report.directory/"pending.json").c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
            if(mode=="held-directory")external=CreateFileW(report.directory.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);
            if(mode=="held-reader" || mode=="held-directory")Require(external!=INVALID_HANDLE_VALUE,"External lease fixture failed");
            const bool acknowledged=PrefabSourceAcknowledgement::ArchiveResolved(project,revision.guid,report,archived,error,[&](const fs::path& destination){
                if(mode=="destination-conflict"){occupied=destination;fs::create_directory(destination);Write(destination/"foreign-owner","external");}
                if(mode=="callback-failure")throw std::runtime_error("Injected pre-archive failure");
                if(mode=="foreign-late"){Write(report.directory/"late-foreign","retain late data");transactionFiles=Snapshot(report.directory);}
                if(mode=="write-lock")for(const auto& path:{file,revision.metadata,report.directory/"pending.json",report.directory/"original.prefab"}){const auto writer=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);const auto code=GetLastError();if(writer!=INVALID_HANDLE_VALUE)CloseHandle(writer);Require(writer==INVALID_HANDLE_VALUE && code==ERROR_SHARING_VIOLATION,"Acknowledgement dropped write exclusion before final validation");}
            });
            if(external!=INVALID_HANDLE_VALUE)CloseHandle(external);
            if(mode=="destination-conflict" || mode=="callback-failure" || mode=="stale-source" || mode=="stale-report" || mode=="stale-metadata" || mode=="held-reader" || mode=="held-directory" || mode=="conflict"){
                Require(!acknowledged && archived==L"unchanged" && fs::exists(report.directory) && Snapshot(report.directory)==transactionFiles && Read(file)==sourceBytes && Read(revision.metadata)==metadataBytes,"Failed acknowledgement changed source/intent/output");
                if(mode=="destination-conflict")Require(Read(occupied/"foreign-owner")=="external","Archive replaced foreign directory");
                else Require(Snapshot(root)==before,"Failed acknowledgement changed file inventory");
                if(mode=="held-reader" || mode=="held-directory")Require(PrefabSourceAcknowledgement::ArchiveResolved(project,revision.guid,report,archived,error),error.c_str());
                continue;
            }
            Require(acknowledged,error.c_str());
            Require(!fs::exists(report.directory) && archived.parent_path()==root/"Assets/.kubo/prefab-archive" && Snapshot(archived)==transactionFiles && Read(file)==sourceBytes && Read(revision.metadata)==metadataBytes,"Archive lost transaction data/changed active source");
            Require(Identity(archived/"pending.json")==journalIdentity,"Archive changed journal native identity");
            ProjectPrefabRevision current;Require(ReadProjectPrefabRevision(project,registry,revision.guid,current,error),error.c_str());
            const bool keptOriginal=mode=="prepared" || mode=="restored";
            Require(current.fileIdentity==(keptOriginal?revision.fileIdentity:report.nextIdentity) && current.metadataIdentity==revision.metadataIdentity,"Acknowledgement changed active identities");
            Require(Identity(archived/(keptOriginal?"next.prefab":"original.prefab"))==(keptOriginal?report.nextIdentity:revision.fileIdentity),"Archive changed retained source identity");
            fs::path repeated=L"unchanged";Require(!PrefabSourceAcknowledgement::ArchiveResolved(project,revision.guid,report,repeated,error) && repeated==L"unchanged","Stale completed report acknowledged twice");
            {HeldProjectPrefabWriter writer;Require(writer.Open(project,registry,current,error),error.c_str());}
            // A second actual publication proves pending-name release, not only
            // a successful writer probe. The old archive remains byte-for-byte.
            {PrefabSourcePublication second;Require(second.Prepare(project,registry,current,definition,error) && second.Publish(error),error.c_str());}
            Require(Snapshot(archived)==transactionFiles,"Next generation modified retained archive");
        }
        fs::remove_all(fixture);std::cout<<"PASS: explicit resolved transaction archive, source/metadata/retained file IDs, child interruption before/after acknowledgement and Parked refusal, write/reader/directory exclusion, conflict/failure isolation and next publication generation\n";return 0;
    }catch(const std::exception& exception){std::cerr<<"FAIL: "<<exception.what()<<" (owned fixture retained)\n";return 1;}
}
