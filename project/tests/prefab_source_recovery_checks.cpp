#include "engine/editor/PrefabSourcePublication.h"
#include "engine/editor/PrefabSourceRecovery.h"
#include <fstream>
#include <iostream>
#include <map>
namespace {
namespace fs=std::filesystem;
using namespace Engine::Editor;
void Require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void Write(const fs::path& path,const std::string& bytes){std::ofstream out(path,std::ios::binary);out<<bytes;out.close();Require(!out.fail(),"Fixture write failed");}
std::string Read(const fs::path& path){std::ifstream in(path,std::ios::binary);Require(in.good(),"Fixture read failed");return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};}
std::map<fs::path,std::string> Snapshot(const fs::path& root){std::map<fs::path,std::string> result;for(const auto& entry:fs::recursive_directory_iterator(root))if(entry.is_regular_file())result.emplace(entry.path().lexically_relative(root),Read(entry.path()));return result;}
struct Child {
    PROCESS_INFORMATION info{};
    explicit Child(const fs::path& root,const std::wstring& phase){
        wchar_t executable[32768];Require(GetModuleFileNameW(nullptr,executable,32768)!=0,"Child executable missing");
        std::wstring command=L"\""+std::wstring(executable)+L"\" --child \""+root.wstring()+L"\" "+phase;
        STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
        Require(CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&info)!=0,"Child start failed");
    }
    void Kill(){Require(TerminateProcess(info.hProcess,91)!=0,"Owned child termination failed");Require(WaitForSingleObject(info.hProcess,5000)==WAIT_OBJECT_0,"Owned child did not exit");DWORD code=0;Require(GetExitCodeProcess(info.hProcess,&code) && code==91,"Child termination exit mismatch");}
    ~Child(){if(info.hProcess){if(WaitForSingleObject(info.hProcess,0)==WAIT_TIMEOUT){TerminateProcess(info.hProcess,92);WaitForSingleObject(info.hProcess,5000);}CloseHandle(info.hThread);CloseHandle(info.hProcess);}}
};
int ChildMain(const fs::path& root,const std::wstring& phase){
    Engine::Base::ProjectContext project;std::string error;
    Require(project.Open(root/"Game.kubo",error),error.c_str());Engine::Assets::AssetRegistry registry;
    Require(registry.Scan(root/"Assets",error),error.c_str());const auto file=root/L"Assets/ソース.prefab";
    ProjectPrefabRevision revision;Require(ReadProjectPrefabRevision(project,registry,registry.FindByPath(file)->guid,revision,error),error.c_str());
    auto next=revision.definition;next["world"]["entities"][size_t(0)]["name"]="After";
    const auto stop=[&]{Write(root/"ready","ready");Sleep(INFINITE);};
    {
        PrefabSourcePublication publication;Require(publication.Prepare(project,registry,revision,next,error),error.c_str());
        if(phase==L"prepared")stop();
        else if(phase==L"parked")Require(publication.Publish(error,stop),error.c_str());
        else {Require(publication.Publish(error),error.c_str());if(phase!=L"restore-parked")stop();}
    }
    if(phase==L"restore-parked"){
        PrefabSourceRecoveryReport report;Require(PrefabSourceRecovery::Inspect(project,revision.guid,report,error),error.c_str());
        Require(PrefabSourceRecovery::RestoreOriginal(project,revision.guid,report,error,stop),error.c_str());
    }
    return 2;
}
}
int wmain(int argc,wchar_t** argv){
    const auto fixture=fs::temp_directory_path()/(L"kubo-prefab-recovery-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try {
        if(argc==4 && std::wstring(argv[1])==L"--child")return ChildMain(argv[2],argv[3]);
        for(const auto& scenario:{"prepared","parked","published","conflict","missing-key","traversal","metadata","metadata-replacement","replacement","edited","missing-journal","invalid-id","invalid-utf8","hardlink","restore-failure","restore-conflict","restore-interrupted","stale-source","stale-metadata","stale-stage","stale-backup"}){
            const auto root=fixture/scenario;fs::create_directories(root/"Assets");Write(root/"Game.kubo",R"({"name":"Recovery","startupScene":"Assets/Main.scene"})");
            Engine::Base::ProjectContext project;std::string error;Require(project.Open(root/"Game.kubo",error),error.c_str());
            Engine::Scene::EntityWorld world;world.CreateEntity("Before");PrefabValue definition;Require(CapturePrefabSubtree(world,1,definition,error),error.c_str());
            const auto file=root/L"Assets/ソース.prefab";Require(SavePrefabDocument(file,definition,error),error.c_str());
            Engine::Assets::AssetRegistry registry;Require(registry.Scan(root/"Assets",error),error.c_str());
            ProjectPrefabRevision revision;Require(ReadProjectPrefabRevision(project,registry,registry.FindByPath(file)->guid,revision,error),error.c_str());
            const std::string mode=scenario;const auto phase=mode=="prepared"?L"prepared":mode=="parked" || mode=="conflict"?L"parked":mode=="restore-interrupted"?L"restore-parked":L"published";
            {
                Child child(root,phase);const auto deadline=GetTickCount64()+5000;
                while(!fs::exists(root/"ready") && GetTickCount64()<deadline && WaitForSingleObject(child.info.hProcess,0)==WAIT_TIMEOUT)Sleep(10);
                Require(fs::exists(root/"ready"),"Child failed before intended interruption point");
                PrefabSourceRecoveryReport busy;busy.guid="unchanged";
                Require(!PrefabSourceRecovery::Inspect(project,revision.guid,busy,error) && busy.guid=="unchanged","Recovery inspected live writer or changed failed output");
                Require(!PrefabSourceRecovery::RestoreOriginal(project,revision.guid,busy,error),"Restore bypassed live writer lock");
                child.Kill();
            }
            const auto directory=root/"Assets/.kubo/prefab-transactions"/revision.guid;const auto journalPath=directory/"pending.json";
            if(mode=="conflict")Write(file,"external owner data");
            else if(mode=="missing-key" || mode=="traversal" || mode=="invalid-id"){
                auto journal=PrefabValue::parse(Read(journalPath));
                if(mode=="missing-key"){journal.get<crude_json::object>().erase("originalIdentity");journal["unknownIdentity"]=1.0;}
                else if(mode=="traversal")journal["sourceRelative"]="../outside.prefab";
                else journal["originalIdentity"]["low"]=-1.0;
                Write(journalPath,journal.dump());
            }else if(mode=="metadata")Write(revision.metadata,revision.metadataBytes+"\n");
            else if(mode=="metadata-replacement"){
                Write(directory/"foreign-meta",revision.metadataBytes);fs::rename(revision.metadata,directory/"retained-meta");fs::rename(directory/"foreign-meta",revision.metadata);
            }
            else if(mode=="replacement"){
                const auto backup=directory/"original.prefab";Write(directory/"foreign.prefab",Read(backup));fs::rename(backup,directory/"retained-original.prefab");fs::rename(directory/"foreign.prefab",backup);
            }else if(mode=="edited")Write(directory/"original.prefab",revision.bytes+"\n");
            else if(mode=="missing-journal")fs::rename(journalPath,directory/"retained-intent.json");
            else if(mode=="invalid-utf8")Write(journalPath,std::string(1,static_cast<char>(0xff)));
            else if(mode=="hardlink")Require(CreateHardLinkW((directory/"backup-alias").c_str(),(directory/"original.prefab").c_str(),nullptr)!=0,"Owned hardlink fixture failed");
            const auto before=Snapshot(root);PrefabSourceRecoveryReport report;report.guid="unchanged";
            const bool success=PrefabSourceRecovery::Inspect(project,revision.guid,report,error);
            Require(Snapshot(root)==before,"Read-only recovery altered file inventory/content");
            if(mode=="missing-key" || mode=="traversal" || mode=="metadata" || mode=="metadata-replacement" || mode=="missing-journal" || mode=="invalid-id" || mode=="invalid-utf8" || mode=="hardlink"){
                Require(!success && report.guid=="unchanged" && !error.empty(),"Malformed/unsafe intent or metadata accepted");
                Require(!PrefabSourceRecovery::RestoreOriginal(project,revision.guid,report,error) && Snapshot(root)==before,"Restore changed invalid transaction");
            }
            else {
                Require(success,error.c_str());const auto expected=mode=="prepared"?PrefabSourceRecoveryState::Prepared:mode=="parked" || mode=="restore-interrupted"?PrefabSourceRecoveryState::Parked:mode=="published" || mode=="restore-failure" || mode=="restore-conflict" || mode.starts_with("stale-")?PrefabSourceRecoveryState::Published:PrefabSourceRecoveryState::Conflict;
                Require(report.state==expected && report.originalBytes==revision.bytes && report.metadataBytes==revision.metadataBytes,"Recovery state/retained bytes mismatch");
                if(expected==PrefabSourceRecoveryState::Conflict){Require(!PrefabSourceRecovery::RestoreOriginal(project,revision.guid,report,error) && Snapshot(root)==before,"Restore overwrote conflict");continue;}
                if(mode.starts_with("stale-")){
                    if(mode=="stale-source")Write(file,report.nextBytes+"\n");
                    else if(mode=="stale-metadata")Write(revision.metadata,revision.metadataBytes+"\n");
                    else if(mode=="stale-stage")Write(directory/"next.prefab","foreign stage owner");
                    else {Write(directory/"foreign-backup",revision.bytes);fs::rename(directory/"original.prefab",directory/"retained-original.prefab");fs::rename(directory/"foreign-backup",directory/"original.prefab");}
                    const auto changedFiles=Snapshot(root);Require(!PrefabSourceRecovery::RestoreOriginal(project,revision.guid,report,error) && Snapshot(root)==changedFiles,"Filesystem changed after inspection but restore overwrote/lost files");continue;
                }
                auto stale=report;stale.nextBytes+="\n";
                Require(!PrefabSourceRecovery::RestoreOriginal(project,revision.guid,stale,error) && Snapshot(root)==before,"Altered/stale report authorized restore");
                const auto busyPath=fs::exists(file)?file:directory/"original.prefab";
                const auto reader=CreateFileW(busyPath.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
                Require(reader!=INVALID_HANDLE_VALUE,"Read lease fixture failed");
                const bool readerBlocked=!PrefabSourceRecovery::RestoreOriginal(project,revision.guid,report,error);CloseHandle(reader);
                Require(readerBlocked && Snapshot(root)==before,"Restore bypassed existing read lease");
                if(mode=="restore-failure" || mode=="restore-conflict"){
                    const bool restored=PrefabSourceRecovery::RestoreOriginal(project,revision.guid,report,error,[&]{if(mode=="restore-conflict")Write(file,"late external data");else throw std::runtime_error("Injected restore park failure");});
                    Require(!restored && Read(directory/"original.prefab")==revision.bytes && Read(revision.metadata)==revision.metadataBytes,"Restore failure lost original backup/metadata");
                    PrefabSourceRecoveryReport failed;Require(PrefabSourceRecovery::Inspect(project,revision.guid,failed,error),error.c_str());
                    if(mode=="restore-failure")Require(Snapshot(root)==before && failed.state==PrefabSourceRecoveryState::Published,"Injected restore failure did not safely roll back next source");
                    else Require(Read(file)=="late external data" && Read(directory/"next.prefab")==report.nextBytes && failed.state==PrefabSourceRecoveryState::Conflict,"Late restore collision overwrote data/lost stage");
                    continue;
                }
                const auto journalBytes=Read(journalPath);Require(PrefabSourceRecovery::RestoreOriginal(project,revision.guid,report,error),error.c_str());
                PrefabSourceRecoveryReport restored;Require(PrefabSourceRecovery::Inspect(project,revision.guid,restored,error),error.c_str());
                Require(restored.state==PrefabSourceRecoveryState::Prepared && restored.originalIdentity==revision.fileIdentity && restored.nextIdentity==report.nextIdentity && Read(file)==revision.bytes && Read(directory/"next.prefab")==report.nextBytes && !fs::exists(directory/"original.prefab") && Read(journalPath)==journalBytes && Read(revision.metadata)==revision.metadataBytes,"Restore did not preserve original/next IDs, raw bytes and intent");
                const auto restoredFiles=Snapshot(root);Require(PrefabSourceRecovery::RestoreOriginal(project,revision.guid,restored,error) && Snapshot(root)==restoredFiles,"Prepared restore was not idempotent");
                if(report.state!=PrefabSourceRecoveryState::Prepared)Require(!PrefabSourceRecovery::RestoreOriginal(project,revision.guid,report,error) && Snapshot(root)==restoredFiles,"Old pre-restore state report reused");
                HeldProjectPrefabWriter blockedWriter;Require(!blockedWriter.Open(project,registry,revision,error),"Restore silently acknowledged pending transaction");
            }
        }
        fs::remove_all(fixture);std::cout<<"PASS: owned-child interruption at publication/restore boundaries, native-ID/byte recovery diagnosis and owned no-replace original restoration, read/writer/stale report refusal, conflict preservation and retained pending intent\n";return 0;
    }catch(const std::exception& exception){std::cerr<<"FAIL: "<<exception.what()<<" (owned fixture retained)\n";return 1;}
}
