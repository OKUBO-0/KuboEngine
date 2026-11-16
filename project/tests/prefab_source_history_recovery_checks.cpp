#include "engine/editor/PrefabSourceHistoryEffect.h"
#include "engine/editor/PrefabSourceHistoryRecovery.h"
#include <fstream>
#include <iostream>
#include <map>
namespace {
namespace fs=std::filesystem;using namespace Engine::Editor;
void Require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void Write(const fs::path& path,const std::string& bytes){std::ofstream out(path,std::ios::binary);out<<bytes;out.close();Require(!out.fail(),"Fixture write failed");}
std::string Read(const fs::path& path){std::ifstream in(path,std::ios::binary);Require(in.good(),"Fixture read failed");return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};}
PrefabFileIdentity Identity(const fs::path& path){const auto handle=CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);Require(handle!=INVALID_HANDLE_VALUE,"Fixture identity failed");BY_HANDLE_FILE_INFORMATION info{};const bool ok=GetFileInformationByHandle(handle,&info)!=0;CloseHandle(handle);Require(ok,"Fixture native identity failed");return {info.dwVolumeSerialNumber,info.nFileIndexHigh,info.nFileIndexLow};}
struct File {std::string bytes;PrefabFileIdentity identity;bool operator==(const File&)const=default;};
std::map<fs::path,File> Snapshot(const fs::path& root){std::map<fs::path,File> result;for(const auto& entry:fs::recursive_directory_iterator(root))if(entry.is_regular_file())result.emplace(entry.path().lexically_relative(root),File{Read(entry.path()),Identity(entry.path())});return result;}
void ReplaceSame(const fs::path& path){Write(path.parent_path()/"replacement",Read(path));fs::rename(path,path.parent_path()/(path.filename().wstring()+L".retained"));fs::rename(path.parent_path()/"replacement",path);}
struct Handle {HANDLE value=INVALID_HANDLE_VALUE;~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}};
struct Child {
    PROCESS_INFORMATION info{};
    Child(const fs::path& root,const std::wstring& phase){wchar_t executable[32768];Require(GetModuleFileNameW(nullptr,executable,32768)!=0,"Child executable missing");std::wstring command=L"\""+std::wstring(executable)+L"\" --child \""+root.wstring()+L"\" "+phase;STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;Require(CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&info)!=0,"Child launch failed");}
    void Kill(){Require(TerminateProcess(info.hProcess,91)!=0 && WaitForSingleObject(info.hProcess,5000)==WAIT_OBJECT_0,"Owned child termination failed");DWORD code=0;Require(GetExitCodeProcess(info.hProcess,&code) && code==91,"Owned child exit mismatch");}
    ~Child(){if(info.hProcess){if(WaitForSingleObject(info.hProcess,0)==WAIT_TIMEOUT){TerminateProcess(info.hProcess,92);WaitForSingleObject(info.hProcess,5000);}CloseHandle(info.hThread);CloseHandle(info.hProcess);}}
};
int ChildMain(const fs::path& root,const std::wstring& phase){
    Engine::Base::ProjectContext project;std::string error;Require(project.Open(root/"Game.kubo",error),error.c_str());
    const auto file=root/L"Assets/ソース.prefab";const auto metadata=Engine::Assets::AssetRegistry::MetadataPath(file);
    const auto stop=[&]{Write(root/"ready.pending","ready");fs::rename(root/"ready.pending",root/"ready");Sleep(INFINITE);};
    if(phase.starts_with(L"restore")){
        const auto guid=PrefabValue::parse(Read(metadata))["guid"].get<crude_json::string>();PrefabSourceHistoryRecoveryReport report;
        Require(PrefabSourceHistoryRecovery::Inspect(project,guid,report,error) && PrefabSourceHistoryRecovery::RestoreBeforeReplay(project,guid,report,error,stop),error.c_str());return 2;
    }
    PrefabSourceRecoveryReport receipt;receipt.guid=PrefabValue::parse(Read(metadata))["guid"].get<crude_json::string>();receipt.source=file;receipt.metadataBytes=Read(metadata);receipt.metadataIdentity=Identity(metadata);
    for(const auto& entry:fs::directory_iterator(root/"Assets/.kubo/prefab-archive"))if(entry.is_directory())receipt.directory=entry.path();
    const bool undo=phase.find(L"redo")==std::wstring::npos;const bool completed=phase.starts_with(L"completed");
    const auto original=undo?receipt.directory/"original.prefab":file;const auto next=undo?file:receipt.directory/"next.prefab";
    receipt.originalBytes=Read(original);receipt.originalIdentity=Identity(original);receipt.nextBytes=Read(next);receipt.nextIdentity=Identity(next);
    PrefabSourceHistoryEffect effect(project,receipt,[&]{if(!completed)stop();});Require(effect.Begin(undo,error),error.c_str());stop();return 2;
}
}
int wmain(int argc,wchar_t** argv){
    const auto fixture=fs::temp_directory_path()/(L"kubo-history-recovery-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try {
        if(argc==4 && std::wstring(argv[1])==L"--child")return ChildMain(argv[2],argv[3]);
        for(const auto& scenario:{"parked-undo","parked-redo","completed-undo","completed-redo","conflict","source-replaced","source-edited","backup-replaced","stage-replaced","metadata-edit","metadata-replaced","missing-marker","missing-key","bad-direction","bad-guid","bad-format","traversal","absolute","nul-path","ads-path","trailing-dot","foreign-archive-guid","intent-mismatch","journal-edit","missing-journal","invalid-utf8","marker-hardlink","stage-hardlink","duplicate-guid","unknown-pending","writer-busy","source-reader","marker-budget","project-root-changed","restore-stale-source","restore-stale-metadata","restore-marker-replaced","restore-asset-move","restore-callback-failure","restore-collision","restore-interrupted","restore-redo-interrupted","restore-source-reader","restore-stage-reader","restore-directory-reader"}){
            const std::string mode=scenario;const auto root=fixture/scenario;fs::create_directories(root/"Assets");Write(root/"Game.kubo",R"({"name":"HistoryRecovery","startupScene":"Assets/Main.scene"})");
            Engine::Base::ProjectContext project;std::string error;Require(project.Open(root/"Game.kubo",error),error.c_str());
            Engine::Scene::EntityWorld world;world.CreateEntity("Before");PrefabValue definition;Require(CapturePrefabSubtree(world,1,definition,error),error.c_str());const auto file=root/L"Assets/ソース.prefab";Require(SavePrefabDocument(file,definition,error),error.c_str());
            Engine::Assets::AssetRegistry registry;Require(registry.Scan(root/"Assets",error),error.c_str());ProjectPrefabRevision revision;Require(ReadProjectPrefabRevision(project,registry,registry.FindByPath(file)->guid,revision,error),error.c_str());
            auto next=definition;next["world"]["entities"][size_t(0)]["name"]="After";PrefabSourceRecoveryReport receipt;fs::path archive;
            {PrefabSourcePublication publication;Require(publication.Prepare(project,registry,revision,next,error) && publication.Publish(error),error.c_str());}
            Require(PrefabSourceRecovery::Inspect(project,revision.guid,receipt,error) && PrefabSourceAcknowledgement::ArchiveResolved(project,revision.guid,receipt,archive,error),error.c_str());receipt.directory=archive;
            const bool undo=mode!="parked-redo" && mode!="completed-redo" && mode!="restore-redo-interrupted";
            const bool completed=mode.starts_with("completed") || mode=="source-reader" || mode=="source-replaced" || mode=="source-edited" || mode.starts_with("restore-");
            if(!undo){PrefabSourceHistoryEffect prepare(project,receipt);Require(prepare.Begin(true,error),error.c_str());prepare.End();}
            {
                Child child(root,(completed?L"completed-":L"parked-")+std::wstring(undo?L"undo":L"redo"));const auto deadline=GetTickCount64()+5000;
                while(!fs::exists(root/"ready") && GetTickCount64()<deadline && WaitForSingleObject(child.info.hProcess,0)==WAIT_TIMEOUT)Sleep(10);
                Require(fs::exists(root/"ready"),"Child failed before interruption boundary");PrefabSourceHistoryRecoveryReport busy;busy.source.guid="unchanged";
                Require(!PrefabSourceHistoryRecovery::Inspect(project,revision.guid,busy,error) && busy.source.guid=="unchanged","Inspected a live replay or changed failed output");child.Kill();
            }
            const auto pending=root/"Assets/.kubo/prefab-transactions"/revision.guid;const auto marker=pending/"history.json";const auto journal=archive/"pending.json";
            bool accepted=true;auto state=completed?(undo?PrefabSourceRecoveryState::Prepared:PrefabSourceRecoveryState::Published):PrefabSourceRecoveryState::Parked;
            if(mode=="conflict"){Write(file,"external data");state=PrefabSourceRecoveryState::Conflict;}
            if(mode=="source-replaced"){ReplaceSame(file);state=PrefabSourceRecoveryState::Conflict;}
            if(mode=="source-edited"){Write(file,Read(file)+"\n");state=PrefabSourceRecoveryState::Conflict;}
            if(mode=="backup-replaced"){ReplaceSame(archive/"original.prefab");state=PrefabSourceRecoveryState::Conflict;}
            if(mode=="stage-replaced"){ReplaceSame(archive/"next.prefab");state=PrefabSourceRecoveryState::Conflict;}
            if(mode=="metadata-edit"){Write(revision.metadata,revision.metadataBytes+"\n");accepted=false;}
            if(mode=="metadata-replaced"){ReplaceSame(revision.metadata);accepted=false;}
            if(mode=="missing-marker"){fs::rename(marker,pending/"retained-marker");accepted=false;}
            if(mode=="missing-journal"){fs::rename(journal,archive/"retained-journal");accepted=false;}
            if(mode=="missing-key" || mode=="bad-direction" || mode=="bad-guid" || mode=="bad-format" || mode=="traversal" || mode=="absolute" || mode=="nul-path" || mode=="ads-path" || mode=="trailing-dot" || mode=="foreign-archive-guid" || mode=="intent-mismatch"){
                auto value=PrefabValue::parse(Read(marker));accepted=false;
                if(mode=="missing-key"){value.get<crude_json::object>().erase("undo");value["unknown"]=true;}
                if(mode=="bad-direction")value["undo"]="true";
                if(mode=="bad-guid")value["guid"]=std::string(32,'f');
                if(mode=="bad-format")value["format"]="unknown";
                if(mode=="traversal")value["archiveRelative"]="../outside";
                if(mode=="absolute")value["archiveRelative"]=Engine::Base::PathToUtf8(archive);
                if(mode=="nul-path")value["archiveRelative"]=value["archiveRelative"].get<crude_json::string>()+std::string("\0foreign",8);
                if(mode=="ads-path")value["archiveRelative"]=value["archiveRelative"].get<crude_json::string>()+":foreign";
                if(mode=="trailing-dot")value["archiveRelative"]=value["archiveRelative"].get<crude_json::string>()+".";
                if(mode=="foreign-archive-guid")value["archiveRelative"]=".kubo/prefab-archive/"+std::string(32,'f')+"-foreign";
                if(mode=="intent-mismatch")value["intent"]["nextBytes"]=receipt.nextBytes+"\n";
                Write(marker,value.dump());
            }
            if(mode=="journal-edit"){auto value=PrefabValue::parse(Read(journal));value["nextBytes"]=receipt.nextBytes+"\n";Write(journal,value.dump());accepted=false;}
            if(mode=="invalid-utf8"){Write(marker,std::string("\xff"));accepted=false;}
            if(mode=="marker-hardlink"){Require(CreateHardLinkW((root/"marker-link").c_str(),marker.c_str(),nullptr)!=0,"Fixture marker hardlink failed");accepted=false;}
            if(mode=="stage-hardlink"){Require(CreateHardLinkW((root/"stage-link").c_str(),(archive/"next.prefab").c_str(),nullptr)!=0,"Fixture stage hardlink failed");accepted=false;}
            if(mode=="duplicate-guid"){Write(root/"Assets/duplicate.prefab",revision.bytes);Write(Engine::Assets::AssetRegistry::MetadataPath(root/"Assets/duplicate.prefab"),revision.metadataBytes);accepted=false;}
            if(mode=="unknown-pending")Write(pending/"unknown-data","retain me");
            if(mode=="marker-budget"){Write(marker,std::string(24*1024*1024+1,' '));accepted=false;}
            if(mode=="project-root-changed"){fs::create_directory(root/"NewAssets");Write(root/"Game.kubo",R"({"name":"Changed","assetDirectory":"NewAssets"})");accepted=false;}
            const auto files=Snapshot(root);PrefabSourceHistoryRecoveryReport report;report.source.guid="unchanged";
            {
                Handle held;
                if(mode=="writer-busy"){held.value=CreateFileW((root/"Assets/.kubo/prefab-writers"/(revision.guid+".lock")).c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);Require(held.value!=INVALID_HANDLE_VALUE,"Fixture writer lock failed");accepted=false;}
                if(mode=="source-reader"){held.value=CreateFileW(file.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);Require(held.value!=INVALID_HANDLE_VALUE,"Fixture source reader failed");}
                const bool result=PrefabSourceHistoryRecovery::Inspect(project,revision.guid,report,error);
                Require(result==accepted,"Unexpected history recovery inspection result");
            }
            Require(Snapshot(root)==files,"Read-only history inspection changed bytes/file identities/inventory");
            if(accepted)Require(report.undo==undo && report.pending==pending && report.source.directory==archive && report.source.source==file && report.source.state==state && report.source.originalBytes==revision.bytes && report.source.originalIdentity==revision.fileIdentity && report.source.nextBytes==receipt.nextBytes && report.markerBytes==Read(marker) && report.markerIdentity==Identity(marker),"History recovery lost direction/source/marker state");
            else Require(report.source.guid=="unchanged" && report.markerBytes.empty() && report.pending.empty(),"Failed history inspection published output");
            PrefabSourceRecoveryReport generic;Require(!PrefabSourceRecovery::Inspect(project,revision.guid,generic,error),"Generic recovery consumed a history-only marker");
            if(!accepted)continue;
            {
                fs::path unchanged="unchanged";
                Require(!PrefabSourceHistoryRecovery::ArchiveRestored(project,revision.guid,report,unchanged,error) && unchanged==fs::path("unchanged") && Snapshot(root)==files,
                    "Unrestored/conflicting history was acknowledged or failed output changed");
            }
            if(state==PrefabSourceRecoveryState::Conflict){Require(!PrefabSourceHistoryRecovery::RestoreBeforeReplay(project,revision.guid,report,error) && Snapshot(root)==files,"Conflicting history recovery changed files");continue;}
            if(mode=="restore-stale-source")Write(file,Read(file)+"\n");
            if(mode=="restore-stale-metadata")Write(revision.metadata,revision.metadataBytes+"\n");
            if(mode=="restore-marker-replaced")ReplaceSame(marker);
            if(mode=="restore-asset-move")Write(root/"Assets/.kubo/asset-move.pending.json","foreign pending");
            const auto beforeRestore=Snapshot(root);
            if(mode=="restore-source-reader" || mode=="restore-stage-reader" || mode=="restore-directory-reader"){
                {
                    Handle reader;const auto path=mode=="restore-source-reader"?file:mode=="restore-stage-reader"?archive/"next.prefab":archive;
                    reader.value=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,mode=="restore-directory-reader"?FILE_FLAG_BACKUP_SEMANTICS:0,nullptr);
                    Require(reader.value!=INVALID_HANDLE_VALUE,"Fixture recovery reader failed");
                    Require(!PrefabSourceHistoryRecovery::RestoreBeforeReplay(project,revision.guid,report,error),"Source restore bypassed external reader");
                }
                Require(Snapshot(root)==beforeRestore,"Reader-rejected restore changed data");
                // Retry below after the external reader releases ownership.
            }
            if(mode=="restore-stale-source" || mode=="restore-stale-metadata" || mode=="restore-marker-replaced" || mode=="restore-asset-move"){
                Require(!PrefabSourceHistoryRecovery::RestoreBeforeReplay(project,revision.guid,report,error) && Snapshot(root)==beforeRestore,"Stale/busy restore changed files");continue;
            }
            if(mode=="restore-callback-failure" || mode=="restore-collision"){
                Require(!PrefabSourceHistoryRecovery::RestoreBeforeReplay(project,revision.guid,report,error,[&]{if(mode=="restore-collision")Write(file,"foreign late source");else throw std::runtime_error("Injected restore failure");}),"Injected restore failure succeeded");
                if(mode=="restore-callback-failure")Require(Snapshot(root)==beforeRestore,"Restore rollback changed identities/bytes");
                else Require(Read(file)=="foreign late source" && Read(archive/"original.prefab")==revision.bytes && Read(archive/"next.prefab")==receipt.nextBytes && Read(marker)==report.markerBytes,"Restore collision lost pending/source pair");continue;
            }
            if(mode=="restore-interrupted" || mode=="restore-redo-interrupted"){
                fs::rename(root/"ready",root/"first-ready");
                {Child child(root,L"restore");const auto deadline=GetTickCount64()+5000;while(!fs::exists(root/"ready") && GetTickCount64()<deadline && WaitForSingleObject(child.info.hProcess,0)==WAIT_TIMEOUT)Sleep(10);Require(fs::exists(root/"ready"),"Restore child failed before park");child.Kill();}
                Require(!fs::exists(file) && Read(archive/"original.prefab")==revision.bytes && Read(archive/"next.prefab")==receipt.nextBytes && Read(marker)==report.markerBytes,"Interrupted restore lost pair/marker");
                Require(PrefabSourceHistoryRecovery::Inspect(project,revision.guid,report,error) && report.source.state==PrefabSourceRecoveryState::Parked,"Interrupted restore not diagnosed Parked");
            }
            Require(PrefabSourceHistoryRecovery::RestoreBeforeReplay(project,revision.guid,report,error),error.c_str());
            Require(Read(file)==(undo?receipt.nextBytes:revision.bytes) && Identity(file)==(undo?receipt.nextIdentity:revision.fileIdentity) && Read(marker)==report.markerBytes && Identity(marker)==report.markerIdentity && Read(revision.metadata)==revision.metadataBytes,"Explicit restore lost pre-replay source/marker identity");
            PrefabSourceHistoryRecoveryReport restored;Require(PrefabSourceHistoryRecovery::Inspect(project,revision.guid,restored,error) && restored.source.state==(undo?PrefabSourceRecoveryState::Published:PrefabSourceRecoveryState::Prepared),"Restored physical state incorrect");
            const auto restoredFiles=Snapshot(root);Require(PrefabSourceHistoryRecovery::RestoreBeforeReplay(project,revision.guid,restored,error) && Snapshot(root)==restoredFiles,"Already-restored no-op changed data");
            HeldProjectPrefabWriter writer;Require(!writer.Open(project,registry,revision,error),"Source-only restore acknowledged history pending");
            fs::path retained;
            const auto sourceBeforeArchive=Identity(file);const auto metadataBeforeArchive=Identity(revision.metadata);
            Require(PrefabSourceHistoryRecovery::ArchiveRestored(project,revision.guid,restored,retained,error),error.c_str());
            Require(!fs::exists(pending) && retained.parent_path()==archive && Read(retained/"history.json")==restored.markerBytes && Identity(retained/"history.json")==restored.markerIdentity && Identity(file)==sourceBeforeArchive && Identity(revision.metadata)==metadataBeforeArchive,"History completion lost marker/source/metadata identities");
            if(mode=="unknown-pending")Require(Read(retained/"unknown-data")=="retain me","History acknowledgement deleted unknown data");
            PrefabSourceHistoryRecoveryReport stale;Require(!PrefabSourceHistoryRecovery::Inspect(project,revision.guid,stale,error),"Acknowledged history inspected old pending name");
            ProjectPrefabRevision freshRevision;Require(ReadProjectPrefabRevision(project,registry,revision.guid,freshRevision,error) && writer.Open(project,registry,freshRevision,error),error.c_str());writer.Release();
        }
        fs::remove_all(fixture);std::cout<<"PASS: retained history diagnosis/pre-replay source restore after owned child interruption and explicit whole-marker archive/new writer admission, native identities/bytes, unsafe/unrestored/reader/stale refusal, rollback/collision and unknown data preservation (source-only, not Scene/history acknowledgement)\n";return 0;
    }catch(const std::exception& exception){std::cerr<<"FAIL: "<<exception.what()<<" (owned fixture retained)\n";return 1;}
}
